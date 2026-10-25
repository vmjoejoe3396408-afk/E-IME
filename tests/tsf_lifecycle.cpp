#include <windows.h>
#include <msctf.h>
#include <iostream>
#include "../tsf/guids.h"
#include "../tsf/display_attribute.h"
#include "../testbench/mock_tsf.h"
namespace eime { std::atomic<long> g_dllRefCount{0}; }

#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL line " << __LINE__ << ": " #x << std::endl; return 1; } } while (0)

int wmain(int argc, wchar_t** argv) {
    CHECK(argc >= 2);
    CHECK(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)));
    for (int cycle = 0; cycle < 25; ++cycle) {
        HMODULE dll = LoadLibraryW(argv[1]);
        if (!dll) std::cerr << "LoadLibrary error " << GetLastError() << std::endl;
        CHECK(dll);
        auto get = reinterpret_cast<HRESULT(WINAPI*)(REFCLSID, REFIID, void**)>(GetProcAddress(dll, "DllGetClassObject"));
        auto unload = reinterpret_cast<HRESULT(WINAPI*)()>(GetProcAddress(dll, "DllCanUnloadNow"));
        CHECK(get && unload);
        IClassFactory* factory = nullptr;
        CHECK(SUCCEEDED(get(eime::CLSID_EIME_TextService, IID_IClassFactory, reinterpret_cast<void**>(&factory))));
        ITfTextInputProcessor* tip = nullptr;
        CHECK(SUCCEEDED(factory->CreateInstance(nullptr, IID_ITfTextInputProcessor, reinterpret_cast<void**>(&tip))));
        factory->Release();
        ITfDisplayAttributeProvider* dap = nullptr;
        // Use the Windows SDK IID independently of the fallback declaration:
        // sharing an incorrect constant with the DLL previously hid this failure.
        const IID system_provider_iid = {0xfee47777, 0x163c, 0x4769,
            {0x99, 0x6a, 0x6e, 0x9c, 0x50, 0xad, 0x8f, 0x54}};
        CHECK(SUCCEEDED(tip->QueryInterface(system_provider_iid, reinterpret_cast<void**>(&dap))));
        IEnumTfDisplayAttributeInfo* attrs = nullptr;
        CHECK(SUCCEEDED(dap->EnumDisplayAttributeInfo(&attrs)));
        ITfDisplayAttributeInfo* info = nullptr;
        ULONG fetched = 0;
        CHECK(attrs->Next(1, &info, &fetched) == S_OK && fetched == 1);
        TF_DISPLAYATTRIBUTE attr = {};
        CHECK(SUCCEEDED(info->GetAttributeInfo(&attr)));
        CHECK(attr.lsStyle == TF_LS_SOLID && attr.bAttr == TF_ATTR_INPUT);
        dap->Release();
        tip->Release();
        if (argc == 2) CHECK(unload() == S_FALSE); // Windows may retain metadata after the TIP is gone.
        attrs->Release();
        if (argc == 2) CHECK(unload() == S_FALSE);
        info->Release();
        CHECK(unload() == S_OK);

        CHECK(SUCCEEDED(get(eime::CLSID_EIME_TextService, IID_IClassFactory, reinterpret_cast<void**>(&factory))));
        CHECK(SUCCEEDED(factory->CreateInstance(nullptr, IID_ITfTextInputProcessor, reinterpret_cast<void**>(&tip))));
        factory->Release();
        auto* mgr = new eime::test::MockThreadMgr();
        auto* context = new eime::test::MockContext();
        CHECK(SUCCEEDED(tip->Activate(mgr, 1001)));
        auto* sink = mgr->key_sink();
        CHECK(sink);
        BOOL eaten = FALSE;
        std::wstring expected;
        for (int i = 0; i < 20; ++i) {
            for (char key : std::string("su3cl3")) {
                CHECK(SUCCEEDED(sink->OnKeyDown(context, static_cast<WPARAM>(toupper(key)), 0, &eaten)) && eaten);
            }
            CHECK(SUCCEEDED(sink->OnKeyDown(context, VK_RETURN, 0, &eaten)) && eaten);
            expected += L"你好";
            CHECK(context->range()->current_text() == expected);
            CHECK(SUCCEEDED(sink->OnKeyDown(context, 'S', 0, &eaten)) && eaten);
            CHECK(SUCCEEDED(sink->OnKeyDown(context, VK_ESCAPE, 0, &eaten)) && eaten);
        }
        context->defer_edits = true;
        CHECK(SUCCEEDED(sink->OnKeyDown(context, 'S', 0, &eaten)) && eaten);
        CHECK(SUCCEEDED(tip->Deactivate()));
        CHECK(SUCCEEDED(tip->Activate(mgr, 1001)));
        auto before = context->range()->current_text();
        context->flush_edits();
        CHECK(context->range()->current_text() == before);
        CHECK(SUCCEEDED(tip->Deactivate()));
        tip->Release();
        context->Release();
        mgr->Release();
        CHECK(unload() == S_OK);
        CHECK(FreeLibrary(dll));
    }
    CoUninitialize();
    std::cout << "PASS: 25 load/unload cycles, metadata lifetime, 500 reentrant commits/cancels, stale queued edits" << std::endl;
    return 0;
}
