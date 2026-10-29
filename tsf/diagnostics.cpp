#include "diagnostics.h"
#include <vector>
#include <TraceLoggingDynamic.h>

namespace eime::diag {
namespace {
constexpr GUID provider_id = {0x5c0c84b5, 0x74c2, 0x4b35, {0x94, 0xaa, 0xb0, 0x15, 0x87, 0x21, 0xa9, 0x12}};
char module_anchor;
using Event = tld::Event<std::vector<BYTE>>;
void number(Event& event, const char* name, std::uint64_t value) {
    event.AddField(name, tld::TypeUInt64);
    event.AddValue(static_cast<UINT64>(value));
}
void text(Event& event, const char* name, const char* value) {
    event.AddField(name, tld::TypeUtf8String);
    event.AddString(value);
}
}
struct Trace::Impl {
    tld::Provider provider{"EIME.Diagnostics", provider_id};
    GUID service{};
    std::uint64_t sequence = 0, key = 0, edit = 0;
    std::uint64_t active_edit = 0, active_key = 0, generation = 0;
    bool announced = false;
    Impl() { CoCreateGuid(&service); }

    void common(Event& event, const char* kind, std::uint64_t edit_id, std::uint64_t gen) {
        text(event, "Kind", kind);
        number(event, "Schema", 1);
        event.AddField("Service", tld::TypeGuid);
        event.AddValue(service);
        number(event, "Sequence", ++sequence);
        number(event, "Key", edit_id != 0 && edit_id == active_edit ? active_key : key);
        number(event, "Edit", edit_id);
        number(event, "Generation", gen);
    }
    void module() {
        Event event("Module", 4, 1);
        common(event, "Module", 0, 0);
        text(event, "Revision", EIME_BUILD_REVISION);
        text(event, "Compiler", EIME_BUILD_COMPILER);
        HMODULE module = nullptr;
        wchar_t path[32768] = {};
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&module_anchor), &module)) GetModuleFileNameW(module, path, 32768);
        event.AddField("ModulePath", tld::TypeUtf16String);
        event.AddString(path);
        wchar_t host[32768] = {};
        GetModuleFileNameW(nullptr, host, 32768);
        event.AddField("HostPath", tld::TypeUtf16String);
        event.AddString(host);
        event.Write(provider);
        announced = true;
    }
};
Trace::Trace() noexcept {
    try { impl_ = std::make_unique<Impl>(); } catch (...) {}
}
Trace::~Trace() = default;
void Trace::write(const char* name, std::uint64_t edit, std::uint64_t generation,
    std::uint64_t value, HRESULT result, const char* operation) noexcept {
    if (!impl_) return;
    const UCHAR level = FAILED(result) ? 2 : 4;
    if (!impl_->provider.IsEnabled(level, 1)) { impl_->announced = false; return; }
    try {
        if (!impl_->announced) impl_->module();
        Event event(name, level, 1);
        impl_->common(event, name, edit, generation);
        number(event, "Value", value);
        // UInt32 preserves the exact HRESULT regardless of decoder localization.
        event.AddField("Result", tld::TypeUInt32);
        event.AddValue(static_cast<UINT32>(result));
        text(event, "Operation", operation);
        event.Write(impl_->provider);
    } catch (...) {} // Diagnostic failure must never change typing behavior.
}
void Trace::key(unsigned flags) noexcept {
    if (impl_) ++impl_->key;
    write("Key", 0, 0, flags);
}
std::uint64_t Trace::key_id() const noexcept { return impl_ ? impl_->key : 0; }
std::uint64_t Trace::next_edit() noexcept { return impl_ ? ++impl_->edit : 0; }
void Trace::begin(std::uint64_t edit, std::uint64_t key, std::uint64_t generation) noexcept {
    if (impl_) { impl_->active_edit = edit; impl_->active_key = key; impl_->generation = generation; }
    write("EditBegin", edit, generation);
}
void Trace::end() noexcept {
    if (impl_) {
        write("EditEnd", impl_->active_edit, impl_->generation);
        impl_->active_edit = 0;
    }
}
void Trace::snapshot(std::uint64_t edit, std::uint64_t generation, std::uint64_t caret,
    std::uint64_t preview_bytes, std::uint64_t commit_bytes) noexcept {
    if (!impl_ || !impl_->provider.IsEnabled(4, 1)) return;
    try {
        if (!impl_->announced) impl_->module();
        Event event("Snapshot", 4, 1);
        impl_->common(event, "Snapshot", edit, generation);
        number(event, "CaretUtf16", caret);
        number(event, "PreviewBytes", preview_bytes);
        number(event, "CommitBytes", commit_bytes);
        event.Write(impl_->provider);
    } catch (...) {}
}
HRESULT Trace::host_call(const char* operation, HRESULT result) noexcept {
    if (impl_) write("HostCall", impl_->active_edit, impl_->generation, 0, result, operation);
    return result;
}
}
