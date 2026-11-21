#pragma once

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include "ChunkStreaming.hpp" // ChunkCoord

namespace SF::Engine
{
    static_assert(std::endian::native == std::endian::little, "Chunk packs are stored little-endian");

    namespace ChunkPackFormat
    {
        inline constexpr char Magic[4]    = {'S', 'F', 'C', 'P'};
        inline constexpr uint32_t Version = 1;

        struct Header
        {
            char magic[4];
            uint32_t version;
            uint32_t chunkCount;
            uint32_t reserved;
            uint64_t indexOffset;
            uint64_t fileSize;
        };

        struct IndexEntry
        {
            int32_t x;
            int32_t y;
            uint64_t offset; // absolute file offset of the blob
            uint32_t size;
            uint32_t reserved; // e.g. uncompressed size / CRC later
        };

        static_assert(sizeof(Header) == 32);
        static_assert(sizeof(IndexEntry) == 24);

        inline bool Less(const IndexEntry &a, const IndexEntry &b) { return a.x != b.x ? a.x < b.x : a.y < b.y; }
    } // namespace ChunkPackFormat

    /// Thread-safe positional read access to a blob of bytes.
    class IByteSource
    {
    public:
        virtual ~IByteSource()                                                         = default;
        [[nodiscard]] virtual uint64_t Size() const                                    = 0;
        [[nodiscard]] virtual bool Read(uint64_t offset, void *dst, size_t size) const = 0;
    };

    /// Portable default: one ifstream guarded by a mutex (serialises I/O; fine for HDD/mobile flash).
    /// For NVMe-heavy workloads swap in pread()/mmap based sources behind the same interface.
    class StreamByteSource final : public IByteSource
    {
    public:
        static std::unique_ptr<StreamByteSource> Open(const std::filesystem::path &path)
        {
            auto src = std::unique_ptr<StreamByteSource>(new StreamByteSource());
            src->m_stream.open(path, std::ios::binary);
            if (!src->m_stream)
                return nullptr;
            src->m_stream.seekg(0, std::ios::end);
            src->m_size = static_cast<uint64_t>(src->m_stream.tellg());
            src->m_stream.clear();
            return src;
        }

        uint64_t Size() const override { return m_size; }

        bool Read(const uint64_t offset, void *dst, const size_t size) const override
        {
            if (offset > m_size || size > m_size - offset)
                return false;
            if (size == 0)
                return true;

            std::lock_guard lock(m_mutex);
            m_stream.clear();
            m_stream.seekg(static_cast<std::streamoff>(offset));
            m_stream.read(static_cast<char *>(dst), static_cast<std::streamsize>(size));
            return m_stream && static_cast<size_t>(m_stream.gcount()) == size;
        }

    private:
        StreamByteSource() = default;

        mutable std::ifstream m_stream;
        mutable std::mutex m_mutex;
        uint64_t m_size = 0;
    };

    /// In-memory source (tests, embedded data).
    class MemoryByteSource final : public IByteSource
    {
    public:
        explicit MemoryByteSource(std::vector<uint8_t> data) : m_data(std::move(data)) {}

        uint64_t Size() const override { return m_data.size(); }

        bool Read(const uint64_t offset, void *dst, const size_t size) const override
        {
            if (offset > m_data.size() || size > m_data.size() - offset)
                return false;
            if (size)
                std::memcpy(dst, m_data.data() + offset, size);
            return true;
        }

    private:
        std::vector<uint8_t> m_data;
    };

    /// Streams chunks to "<path>.tmp" and atomically renames it to <path> in Finish().
    /// Abandoning the writer without Finish() deletes the temp file.
    class ChunkPackWriter
    {
    public:
        explicit ChunkPackWriter(std::filesystem::path path) : m_path(std::move(path)), m_tmp(m_path)
        {
            m_tmp += ".tmp";
            m_out.open(m_tmp, std::ios::binary | std::ios::trunc);
            if (!m_out)
                throw std::runtime_error("Cannot create chunk pack: " + m_tmp.string());

            constexpr ChunkPackFormat::Header blank{};
            m_out.write(reinterpret_cast<const char *>(&blank), sizeof blank);
            m_cursor = sizeof blank;
        }

        ~ChunkPackWriter()
        {
            if (!m_finished)
            {
                m_out.close();
                std::error_code ec;
                std::filesystem::remove(m_tmp, ec);
            }
        }

        ChunkPackWriter(const ChunkPackWriter &)            = delete;
        ChunkPackWriter &operator=(const ChunkPackWriter &) = delete;

        /// Returns false if this coordinate was already added.
        bool AddChunk(const ChunkCoord coord, const std::span<const uint8_t> data)
        {
            if (m_finished)
                throw std::logic_error("ChunkPackWriter already finished");
            if (data.size() > UINT32_MAX)
                throw std::length_error("Chunk larger than 4 GiB");
            if (!m_seen.insert(coord).second)
                return false;

            m_out.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
            if (!m_out)
                throw std::runtime_error("Write failed: " + m_tmp.string());

            m_index.push_back({.x        = coord.x,
                               .y        = coord.y,
                               .offset   = m_cursor,
                               .size     = static_cast<uint32_t>(data.size()),
                               .reserved = 0});
            m_cursor += data.size();
            return true;
        }

