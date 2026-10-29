#pragma once

// Entire call sites, including their arguments, disappear from product builds.
#ifdef EIME_ENABLE_DIAGNOSTICS
#include <windows.h>
#include <cstdint>
#include <memory>

namespace eime::diag {
class Trace {
public:
    Trace() noexcept;
    ~Trace();
    Trace(const Trace&) = delete;
    Trace& operator=(const Trace&) = delete;
    void key(unsigned flags) noexcept;
    std::uint64_t key_id() const noexcept;
    std::uint64_t next_edit() noexcept;
    void write(const char* event, std::uint64_t edit, std::uint64_t generation,
        std::uint64_t value = 0, HRESULT result = S_OK, const char* operation = "") noexcept;
    void begin(std::uint64_t edit, std::uint64_t key, std::uint64_t generation) noexcept;
    void snapshot(std::uint64_t edit, std::uint64_t generation, std::uint64_t caret,
        std::uint64_t preview_bytes, std::uint64_t commit_bytes) noexcept;
    void end() noexcept;
    HRESULT host_call(const char* operation, HRESULT result) noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
#define EIME_TRACE(...) do { __VA_ARGS__; } while (false)
#define EIME_HOST_CALL(operation, expression) trace_.host_call(operation, (expression))
#else
#define EIME_TRACE(...) do {} while (false)
#define EIME_HOST_CALL(operation, expression) (expression)
#endif
