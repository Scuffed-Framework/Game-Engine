#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <span>
#include <unordered_map>
#include <vector>

#include <Platform/Threading/ThreadPool.hpp>

namespace SF::Engine
{
    /// Integer cell coordinate of a streaming chunk (a 2D grid; add z if you need layers).
    struct ChunkCoord
    {
        int32_t x = 0;
        int32_t y = 0;

        bool operator==(const ChunkCoord &) const = default;
    };
} // namespace SF::Engine

template<>
struct std::hash<SF::Engine::ChunkCoord>
{
    size_t operator()(const SF::Engine::ChunkCoord &c) const noexcept
    {
        uint64_t k = (static_cast<uint64_t>(static_cast<uint32_t>(c.x)) << 32) | static_cast<uint32_t>(c.y);
        k ^= k >> 33;
        k *= 0xff51afd7ed558ccdULL;
        k ^= k >> 33;
        return static_cast<size_t>(k);
    }
};

namespace SF::Engine
{
    // CPU-side decoded chunk data produced on a worker thread and consumed on the main thread.
    // Derive more stuff (terrain heights, mesh blobs, entity spawn lists, ...).
    struct ChunkPayload
    {
        virtual ~ChunkPayload() = default;
    };

    // Game-specific half of the streamer.
    class IChunkHandler
    {
    public:
        virtual ~IChunkHandler() = default;

        /* WORKER THREAD. Read and decode everything that does not need the main thread / GPU.
         Poll canceled between expensive steps and return early if set.
         Return nullptr if the chunk has no content (it is then remembered as empty, no Commit/Unload).
         Throwing marks the chunk Failed (not retried until it leaves and re-enters range).
         Must be thread-safe: several chunks load concurrently.
         */
        virtual std::unique_ptr<ChunkPayload> Load(ChunkCoord coord, const std::atomic<bool> &cancelled) = 0;

        /// MAIN THREAD. Create entities, upload to the GPU, register colliders...
        virtual void Commit(ChunkCoord coord, std::unique_ptr<ChunkPayload> payload) = 0;

        /// MAIN THREAD. Destroy whatever Commit created. Only called for chunks that were committed.
        virtual void Unload(ChunkCoord coord) = 0;
    };

    struct StreamFocus
    {
        float x = 0.0f; // world-space position on the chunk grid plane (camera, player, ...)
        float y = 0.0f;
    };

    struct StreamingSettings
    {
        float chunkSize             = 64.0f; // world units per chunk edge
        int32_t loadRadius          = 6;     // chunks (circular) around each focus that should be resident
        int32_t unloadRadius        = 8; // chunks beyond which resident chunks are evicted (>= loadRadius, hysteresis)
        uint32_t maxInFlight        = 0; // concurrent loads; 0 = max(1, pool threads / 2) so other jobs aren't starved
        double commitBudgetMs       = 2.0; // main-thread time per Update() spent in IChunkHandler::Commit
        uint32_t minCommitsPerFrame = 1;   // always make progress even if one Commit exceeds the budget
    };

    /**
     * @brief Distance-prioritised, budgeted streaming of grid chunks on top of ThreadPool.
     *
     * All public methods must be called from one thread (the main/update thread).
     * Pipeline per chunk: Pending -> Loading (worker) -> Loaded (awaiting commit) -> Resident -> evicted.
     *
     * Requests are kept in a distance-sorted list here and only `maxInFlight` are handed to the (FIFO)
     * pool at a time, so priorities are re-evaluated every time the focus changes chunk.
     */
    class StreamingManager
    {
    public:
        struct Stats
        {
            size_t loading        = 0;
            size_t awaitingCommit = 0;
            size_t resident       = 0;
            size_t empty          = 0;
            size_t failed         = 0;
            size_t pending        = 0;
        };

        StreamingManager(ThreadPool &pool, IChunkHandler &handler, const StreamingSettings &settings = {});

        /// Cancels outstanding loads and waits for running tasks. Does NOT call Unload; use UnloadAll() first.
        ~StreamingManager();

        StreamingManager(const StreamingManager &)            = delete;
        StreamingManager &operator=(const StreamingManager &) = delete;

        /// Call once per frame.
        void Update(std::span<const StreamFocus> foci);

        /// Block until everything within loadRadius is loaded and committed (loading screens, teleports).
        void LoadBlocking(std::span<const StreamFocus> foci);

        /// Unload every committed chunk and forget all state.
        void UnloadAll();

        [[nodiscard]] bool IsResident(ChunkCoord coord) const;
        [[nodiscard]] bool IsSettled() const;
        [[nodiscard]] Stats GetStats() const;
        [[nodiscard]] const StreamingSettings &GetSettings() const noexcept { return m_settings; }

    private:
        enum class State : uint8_t
        {
            Loading,
            Loaded,
            Resident,
            Empty,
            Failed
        };

        struct Entry
        {
            State state        = State::Loading;
            uint64_t requestId = 0;
            std::shared_ptr<std::atomic<bool>> cancel;
        };

        struct Completed
        {
            ChunkCoord coord;
            uint64_t requestId = 0;
            std::unique_ptr<ChunkPayload> payload;
            bool failed = false;
        };

        // Everything worker tasks touch lives here so tasks never reference the manager itself.
        struct Shared
        {
            std::mutex mutex;
            std::condition_variable cv;
            std::vector<Completed> done;
            std::atomic<uint32_t> running{0};
        };

        struct Pending
        {
            ChunkCoord coord;
            int64_t dist2;
        };

        struct Ready
        {
            ChunkCoord coord;
            uint64_t requestId;
            std::unique_ptr<ChunkPayload> payload;
        };

        void UpdateImpl(std::span<const StreamFocus> foci, bool unlimitedBudget);
        void Rebuild(const std::vector<ChunkCoord> &cells);
        void DrainCompletions();
        void PumpRequests();
        void RequestLoad(ChunkCoord coord);
        void CommitReady(bool unlimitedBudget);
        void EvictEntry(ChunkCoord coord, const Entry &entry) const;
        [[nodiscard]] ChunkCoord ToChunk(const StreamFocus &f) const;

        ThreadPool &m_pool;
        IChunkHandler &m_handler;
        StreamingSettings m_settings;
        uint32_t m_maxInFlight = 1;

        std::shared_ptr<Shared> m_shared;

        std::unordered_map<ChunkCoord, Entry> m_entries;
        std::vector<Pending> m_pending; // sorted so the nearest chunk is at the back
        std::deque<Ready> m_commitQueue;
        std::vector<ChunkCoord> m_lastCells;

        size_t m_inFlight        = 0;
        uint64_t m_nextRequestId = 0;
    };
} // namespace SF::Engine
