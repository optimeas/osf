// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include "durable_file.hpp"

#include <cerrno>
#include <cstring>
#include <string>
#include <utility>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <unistd.h>
#endif

namespace osf::detail {

namespace {

#ifdef _WIN32
std::string last_win32_error_message(DWORD err) {
    LPSTR msg = nullptr;
    DWORD const n = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, 0, reinterpret_cast<LPSTR>(&msg), 0, nullptr);
    std::string result;
    if (n > 0 && msg) {
        result.assign(msg, n);
        // Trim trailing CR/LF that FormatMessage tends to append.
        while (!result.empty() &&
               (result.back() == '\r' || result.back() == '\n')) {
            result.pop_back();
        }
    } else {
        result = "Win32 error " + std::to_string(err);
    }
    if (msg) LocalFree(msg);
    return result;
}
#else
std::string last_errno_message(int err) {
    return std::string{std::strerror(err)};
}
#endif

Error io_error(std::string what) {
    return Error{Error::Code::IoError, std::move(what)};
}

}  // namespace

// ── create ─────────────────────────────────────────────────────────

Result<DurableFile> DurableFile::create(
        std::filesystem::path const& path) {
#ifdef _WIN32
    HANDLE const h = CreateFileW(
        path.c_str(), GENERIC_WRITE, /*shareMode=*/0, /*sa=*/nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, /*template=*/nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD const err = GetLastError();
        return tl::make_unexpected(io_error(
            "DurableFile::create: " + last_win32_error_message(err) +
            " (" + path.string() + ")"));
    }
    DurableFile f;
    f.handle_ = h;
    return f;
#else
    int const fd = ::open(path.c_str(),
                          O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        int const err = errno;
        return tl::make_unexpected(io_error(
            "DurableFile::create: " + last_errno_message(err) +
            " (" + path.string() + ")"));
    }
    DurableFile f;
    f.fd_ = fd;
    return f;
#endif
}

// ── move ───────────────────────────────────────────────────────────

DurableFile::DurableFile(DurableFile&& other) noexcept {
#ifdef _WIN32
    handle_ = other.handle_;
    other.handle_ = nullptr;
#else
    fd_ = other.fd_;
    other.fd_ = -1;
#endif
}

DurableFile& DurableFile::operator=(DurableFile&& other) noexcept {
    if (this != &other) {
        (void) close();   // best-effort close of any prior handle
#ifdef _WIN32
        handle_ = other.handle_;
        other.handle_ = nullptr;
#else
        fd_ = other.fd_;
        other.fd_ = -1;
#endif
    }
    return *this;
}

// ── destructor ─────────────────────────────────────────────────────

DurableFile::~DurableFile() {
    (void) close();
}

// ── is_open ────────────────────────────────────────────────────────

bool DurableFile::is_open() const noexcept {
#ifdef _WIN32
    return handle_ != nullptr;
#else
    return fd_ >= 0;
#endif
}

// ── write ──────────────────────────────────────────────────────────

Result<void> DurableFile::write(std::uint8_t const* data,
                                std::size_t size) {
    if (!is_open()) {
        return tl::make_unexpected(io_error(
            "DurableFile::write: file is closed"));
    }
    std::size_t written = 0;
    while (written < size) {
        std::size_t const remaining = size - written;
#ifdef _WIN32
        DWORD const chunk = static_cast<DWORD>(
            (remaining > 0x7FFFFFFF) ? 0x7FFFFFFF : remaining);
        DWORD wrote = 0;
        BOOL const ok = WriteFile(handle_, data + written, chunk,
                                  &wrote, nullptr);
        if (!ok) {
            DWORD const err = GetLastError();
            return tl::make_unexpected(io_error(
                "DurableFile::write: " + last_win32_error_message(err)));
        }
        if (wrote == 0) {
            return tl::make_unexpected(io_error(
                "DurableFile::write: WriteFile returned 0 bytes written"));
        }
        written += wrote;
#else
        ssize_t const n = ::write(fd_, data + written, remaining);
        if (n < 0) {
            if (errno == EINTR) continue;  // retry on signal interruption
            int const err = errno;
            return tl::make_unexpected(io_error(
                "DurableFile::write: " + last_errno_message(err)));
        }
        if (n == 0) {
            return tl::make_unexpected(io_error(
                "DurableFile::write: write(2) returned 0 bytes written"));
        }
        written += static_cast<std::size_t>(n);
#endif
    }
    return {};
}

// ── force ──────────────────────────────────────────────────────────

Result<void> DurableFile::force() {
    if (!is_open()) {
        return tl::make_unexpected(io_error(
            "DurableFile::force: file is closed"));
    }
#ifdef _WIN32
    if (!FlushFileBuffers(handle_)) {
        DWORD const err = GetLastError();
        return tl::make_unexpected(io_error(
            "DurableFile::force: " + last_win32_error_message(err)));
    }
#else
    if (::fsync(fd_) != 0) {
        int const err = errno;
        return tl::make_unexpected(io_error(
            "DurableFile::force: " + last_errno_message(err)));
    }
#endif
    return {};
}

// ── close ──────────────────────────────────────────────────────────

Result<void> DurableFile::close() {
#ifdef _WIN32
    if (handle_ == nullptr) return {};
    HANDLE const h = handle_;
    handle_ = nullptr;          // mark closed first; matches POSIX path
    if (!CloseHandle(h)) {
        DWORD const err = GetLastError();
        return tl::make_unexpected(io_error(
            "DurableFile::close: " + last_win32_error_message(err)));
    }
#else
    if (fd_ < 0) return {};
    int const fd = fd_;
    fd_ = -1;                    // mark closed first
    if (::close(fd) != 0) {
        int const err = errno;
        return tl::make_unexpected(io_error(
            "DurableFile::close: " + last_errno_message(err)));
    }
#endif
    return {};
}

}  // namespace osf::detail
