// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include "osf/compression.h"

#include <zlib.h>

#include <cstddef>
#include <cstdint>
#include <ios>
#include <streambuf>
#include <vector>

namespace osf {

namespace {

constexpr std::size_t kBufferSize = 64u * 1024u;

// Classify a two-byte header. Mirrors the Rust compression module.
CompressionFormat classify(std::uint8_t b0, std::uint8_t b1) noexcept {
    if (b0 == 0x1F && b1 == 0x8B) {
        return CompressionFormat::Gzip;
    }
    if (b0 == 0x78 &&
        (b1 == 0x01 || b1 == 0x5E || b1 == 0x9C || b1 == 0xDA)) {
        return CompressionFormat::Zlib;
    }
    return CompressionFormat::None;
}

}  // namespace

// =====================================================================
// detectCompression — non-consuming peek
// =====================================================================

CompressionFormat detectCompression(std::istream& source) {
    std::istream::pos_type const start = source.tellg();
    char head[2] = {0, 0};
    source.read(head, 2);
    auto const got = source.gcount();
    // Restore the read position; the short read may have set eof/fail.
    source.clear();
    if (start != std::istream::pos_type(-1)) {
        source.seekg(start);
    }
    if (got < 2) {
        return CompressionFormat::None;
    }
    return classify(static_cast<std::uint8_t>(head[0]),
                    static_cast<std::uint8_t>(head[1]));
}

// =====================================================================
// DecompressingIStream::Streambuf
// =====================================================================

class DecompressingIStream::Streambuf final : public std::streambuf {
public:
    explicit Streambuf(std::istream& source)
        : m_src(source), m_in(kBufferSize), m_out(kBufferSize) {
        // Prime the input buffer and classify from the leading bytes.
        m_src.read(m_in.data(), static_cast<std::streamsize>(m_in.size()));
        auto const n = static_cast<std::size_t>(m_src.gcount());
        m_inAvail = n;
        m_inPos = 0;

        if (n >= 2) {
            m_format = classify(static_cast<std::uint8_t>(
                                   static_cast<unsigned char>(m_in[0])),
                               static_cast<std::uint8_t>(
                                   static_cast<unsigned char>(m_in[1])));
        }

        if (m_format != CompressionFormat::None) {
            m_zs.zalloc = Z_NULL;
            m_zs.zfree = Z_NULL;
            m_zs.opaque = Z_NULL;
            m_zs.next_in = Z_NULL;
            m_zs.avail_in = 0;
            // MAX_WBITS | 32 → automatic zlib / gzip header detection.
            if (inflateInit2(&m_zs, MAX_WBITS | 32) == Z_OK) {
                m_inflateActive = true;
                m_zs.next_in = reinterpret_cast<Bytef*>(m_in.data());
                m_zs.avail_in = static_cast<uInt>(n);
            } else {
                // Initialisation failure (effectively only ENOMEM) —
                // degrade to passing the raw bytes through.
                m_format = CompressionFormat::None;
            }
        }
    }

    ~Streambuf() override {
        if (m_inflateActive) {
            inflateEnd(&m_zs);
        }
    }

    Streambuf(Streambuf const&) = delete;
    Streambuf& operator=(Streambuf const&) = delete;

    [[nodiscard]] CompressionFormat format() const noexcept { return m_format; }

protected:
    int_type underflow() override {
        if (gptr() < egptr()) {
            return traits_type::to_int_type(*gptr());
        }
        return (m_format == CompressionFormat::None) ? underflow_plain()
                                                    : underflow_inflate();
    }

private:
    // Pass-through: serve the raw input buffer, refilling from the source.
    int_type underflow_plain() {
        if (m_inPos >= m_inAvail) {
            m_src.read(m_in.data(), static_cast<std::streamsize>(m_in.size()));
            m_inAvail = static_cast<std::size_t>(m_src.gcount());
            m_inPos = 0;
            if (m_inAvail == 0) {
                return traits_type::eof();
            }
        }
        char* const base = m_in.data();
        setg(base, base + m_inPos, base + m_inAvail);
        m_inPos = m_inAvail;
        return traits_type::to_int_type(*gptr());
    }

    // Streaming inflate into the output buffer.
    int_type underflow_inflate() {
        if (m_streamEnd) {
            return traits_type::eof();
        }
        for (;;) {
            if (m_zs.avail_in == 0 && !m_sourceExhausted) {
                m_src.read(m_in.data(),
                          static_cast<std::streamsize>(m_in.size()));
                auto const n = static_cast<std::size_t>(m_src.gcount());
                if (n == 0) {
                    m_sourceExhausted = true;
                } else {
                    m_zs.next_in = reinterpret_cast<Bytef*>(m_in.data());
                    m_zs.avail_in = static_cast<uInt>(n);
                }
            }

            m_zs.next_out = reinterpret_cast<Bytef*>(m_out.data());
            m_zs.avail_out = static_cast<uInt>(m_out.size());
            int const ret = inflate(&m_zs, Z_NO_FLUSH);
            std::size_t const produced = m_out.size() - m_zs.avail_out;

            if (ret == Z_STREAM_END) {
                m_streamEnd = true;
            }

            if (produced > 0) {
                char* const base = m_out.data();
                setg(base, base, base + produced);
                return traits_type::to_int_type(*gptr());
            }

            // No output this round.
            if (ret == Z_STREAM_END) {
                return traits_type::eof();
            }
            if (ret == Z_OK || ret == Z_BUF_ERROR) {
                // Need more input. If the source is drained, the stream
                // was truncated — best-effort EOF (the reader is already
                // best-effort at a truncated trailing block).
                if (m_zs.avail_in == 0 && m_sourceExhausted) {
                    return traits_type::eof();
                }
                // Z_BUF_ERROR with input still available means no forward
                // progress is possible — bail rather than spin.
                if (ret == Z_BUF_ERROR && m_zs.avail_in != 0) {
                    return traits_type::eof();
                }
                continue;
            }
            // Z_DATA_ERROR / Z_NEED_DICT / Z_MEM_ERROR — corrupt stream.
            // Best-effort EOF; surfaces the blocks decoded so far.
            return traits_type::eof();
        }
    }

    std::istream& m_src;
    CompressionFormat m_format = CompressionFormat::None;

    std::vector<char> m_in;
    std::vector<char> m_out;
    std::size_t m_inAvail = 0;   // bytes held in m_in (plain path)
    std::size_t m_inPos = 0;     // consumed offset within m_in (plain path)

    z_stream m_zs{};
    bool m_inflateActive = false;
    bool m_streamEnd = false;
    bool m_sourceExhausted = false;
};

// =====================================================================
// DecompressingIStream
// =====================================================================

DecompressingIStream::DecompressingIStream(std::istream& source)
    : std::istream(nullptr), m_buf(std::make_unique<Streambuf>(source)) {
    this->rdbuf(m_buf.get());
}

DecompressingIStream::~DecompressingIStream() = default;

CompressionFormat DecompressingIStream::format() const noexcept {
    return m_buf->format();
}

bool DecompressingIStream::isCompressed() const noexcept {
    return m_buf->format() != CompressionFormat::None;
}

}  // namespace osf
