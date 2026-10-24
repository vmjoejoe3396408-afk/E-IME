#include "guids.h"
#include "text_service.h"
#include <windows.h>
#include <msctf.h>
#include <olectl.h>
#include <string>

namespace eime {

static const wchar_t* kClsidRegistryKey = L"CLSID\\{D8E42B10-7A5C-4E91-9B2A-1F3C5D7E9A01}";
static const wchar_t* kInprocServer32Key = L"CLSID\\{D8E42B10-7A5C-4E91-9B2A-1F3C5D7E9A01}\\InprocServer32";

HRESULT RegisterComServer() {
    wchar_t module_path[MAX_PATH] = {0};
    if (!GetModuleFileNameW(g_hInst, module_path, MAX_PATH)) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    HKEY hKey = nullptr;
    LONG res = RegCreateKeyExW(
        HKEY_CLASSES_ROOT, kClsidRegistryKey, 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr
    );
    if (res != ERROR_SUCCESS) return HRESULT_FROM_WIN32(res);

    RegSetValueExW(
        hKey, nullptr, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(kEimeDisplayName),
        static_cast<DWORD>((wcslen(kEimeDisplayName) + 1) * sizeof(wchar_t))
    );
    RegCloseKey(hKey);

    res = RegCreateKeyExW(
        HKEY_CLASSES_ROOT, kInprocServer32Key, 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr
    );
    if (res != ERROR_SUCCESS) return HRESULT_FROM_WIN32(res);

    RegSetValueExW(
        hKey, nullptr, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(module_path),
        static_cast<DWORD>((wcslen(module_path) + 1) * sizeof(wchar_t))
    );

    const wchar_t* threading_model = L"Apartment";
    RegSetValueExW(
        hKey, L"ThreadingModel", 0, REG_SZ,
        reinterpret_cast<const BYTE*>(threading_model),
        static_cast<DWORD>((wcslen(threading_model) + 1) * sizeof(wchar_t))
    );
    RegCloseKey(hKey);
    return S_OK;
}

HRESULT RegisterTsfProfiles() {
    wchar_t module_path[MAX_PATH] = {0};
    GetModuleFileNameW(g_hInst, module_path, MAX_PATH);

    ITfInputProcessorProfiles* profiles = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITfInputProcessorProfiles, reinterpret_cast<void**>(&profiles)
    );
    if (FAILED(hr) || !profiles) return hr;

    hr = profiles->Register(CLSID_EIME_TextService);
    if (SUCCEEDED(hr)) {
        hr = profiles->AddLanguageProfile(
            CLSID_EIME_TextService,
            kTaiwanChineseLangId,
            GUID_EIME_Profile,
            kEimeDisplayName,
            static_cast<ULONG>(wcslen(kEimeDisplayName)),
            module_path,
            static_cast<ULONG>(wcslen(module_path)),
            0
        );
    }
    profiles->Release();
    if (FAILED(hr)) return hr;

    ITfCategoryMgr* cat_mgr = nullptr;
    hr = CoCreateInstance(
        CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITfCategoryMgr, reinterpret_cast<void**>(&cat_mgr)
    );
    if (SUCCEEDED(hr) && cat_mgr) {
        const GUID categories[] = {
            GUID_TFCAT_TIP_KEYBOARD,
            GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER,
            EIME_GUID_TFCAT_TIPCAP_SECUREMODE,
            EIME_GUID_TFCAT_TIPCAP_UIELEMENTENABLED,
            EIME_GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,
            EIME_GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT
        };
        for (const auto& cat : categories) {
            cat_mgr->RegisterCategory(CLSID_EIME_TextService, cat, CLSID_EIME_TextService);
        }
        cat_mgr->Release();
    }
    return S_OK;
}

HRESULT UnregisterTsfProfiles() {
    ITfCategoryMgr* cat_mgr = nullptr;
    if (SUCCEEDED(CoCreateInstance(
            CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
            IID_ITfCategoryMgr, reinterpret_cast<void**>(&cat_mgr))) && cat_mgr) {
        const GUID categories[] = {
            GUID_TFCAT_TIP_KEYBOARD,
            GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER,
            EIME_GUID_TFCAT_TIPCAP_SECUREMODE,
            EIME_GUID_TFCAT_TIPCAP_UIELEMENTENABLED,
            EIME_GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,
            EIME_GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT
        };
        for (const auto& cat : categories) {
            cat_mgr->UnregisterCategory(CLSID_EIME_TextService, cat, CLSID_EIME_TextService);
        }
        cat_mgr->Release();
    }

    ITfInputProcessorProfiles* profiles = nullptr;
    if (SUCCEEDED(CoCreateInstance(
            CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
            IID_ITfInputProcessorProfiles, reinterpret_cast<void**>(&profiles))) && profiles) {
        profiles->Unregister(CLSID_EIME_TextService);
        profiles->Release();
    }

    RegDeleteKeyW(HKEY_CLASSES_ROOT, kInprocServer32Key);
    RegDeleteKeyW(HKEY_CLASSES_ROOT, kClsidRegistryKey);
    return S_OK;
}

} // namespace eime
