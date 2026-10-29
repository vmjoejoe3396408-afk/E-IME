#include <windows.h>
#include <msctf.h>
#include <iostream>
#include "../tsf/guids.h"
#include "../testbench/mock_tsf.h"
#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL line " << __LINE__ << ": " #x << '\n'; return 1; } } while (0)

int wmain(int argc, wchar_t** argv) {
    CHECK(argc == 2);
    CHECK(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)));
    HMODULE dll = LoadLibraryW(argv[1]);
    CHECK(dll);
    auto get = reinterpret_cast<HRESULT(WINAPI*)(REFCLSID, REFIID, void**)>(GetProcAddress(dll, "DllGetClassObject"));
    CHECK(get);
    IClassFactory* factory = nullptr;
    CHECK(SUCCEEDED(get(eime::CLSID_EIME_TextService, IID_IClassFactory, reinterpret_cast<void**>(&factory))));
    ITfTextInputProcessor* tip = nullptr;
    CHECK(SUCCEEDED(factory->CreateInstance(nullptr, IID_ITfTextInputProcessor, reinterpret_cast<void**>(&tip))));
    factory->Release();
    auto* manager = new eime::test::MockThreadMgr();
    auto* context = new eime::test::MockContext();
    CHECK(SUCCEEDED(tip->Activate(manager, 1001)));
    auto key = [&](WPARAM vk) {
        BYTE state[256] = {};
        SetKeyboardState(state);
        BOOL eaten = FALSE;
        if (FAILED(manager->key_sink()->OnTestKeyDown(context, vk, 0, &eaten)) || !eaten) return false;
        eaten = FALSE;
        return SUCCEEDED(manager->key_sink()->OnKeyDown(context, vk, 0, &eaten)) && eaten;
    };
    context->defer_edits = true;
    for (char c : std::string("SU3CL3")) CHECK(key(c));
    CHECK(key(VK_LEFT));
    CHECK(key(VK_OEM_PLUS));
    CHECK(key(VK_RETURN));
    context->flush_edits();
    CHECK(context->range()->current_text() == L"你=好");
    // The recorder must expose this genuine HRESULT from a host API call.
    context->defer_edits = false;
    context->range()->document()->set_text_result = E_ACCESSDENIED;
    CHECK(key(VK_OEM_PLUS));
    CHECK(context->range()->current_text() == L"你=好");
    context->range()->document()->set_text_result = S_OK;
    CHECK(key(VK_ESCAPE));
    CHECK(SUCCEEDED(tip->Deactivate()));
    tip->Release(); context->Release(); manager->Release();
    CHECK(FreeLibrary(dll));
    CoUninitialize();
    std::cout << "PASS: queued mixed input and injected Preview.SetText access-denied failure\n";
    return 0;
}
