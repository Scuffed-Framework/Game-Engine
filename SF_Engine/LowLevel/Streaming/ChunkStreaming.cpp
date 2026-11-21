#include "ChunkStreaming.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace SF::Engine
{
    StreamingManager::StreamingManager(ThreadPool &pool, IChunkHandler &handler, const StreamingSettings &settings) :
        m_pool(pool), m_handler(handler), m_settings(settings), m_shared(std::make_shared<Shared>())
    {
        m_settings.chunkSize    = std::max(m_settings.chunkSize, 0.001f);
        m_settings.loadRadius   = std::max<int32_t>(m_settings.loadRadius, 0);
        m_settings.unloadRadius = std::max(m_settings.unloadRadius, m_settings.loadRadius);

        m_maxInFlight =
                m_settings.maxInFlight != 0 ? m_settings.maxInFlight : std::max<uint32_t>(1, pool.GetThreadCount() / 2);
    }

    StreamingManager::~StreamingManager()
    {
        for (auto &entry: m_entries | std::views::values)
            if (entry.cancel)
                entry.cancel->store(true, std::memory_order_relaxed);

        // Tasks use the handler; wait until none can still be running. (Queued-but-canceled tasks exit fast.)
        const auto &running = m_shared->running;
        for (uint32_t n = running.load(); n != 0; n = running.load())
            running.wait(n);
    }

    ChunkCoord StreamingManager::ToChunk(const StreamFocus &f) const
    {
        return {.x = static_cast<int32_t>(std::floor(f.x / m_settings.chunkSize)),
                .y = static_cast<int32_t>(std::floor(f.y / m_settings.chunkSize))};
    }

    void StreamingManager::Update(const std::span<const StreamFocus> foci) { UpdateImpl(foci, false); }

    void StreamingManager::LoadBlocking(const std::span<const StreamFocus> foci)
    {
        for (;;)
        {
            UpdateImpl(foci, true);

            if (IsSettled())
                return;

            std::unique_lock lock(m_shared->mutex);
            m_shared->cv.wait(lock, [this] { return !m_shared->done.empty(); });
        }
    }

    void StreamingManager::UpdateImpl(const std::span<const StreamFocus> foci, const bool unlimitedBudget)
    {
        DrainCompletions();

        if (!foci.empty())
        {
            std::vector<ChunkCoord> cells;
            cells.reserve(foci.size());
            for (const StreamFocus &f: foci)
                cells.push_back(ToChunk(f));

            if (cells != m_lastCells)
            {
                m_lastCells = cells;
                Rebuild(cells);
            }
        }

        PumpRequests();
        CommitReady(unlimitedBudget);
    }

    void StreamingManager::Rebuild(const std::vector<ChunkCoord> &cells)
    {
        const int64_t R  = m_settings.loadRadius;
        const int64_t U  = m_settings.unloadRadius;
        const int64_t R2 = R * R;
        const int64_t U2 = U * U;

        // Evict everything outside the unload radius of every focus.
        for (auto it = m_entries.begin(); it != m_entries.end();)
        {
            int64_t best = std::numeric_limits<int64_t>::max();
            for (const auto &[x, y]: cells)
            {
                const int64_t dx = static_cast<int64_t>(it->first.x) - x;
                const int64_t dy = static_cast<int64_t>(it->first.y) - y;
                best             = std::min(best, dx * dx + dy * dy);
            }

            if (best > U2)
            {
                EvictEntry(it->first, it->second);
                it = m_entries.erase(it);
            } else
                ++it;
        }

        // Everything inside the load radius that we don't know about yet is wanted, nearest first.
        std::unordered_map<ChunkCoord, int64_t> wanted;
        for (const auto &[x, y]: cells)
        {
            for (int64_t dy = -R; dy <= R; ++dy)
            {
                for (int64_t dx = -R; dx <= R; ++dx)
                {
                    const int64_t d2 = dx * dx + dy * dy;
                    if (d2 > R2)
                        continue;

                    const ChunkCoord coord{.x = static_cast<int32_t>(x + dx), .y = static_cast<int32_t>(y + dy)};
                    if (m_entries.contains(coord))
                        continue;

                    if (auto [it, inserted] = wanted.try_emplace(coord, d2); !inserted)
                        it->second = std::min(it->second, d2);
                }
            }
        }

        m_pending.clear();
        m_pending.reserve(wanted.size());
        for (const auto &[coord, d2]: wanted)
            m_pending.push_back({.coord = coord, .dist2 = d2});

        std::ranges::sort(m_pending,
                          [](const Pending &a, const Pending &b) { return a.dist2 > b.dist2; }); // nearest at the back
    }

    void StreamingManager::DrainCompletions()
    {
        std::vector<Completed> batch;
        {
            std::lock_guard lock(m_shared->mutex);
            batch.swap(m_shared->done);
        }

        for (auto &[coord, requestId, payload, failed]: batch)
        {
            --m_inFlight;

            auto it = m_entries.find(coord);
            if (it == m_entries.end() || it->second.requestId != requestId || it->second.state != State::Loading)
                continue; // evicted (or evicted and re-requested) while loading: stale result

            if (failed)
                it->second.state = State::Failed;
            else if (!payload)
                it->second.state = State::Empty;
            else
            {
                it->second.state = State::Loaded;
                m_commitQueue.push_back({.coord = coord, .requestId = requestId, .payload = std::move(payload)});
            }
        }
    }

    void StreamingManager::PumpRequests()
    {
        while (m_inFlight < m_maxInFlight && !m_pending.empty())
        {
            const ChunkCoord coord = m_pending.back().coord;
            m_pending.pop_back();

            if (m_entries.contains(coord))
                continue;

            RequestLoad(coord);
        }
    }

    void StreamingManager::RequestLoad(ChunkCoord coord)
    {
        Entry entry;
        entry.state     = State::Loading;
        entry.requestId = ++m_nextRequestId;
        entry.cancel    = std::make_shared<std::atomic<bool>>(false);

        auto [it, inserted] = m_entries.insert_or_assign(coord, entry);

        ++m_inFlight;
        m_shared->running.fetch_add(1, std::memory_order_relaxed);

        try
        {
            m_pool.Dispatch(
                    [shared = m_shared, handler = &m_handler, coord, id = entry.requestId, cancel = entry.cancel]
                    {
                        Completed done;
                        done.coord     = coord;
                        done.requestId = id;

                        if (!cancel->load(std::memory_order_relaxed))
                        {
                            try
                            {
                                done.payload = handler->Load(coord, *cancel);
                            } catch (...)
                            {
                                done.failed = true; // hook your logging here
                            }
                        }

                        {
                            std::lock_guard lock(shared->mutex);
                            shared->done.push_back(std::move(done));
                        }
                        shared->cv.notify_all();

                        // Last touch of `handler`; must stay after the push above.
                        shared->running.fetch_sub(1, std::memory_order_acq_rel);
                        shared->running.notify_all();
                    });
        } catch (...) // pool already stopped
        {
            m_shared->running.fetch_sub(1, std::memory_order_relaxed);
            --m_inFlight;
            it->second.state = State::Failed;
        }
    }

    void StreamingManager::CommitReady(const bool unlimitedBudget)
    {
        using Clock       = std::chrono::steady_clock;
        const auto start  = Clock::now();
        const auto budget = std::chrono::duration<double, std::milli>(m_settings.commitBudgetMs);

        uint32_t committed = 0;
        while (!m_commitQueue.empty())
        {
            if (!unlimitedBudget && committed >= m_settings.minCommitsPerFrame && Clock::now() - start >= budget)
                break;

            auto [coord, requestId, payload] = std::move(m_commitQueue.front());
            m_commitQueue.pop_front();

            auto it = m_entries.find(coord);
            if (it == m_entries.end() || it->second.requestId != requestId || it->second.state != State::Loaded)
                continue; // evicted while waiting for commit

            m_handler.Commit(coord, std::move(payload));
            it->second.state = State::Resident;
            ++committed;
        }
    }

    void StreamingManager::EvictEntry(const ChunkCoord coord, const Entry &entry) const
    {
        switch (entry.state)
        {
            case State::Loading:
                entry.cancel->store(true, std::memory_order_relaxed);
                break;
            case State::Resident:
                m_handler.Unload(coord);
                break;
            default:
                break;
        }
    }

    void StreamingManager::UnloadAll()
    {
        for (auto &[coord, entry]: m_entries)
            EvictEntry(coord, entry);

        m_entries.clear();
        m_pending.clear();
        m_commitQueue.clear();
        m_lastCells.clear();
    }

    bool StreamingManager::IsResident(const ChunkCoord coord) const
    {
        const auto it = m_entries.find(coord);
        return it != m_entries.end() && it->second.state == State::Resident;
    }

    bool StreamingManager::IsSettled() const { return m_pending.empty() && m_inFlight == 0 && m_commitQueue.empty(); }

    StreamingManager::Stats StreamingManager::GetStats() const
    {
        Stats s;
        s.pending = m_pending.size();
        for (const auto &entry: m_entries | std::views::values)
        {
            switch (entry.state)
            {
                case State::Loading:
                    ++s.loading;
                    break;
                case State::Loaded:
                    ++s.awaitingCommit;
                    break;
                case State::Resident:
                    ++s.resident;
                    break;
                case State::Empty:
                    ++s.empty;
                    break;
                case State::Failed:
                    ++s.failed;
                    break;
            }
        }
        return s;
    }
} // namespace SF::Engine