        void Finish()
        {
            using namespace ChunkPackFormat;
            if (m_finished)
                return;

            std::ranges::sort(m_index, Less);

            const uint64_t indexOffset = m_cursor;
            m_out.write(reinterpret_cast<const char *>(m_index.data()),
                        static_cast<std::streamsize>(m_index.size() * sizeof(IndexEntry)));

            Header h{};
            std::memcpy(h.magic, Magic, sizeof h.magic);
            h.version     = Version;
            h.chunkCount  = static_cast<uint32_t>(m_index.size());
            h.indexOffset = indexOffset;
            h.fileSize    = indexOffset + m_index.size() * sizeof(IndexEntry);

            m_out.seekp(0);
            m_out.write(reinterpret_cast<const char *>(&h), sizeof h);
            m_out.flush();
            if (!m_out)
                throw std::runtime_error("Write failed: " + m_tmp.string());
            m_out.close();

            std::filesystem::rename(m_tmp, m_path);
            m_finished = true;
        }

    private:
        std::filesystem::path m_path;
        std::filesystem::path m_tmp;
        std::ofstream m_out;
        std::vector<ChunkPackFormat::IndexEntry> m_index;
        std::unordered_set<ChunkCoord> m_seen;
        uint64_t m_cursor = 0;
        bool m_finished   = false;
    };

    /// Immutable after Open(), so every method is safe to call from many threads at once.
    /// Treats the file as untrusted: all offsets/sizes are bounds-checked up front.
    class ChunkPackReader
    {
    public:
        static std::unique_ptr<ChunkPackReader> Open(std::unique_ptr<IByteSource> source, std::string *error = nullptr)
        {
            using namespace ChunkPackFormat;

            auto fail = [&](const char *msg)
            {
                if (error)
                    *error = msg;
                return std::unique_ptr<ChunkPackReader>{};
            };

            if (!source)
                return fail("no source");

            const uint64_t size = source->Size();
            if (size < sizeof(Header))
                return fail("file too small");

            Header h{};
            if (!source->Read(0, &h, sizeof h))
                return fail("cannot read header");
            if (std::memcmp(h.magic, Magic, sizeof h.magic) != 0)
                return fail("bad magic");
            if (h.version != Version)
                return fail("unsupported version");
            if (h.fileSize != size)
                return fail("size mismatch (truncated or corrupt)");
            if (h.indexOffset < sizeof(Header) || h.indexOffset > size)
                return fail("bad index offset");
            if (h.chunkCount > (size - h.indexOffset) / sizeof(IndexEntry))
                return fail("index exceeds file");

            std::vector<IndexEntry> index(h.chunkCount);
            if (!index.empty() && !source->Read(h.indexOffset, index.data(), index.size() * sizeof(IndexEntry)))
                return fail("cannot read index");

            for (size_t i = 0; i < index.size(); ++i)
            {
                const IndexEntry &e = index[i];
                if (i > 0 && !Less(index[i - 1], e))
                    return fail("index not strictly sorted");
                if (e.offset < sizeof(Header) || e.offset > h.indexOffset || e.size > h.indexOffset - e.offset)
                    return fail("chunk out of bounds");
            }

            return std::unique_ptr<ChunkPackReader>(new ChunkPackReader(std::move(source), std::move(index)));
        }

        [[nodiscard]] bool Has(const ChunkCoord c) const { return Find(c) != nullptr; }
        [[nodiscard]] uint32_t GetChunkCount() const { return static_cast<uint32_t>(m_index.size()); }
        [[nodiscard]] std::span<const ChunkPackFormat::IndexEntry> GetIndex() const { return m_index; }

        /// Returns false if the chunk isn't in the pack or the read failed.
        bool Read(const ChunkCoord c, std::vector<uint8_t> &out) const
        {
            const auto *e = Find(c);
            if (!e)
                return false;
            out.resize(e->size);
            return out.empty() || m_source->Read(e->offset, out.data(), out.size());
        }

    private:
        ChunkPackReader(std::unique_ptr<IByteSource> source, std::vector<ChunkPackFormat::IndexEntry> index) :
            m_source(std::move(source)), m_index(std::move(index))
        {
        }

        const ChunkPackFormat::IndexEntry *Find(const ChunkCoord c) const
        {
            const ChunkPackFormat::IndexEntry key{.x = c.x, .y = c.y, .offset = 0, .size = 0, .reserved = 0};
            const auto it = std::ranges::lower_bound(m_index, key, ChunkPackFormat::Less);
            if (it == m_index.end() || it->x != c.x || it->y != c.y)
                return nullptr;
            return &*it;
        }

        std::unique_ptr<IByteSource> m_source;
        std::vector<ChunkPackFormat::IndexEntry> m_index;
    };
} // namespace SF::Engine
