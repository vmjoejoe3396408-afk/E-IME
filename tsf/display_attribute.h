#pragma once

#include <windows.h>
#include <msctf.h>
#include <vector>
#include <string>
#include <algorithm>
#include <atomic>
#include "guids.h"

#ifndef __ITfDisplayAttributeProvider_INTERFACE_DEFINED__
#define __ITfDisplayAttributeProvider_INTERFACE_DEFINED__

// {FEE45768-45A8-444A-A0F1-4359873D63B2}
static constexpr GUID IID_ITfDisplayAttributeProvider = {
    0xfee45768, 0x45a8, 0x444a, {0xa0, 0xf1, 0x43, 0x59, 0x87, 0x3d, 0x63, 0xb2}
};

MIDL_INTERFACE("fee45768-45a8-444a-a0f1-4359873d63b2")
ITfDisplayAttributeProvider : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE EnumDisplayAttributeInfo(
        IEnumTfDisplayAttributeInfo **ppEnum) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetDisplayAttributeInfo(
        REFGUID guid,
        ITfDisplayAttributeInfo **ppInfo) = 0;
};

#endif

namespace eime {

extern std::atomic<long> g_dllRefCount;

class EimeDisplayAttributeInfo : public ITfDisplayAttributeInfo {
public:
    EimeDisplayAttributeInfo(const GUID& guid, const std::wstring& desc, const TF_DISPLAYATTRIBUTE& attr)
        : ref_count_(1), guid_(guid), desc_(desc), attr_(attr) { ++g_dllRefCount; }

    virtual ~EimeDisplayAttributeInfo() { --g_dllRefCount; }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override {
        if (!ppvObj) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfDisplayAttributeInfo)) {
            *ppvObj = static_cast<ITfDisplayAttributeInfo*>(this);
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

    // ITfDisplayAttributeInfo
    STDMETHODIMP GetGUID(GUID *pguid) override {
        if (!pguid) return E_INVALIDARG;
        *pguid = guid_;
        return S_OK;
    }

    STDMETHODIMP GetDescription(BSTR *pbstrDesc) override {
        if (!pbstrDesc) return E_INVALIDARG;
        *pbstrDesc = SysAllocString(desc_.c_str());
        return *pbstrDesc ? S_OK : E_OUTOFMEMORY;
    }

    STDMETHODIMP GetAttributeInfo(TF_DISPLAYATTRIBUTE *pda) override {
        if (!pda) return E_INVALIDARG;
        *pda = attr_;
        return S_OK;
    }

    STDMETHODIMP SetAttributeInfo(const TF_DISPLAYATTRIBUTE *pda) override {
        if (!pda) return E_INVALIDARG;
        attr_ = *pda;
        return S_OK;
    }

    STDMETHODIMP Reset() override {
        return S_OK;
    }

private:
    ULONG ref_count_;
    GUID guid_;
    std::wstring desc_;
    TF_DISPLAYATTRIBUTE attr_;
};

class EimeEnumDisplayAttributeInfo : public IEnumTfDisplayAttributeInfo {
public:
    explicit EimeEnumDisplayAttributeInfo(std::vector<ITfDisplayAttributeInfo*> items)
        : ref_count_(1), items_(std::move(items)), index_(0) {
        ++g_dllRefCount;
        for (auto* item : items_) {
            if (item) item->AddRef();
        }
    }

    virtual ~EimeEnumDisplayAttributeInfo() {
        for (auto* item : items_) {
            if (item) item->Release();
        }
        --g_dllRefCount;
    }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override {
        if (!ppvObj) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IEnumTfDisplayAttributeInfo)) {
            *ppvObj = static_cast<IEnumTfDisplayAttributeInfo*>(this);
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

    // IEnumTfDisplayAttributeInfo
    STDMETHODIMP Clone(IEnumTfDisplayAttributeInfo **ppEnum) override {
        if (!ppEnum) return E_INVALIDARG;
        auto* clone = new EimeEnumDisplayAttributeInfo(items_);
        clone->index_ = index_;
        *ppEnum = clone;
        return S_OK;
    }

    STDMETHODIMP Next(ULONG ulCount, ITfDisplayAttributeInfo **rgInfo, ULONG *pcFetched) override {
        if (!rgInfo) return E_INVALIDARG;
        ULONG fetched = 0;
        while (fetched < ulCount && index_ < items_.size()) {
            rgInfo[fetched] = items_[index_++];
            rgInfo[fetched]->AddRef();
            fetched++;
        }
        if (pcFetched) *pcFetched = fetched;
        return (fetched == ulCount) ? S_OK : S_FALSE;
    }

    STDMETHODIMP Reset() override {
        index_ = 0;
        return S_OK;
    }

    STDMETHODIMP Skip(ULONG ulCount) override {
        index_ = std::min(index_ + ulCount, static_cast<ULONG>(items_.size()));
        return S_OK;
    }

private:
    ULONG ref_count_;
    std::vector<ITfDisplayAttributeInfo*> items_;
    ULONG index_;
};

} // namespace eime
