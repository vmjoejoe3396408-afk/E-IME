#include "guids.h"
#include "text_service.h"
#include <windows.h>
#include <unknwn.h>
#include <atomic>
#include <new>

namespace eime {

HINSTANCE g_hInst = nullptr;
std::atomic<long> g_dllRefCount{0};

HRESULT RegisterComServer();
HRESULT RegisterTsfProfiles();
HRESULT UnregisterTsfProfiles();

class EimeClassFactory : public IClassFactory {
public:
    EimeClassFactory() : ref_count_(1) {
        ++g_dllRefCount;
    }

    virtual ~EimeClassFactory() {
        --g_dllRefCount;
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override {
        if (!ppvObj) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
            *ppvObj = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObj = nullptr;
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override {
        return ++ref_count_;
    }

    STDMETHODIMP_(ULONG) Release() override {
        ULONG r = --ref_count_;
        if (r == 0) delete this;
        return r;
    }

    STDMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppvObj) override {
        if (!ppvObj) return E_INVALIDARG;
        *ppvObj = nullptr;
        if (pUnkOuter != nullptr) return CLASS_E_NOAGGREGATION;

        try {
            auto* service = new EimeTextService();
            HRESULT hr = service->QueryInterface(riid, ppvObj);
            service->Release();
            return hr;
        } catch (const std::bad_alloc&) {
            return E_OUTOFMEMORY;
        } catch (...) {
            return E_FAIL;
        }
    }

    STDMETHODIMP LockServer(BOOL fLock) override {
        if (fLock) ++g_dllRefCount;
        else --g_dllRefCount;
        return S_OK;
    }

private:
    ULONG ref_count_;
};

} // namespace eime

extern "C" BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved) {
    (void)lpReserved;
    if (dwReason == DLL_PROCESS_ATTACH) {
        OutputDebugStringA("--> E-IME DllMain DLL_PROCESS_ATTACH entered\n");
        eime::g_hInst = hInstance;
        DisableThreadLibraryCalls(hInstance);
    }
    return TRUE;
}

// Match the Windows SDK declarations; eime_tip.def provides the exported names.
STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
    if (!ppv) return E_INVALIDARG;
    *ppv = nullptr;

    if (!IsEqualCLSID(rclsid, eime::CLSID_EIME_TextService)) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    auto* factory = new eime::EimeClassFactory();
    HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    return hr;
}

STDAPI DllCanUnloadNow(void) {
    return (eime::g_dllRefCount.load() <= 0) ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer(void) {
    HRESULT hr = eime::RegisterComServer();
    if (FAILED(hr)) return hr;
    return eime::RegisterTsfProfiles();
}

STDAPI DllUnregisterServer(void) {
    return eime::UnregisterTsfProfiles();
}
