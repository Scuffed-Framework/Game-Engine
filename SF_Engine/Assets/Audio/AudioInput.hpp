#pragma once

#include <Assets/AssetTraits.hpp>
#include <LowLevel/FileSystem/File.hpp>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace SF::Engine::AudioInput
{
    /**
     * @brief An InputDataVariant resolved into something a decoder can open.
     *
     * Either a path on disk (File*) or a non-owning memory view (span). The memory view must stay alive for
     * the duration of the Load() call; decoders copy everything they need into PCM before returning.
     */
    struct Source
    {
        bool fromFile = false;
        std::string path;                // valid when fromFile
        std::span<const uint8_t> memory; // valid when !fromFile
        std::string extension;           // file extension, or sniffed from the magic bytes for memory

        [[nodiscard]] bool IsFile() const { return fromFile; }
        [[nodiscard]] std::string Describe() const { return fromFile ? path : "<memory>"; }
    };

    /** True if the Ogg stream starts with an Opus identification header (vs. Vorbis). */
    inline bool LooksLikeOggOpus(const Source &src)
    {
        char buf[128] = {};
        size_t n;
        if (src.IsFile())
        {
            std::ifstream f(src.path, std::ios::binary);
            f.read(buf, sizeof(buf));
            n = static_cast<size_t>(f.gcount());
        } else
        {
            n = std::min(sizeof(buf), src.memory.size());
            std::memcpy(buf, src.memory.data(), n);
        }
        return std::string_view(buf, n).find("OpusHead") != std::string_view::npos;
    }

    /** Guesses the container from the magic bytes. Returns the registry extension, or "" if unknown. */
    inline std::string SniffExtension(std::span<const uint8_t> d)
    {
        const auto starts = [&](std::string_view m, size_t off = 0)
        { return d.size() >= off + m.size() && std::memcmp(d.data() + off, m.data(), m.size()) == 0; };

        if (starts("RIFF") && starts("WAVE", 8))
            return ".wav";
        if (starts("fLaC"))
            return ".flac";
        if (starts("OggS"))
        {
            Source tmp;
            tmp.memory = d;
            return LooksLikeOggOpus(tmp) ? ".opus" : ".ogg";
        }
        if (d.size() >= 4 && d[0] == 0x1A && d[1] == 0x45 && d[2] == 0xDF && d[3] == 0xA3)
            return ".mka";
        if (starts("ID3") || (d.size() >= 2 && d[0] == 0xFF && (d[1] & 0xE0) == 0xE0))
            return ".mp3";
        return {};
    }

    /** Resolves the variant, logging and returning nullopt if it can't be decoded. */
    inline std::optional<Source> Resolve(const InputDataVariant &in)
    {
        Source s;

        if (const auto *f = std::get_if<File *>(&in))
        {
            File *file = *f;
            if (file == nullptr || !file->Exists())
            {
                std::cerr << "Cannot load sound buffer: file is null or missing" << std::endl;
                return std::nullopt;
            }
            s.fromFile  = true;
            s.path      = std::filesystem::path(file->GetFullPath()).string();
            s.extension = std::string(file->GetExtension());
            return s;
        }

        if (const auto *m = std::get_if<std::span<const uint8_t>>(&in))
        {
            if (m->empty())
            {
                std::cerr << "Cannot load sound buffer: memory span is empty" << std::endl;
                return std::nullopt;
            }
            s.memory    = *m;
            s.extension = SniffExtension(*m);
            return s;
        }

        std::cerr << "Cannot load sound buffer from void*: no size information, use a std::span<const uint8_t>"
                  << std::endl;
        return std::nullopt;
    }
} // namespace SF::Engine::AudioInput
