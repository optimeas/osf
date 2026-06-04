// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include "osf/compression.hpp"

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
// detect_compression — non-consuming peek
// =====================================================================

CompressionFormat detect_compression(std::istream& source) {
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
        : src_(source), in_(kBufferSize), out_(kBufferSize) {
        // Prime the input buffer and classify from the leading bytes.
        src_.read(in_.data(), static_cast<std::streamsize>(in_.size()));
        auto const n = static_cast<std::size_t>(src_.gcount());
        in_avail_ = n;
        in_pos_ = 0;

        if (n >= 2) {
            format_ = classify(static_cast<std::uint8_t>(
                                   static_cast<unsigned char>(in_[0])),
                               static_cast<std::uint8_t>(
                                   static_cast<unsigned char>(in_[1])));
        }

        if (format_ != CompressionFormat::None) {
            zs_.zalloc = Z_NULL;
            zs_.zfree = Z_NULL;
            zs_.opaque = Z_NULL;
            zs_.next_in = Z_NULL;
            zs_.avail_in = 0;
            // MAX_WBITS | 32 → automatic zlib / gzip header detection.
            if (inflateInit2(&zs_, MAX_WBITS | 32) == Z_OK) {
                inflate_active_ = true;
                zs_.next_in = reinterpret_cast<Bytef*>(in_.data());
                zs_.avail_in = static_cast<uInt>(n);
            } else {
                // Initialisation failure (effectively only ENOMEM) —
                // degrade to passing the raw bytes through.
                format_ = CompressionFormat::None;
            }
        }
    }

    ~Streambuf() override {
        if (inflate_active_) {
            inflateEnd(&zs_);
        }
    }

    Streambuf(Streambuf const&) = delete;
    Streambuf& operator=(Streambuf const&) = delete;

    [[nodiscard]] CompressionFormat format() const noexcept { return format_; }

protected:
    int_type underflow() override {
        if (gptr() < egptr()) {
            return traits_type::to_int_type(*gptr());
        }
        return (format_ == CompressionFormat::None) ? underflow_plain()
                                                    : underflow_inflate();
    }

private:
    // Pass-through: serve the raw input buffer, refilling from the source.
    int_type underflow_plain() {
        if (in_pos_ >= in_avail_) {
            src_.read(in_.data(), static_cast<std::streamsize>(in_.size()));
            in_avail_ = static_cast<std::size_t>(src_.gcount());
            in_pos_ = 0;
            if (in_avail_ == 0) {
                return traits_type::eof();
            }
        }
        char* const base = in_.data();
        setg(base, base + in_pos_, base + in_avail_);
        in_pos_ = in_avail_;
        return traits_type::to_int_type(*gptr());
    }

    // Streaming inflate into the output buffer.
    int_type underflow_inflate() {
        if (stream_end_) {
            return traits_type::eof();
        }
        for (;;) {
            if (zs_.avail_in == 0 && !source_exhausted_) {
                src_.read(in_.data(),
                          static_cast<std::streamsize>(in_.size()));
                auto const n = static_cast<std::size_t>(src_.gcount());
                if (n == 0) {
                    source_exhausted_ = true;
                } else {
                    zs_.next_in = reinterpret_cast<Bytef*>(in_.data());
                    zs_.avail_in = static_cast<uInt>(n);
                }
            }

            zs_.next_out = reinterpret_cast<Bytef*>(out_.data());
            zs_.avail_out = static_cast<uInt>(out_.size());
            int const ret = inflate(&zs_, Z_NO_FLUSH);
            std::size_t const produced = out_.size() - zs_.avail_out;

            if (ret == Z_STREAM_END) {
                stream_end_ = true;
            }

            if (produced > 0) {
                char* const base = out_.data();
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
                if (zs_.avail_in == 0 && source_exhausted_) {
                    return traits_type::eof();
                }
                // Z_BUF_ERROR with input still available means no forward
                // progress is possible — bail rather than spin.
                if (ret == Z_BUF_ERROR && zs_.avail_in != 0) {
                    return traits_type::eof();
                }
                continue;
            }
            // Z_DATA_ERROR / Z_NEED_DICT / Z_MEM_ERROR — corrupt stream.
            // Best-effort EOF; surfaces the blocks decoded so far.
            return traits_type::eof();
        }
    }

    std::istream& src_;
    CompressionFormat format_ = CompressionFormat::None;

    std::vector<char> in_;
    std::vector<char> out_;
    std::size_t in_avail_ = 0;   // bytes held in in_ (plain path)
    std::size_t in_pos_ = 0;     // consumed offset within in_ (plain path)

    z_stream zs_{};
    bool inflate_active_ = false;
    bool stream_end_ = false;
    bool source_exhausted_ = false;
};

// =====================================================================
// DecompressingIStream
// =====================================================================

DecompressingIStream::DecompressingIStream(std::istream& source)
    : std::istream(nullptr), buf_(std::make_unique<Streambuf>(source)) {
    this->rdbuf(buf_.get());
}

DecompressingIStream::~DecompressingIStream() = default;

CompressionFormat DecompressingIStream::format() const noexcept {
    return buf_->format();
}

bool DecompressingIStream::is_compressed() const noexcept {
    return buf_->format() != CompressionFormat::None;
}

}  // namespace osf
