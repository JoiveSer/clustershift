#pragma once

#include <windows.h>
#include <utility>

namespace clustershift::core {

class ScopedHandle {
public:
    explicit ScopedHandle(HANDLE handle = INVALID_HANDLE_VALUE) noexcept : handle_(handle) {}
    ~ScopedHandle() noexcept { reset(); }

    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;

    ScopedHandle(ScopedHandle&& other) noexcept : handle_(other.handle_) {
        other.handle_ = INVALID_HANDLE_VALUE;
    }

    ScopedHandle& operator=(ScopedHandle&& other) noexcept {
        if (this != &other) {
            reset();
            handle_ = other.handle_;
            other.handle_ = INVALID_HANDLE_VALUE;
        }
        return *this;
    }

    [[nodiscard]] bool is_valid() const noexcept {
        return handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr;
    }

    [[nodiscard]] HANDLE get() const noexcept { 
        return handle_; 
    }

    void reset(HANDLE new_handle = INVALID_HANDLE_VALUE) noexcept {
        if (is_valid()) {
            CloseHandle(handle_);
        }
        handle_ = new_handle;
    }

private:
    HANDLE handle_;
};

} // namespace clustershift::core
