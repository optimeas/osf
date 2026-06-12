// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

/**
 * @file durablefile_p.h
 * @brief Private RAII wrapper for append-only writes with OS-level fsync.
 *
 * Used by StreamingWriter and reserved for future writers that need
 * durable per-block flush semantics. Two compile-time
 * implementations: POSIX (open / write / fsync / close) and Windows
 * (CreateFileW / WriteFile / FlushFileBuffers / CloseHandle). Selected
 * via #ifdef on _WIN32.
 *
 * Performance note: FlushFileBuffers on Windows is typically 10x
 * slower than POSIX fsync on equivalent hardware. This is OS-caching
 * behaviour, not a library defect. Embedded use cases that target both
 * platforms should size their fsync budget against the slower path.
 *
 * Move-only. The destructor is best-effort close — errors are
 * silently ignored; callers wanting close-error detection must call
 * close() explicitly before destruction.
 */

#ifndef OSF_DETAIL_DURABLE_FILE_HPP
#define OSF_DETAIL_DURABLE_FILE_HPP

#include "osf/error.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace osf::detail {

class DurableFile {
public:
    /// Open the file at `path` for append-only writing, creating it if
    /// needed and truncating any existing content. Returns the
    /// constructed DurableFile or an IoError describing why open failed.
    [[nodiscard]] static Result<DurableFile> create(
        std::filesystem::path const& path);

    // Non-copyable; move-only.
    DurableFile(DurableFile const&) = delete;
    DurableFile& operator=(DurableFile const&) = delete;
    DurableFile(DurableFile&& other) noexcept;
    DurableFile& operator=(DurableFile&& other) noexcept;

    /// Closes the file if still open. Errors during destruction are
    /// silently ignored — callers wanting to detect close errors must
    /// call close() explicitly before destruction.
    ~DurableFile();

    /// Append `size` bytes from `data` to the file.
    /// Returns IoError on partial-write-after-retry or system error.
    [[nodiscard]] Result<void> write(std::uint8_t const* data,
                                     std::size_t size);

    /// fsync / FlushFileBuffers — durably commits all previous writes
    /// to non-volatile storage. Required after each completed block in
    /// the StreamingWriter use case. Returns IoError on system error.
    [[nodiscard]] Result<void> force();

    /// Explicitly close the file. Idempotent (second call is a no-op
    /// success). Returns IoError if the underlying close call fails.
    [[nodiscard]] Result<void> close();

    /// True if the file is currently open.
    [[nodiscard]] bool is_open() const noexcept;

private:
#ifdef _WIN32
    void* handle_ = nullptr;   // HANDLE; nullptr means closed
#else
    int fd_ = -1;              // POSIX file descriptor; -1 means closed
#endif

    DurableFile() = default;   // create() is the only public factory
};

}  // namespace osf::detail

#endif  // OSF_DETAIL_DURABLE_FILE_HPP
