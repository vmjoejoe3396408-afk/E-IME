#pragma once

#include <windows.h>
#include <msctf.h>
#include <string>
#include <vector>
#include <iostream>
#include <memory>
#include <algorithm>

namespace eime::test {

class MockRange;
struct MockDocument {
    std::wstring text;
    std::vector<MockRange*> ranges;
    size_t selection_start = 0;
    size_t selection_end = 0;
};

// 1. Mock ITfRange：模擬宿主文字選區與組字區內容讀寫
class MockRange : public ITfRange {
public:
    MockRange(std::wstring initial_text = L"") : ref_count_(1), document_(std::make_shared<MockDocument>()), start_(0), end_(initial_text.size()) {
        document_->text = std::move(initial_text);
        document_->ranges.push_back(this);
    }
    MockRange(std::shared_ptr<MockDocument> document, size_t start, size_t end)
        : ref_count_(1), document_(std::move(document)), start_(start), end_(end) { document_->ranges.push_back(this); }
    virtual ~MockRange() { std::erase(document_->ranges, this); }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override {
        if (!ppvObj) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfRange)) {
            *ppvObj = static_cast<ITfRange*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObj = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++ref_count_; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG r = --ref_count_;
        if (r == 0) delete this;
        return r;
    }

    STDMETHODIMP GetText(TfEditCookie, DWORD, WCHAR* pchText, ULONG cchMax, ULONG* pcch) override {
        ULONG copy_len = (std::min)(static_cast<ULONG>(end_ - start_), cchMax);
        if (pchText && copy_len > 0) {
            std::copy_n(document_->text.data() + start_, copy_len, pchText);
        }
        if (pcch) *pcch = copy_len;
        return S_OK;
    }

    STDMETHODIMP SetText(TfEditCookie, DWORD, const WCHAR* pchText, LONG cch) override {
        const std::wstring replacement = !pchText ? L"" : cch < 0 ? std::wstring(pchText) : std::wstring(pchText, static_cast<size_t>(cch));
        const size_t old_start = start_, old_end = end_;
        const auto delta = static_cast<ptrdiff_t>(replacement.size()) - static_cast<ptrdiff_t>(old_end - old_start);
        document_->text.replace(old_start, old_end - old_start, replacement);
        auto adjust_selection = [&](size_t pos) {
            if (pos < old_start) return pos;
            if (pos > old_end || (pos == old_end && old_end > old_start)) return static_cast<size_t>(static_cast<ptrdiff_t>(pos) + delta);
            return old_start + replacement.size();
        };
        document_->selection_start = adjust_selection(document_->selection_start);
        document_->selection_end = adjust_selection(document_->selection_end);
        for (auto* range : document_->ranges) {
            if (range == this) continue;
            auto adjust = [&](size_t pos, bool end_anchor) {
                if (pos < old_start) return pos;
                if (pos > old_end || (pos == old_end && old_end > old_start)) return static_cast<size_t>(static_cast<ptrdiff_t>(pos) + delta);
                return end_anchor ? old_start + replacement.size() : old_start;
            };
            range->start_ = adjust(range->start_, false);
            range->end_ = adjust(range->end_, true);
        }
        end_ = start_ + replacement.size();
        return S_OK;
    }

    STDMETHODIMP Collapse(TfEditCookie, TfAnchor anchor) override {
        if (anchor == TF_ANCHOR_START) end_ = start_; else start_ = end_;
        return S_OK;
    }
    STDMETHODIMP Clone(ITfRange** range) override {
        if (!range) return E_INVALIDARG;
        *range = new MockRange(document_, start_, end_);
        return S_OK;
    }
    STDMETHODIMP IsEmpty(TfEditCookie, BOOL* pfEmpty) override {
        if (pfEmpty) *pfEmpty = start_ == end_ ? TRUE : FALSE;
        return S_OK;
    }
    STDMETHODIMP GetFormattedText(TfEditCookie, IDataObject**) override { return E_NOTIMPL; }
    STDMETHODIMP GetEmbedded(TfEditCookie, REFGUID, REFIID, IUnknown**) override { return E_NOTIMPL; }
    STDMETHODIMP InsertEmbedded(TfEditCookie, DWORD, IDataObject*) override { return E_NOTIMPL; }
    STDMETHODIMP GetContext(ITfContext**) override { return E_NOTIMPL; }
    STDMETHODIMP ShiftStart(TfEditCookie, LONG, LONG*, const TF_HALTCOND*) override { return E_NOTIMPL; }
    STDMETHODIMP ShiftEnd(TfEditCookie, LONG count, LONG* shifted, const TF_HALTCOND*) override {
        const auto next = static_cast<size_t>(std::clamp<ptrdiff_t>(static_cast<ptrdiff_t>(end_) + count, start_, document_->text.size()));
        if (shifted) *shifted = static_cast<LONG>(next) - static_cast<LONG>(end_);
        end_ = next;
        return S_OK;
    }
    STDMETHODIMP ShiftStartToRange(TfEditCookie, ITfRange*, TfAnchor) override { return E_NOTIMPL; }
    STDMETHODIMP ShiftEndToRange(TfEditCookie, ITfRange*, TfAnchor) override { return E_NOTIMPL; }
    STDMETHODIMP ShiftStartRegion(TfEditCookie, TfShiftDir, BOOL*) override { return E_NOTIMPL; }
    STDMETHODIMP ShiftEndRegion(TfEditCookie, TfShiftDir, BOOL*) override { return E_NOTIMPL; }
    STDMETHODIMP AdjustForInsert(TfEditCookie, ULONG, BOOL*) override { return E_NOTIMPL; }
    STDMETHODIMP GetGravity(TfGravity*, TfGravity*) override { return E_NOTIMPL; }
    STDMETHODIMP SetGravity(TfEditCookie, TfGravity, TfGravity) override { return E_NOTIMPL; }
    STDMETHODIMP CompareStart(TfEditCookie, ITfRange*, TfAnchor, LONG*) override { return E_NOTIMPL; }
    STDMETHODIMP CompareEnd(TfEditCookie, ITfRange*, TfAnchor, LONG*) override { return E_NOTIMPL; }
    STDMETHODIMP IsEqualStart(TfEditCookie, ITfRange*, TfAnchor, BOOL*) override { return E_NOTIMPL; }
    STDMETHODIMP IsEqualEnd(TfEditCookie, ITfRange*, TfAnchor, BOOL*) override { return E_NOTIMPL; }

    const std::wstring& current_text() const { return document_->text; }
    size_t start() const { return start_; }
    size_t end() const { return end_; }
    const std::shared_ptr<MockDocument>& document() const { return document_; }

private:
    ULONG ref_count_;
    std::shared_ptr<MockDocument> document_;
    size_t start_, end_;
};

// 2. Mock ITfContextView：模擬回報宿主游標螢幕物理座標
class MockContextView : public ITfContextView {
public:
    MockContextView(RECT caret_rect = {120, 240, 130, 260})
        : ref_count_(1), caret_rect_(caret_rect) {}
    virtual ~MockContextView() = default;

    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override {
        if (!ppvObj) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfContextView)) {
            *ppvObj = static_cast<ITfContextView*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObj = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++ref_count_; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG r = --ref_count_;
        if (r == 0) delete this;
        return r;
    }

    STDMETHODIMP GetRangeFromPoint(TfEditCookie, const POINT*, DWORD, ITfRange**) override { return E_NOTIMPL; }
    STDMETHODIMP GetTextExt(TfEditCookie, ITfRange*, RECT* prc, BOOL* pfClipped) override {
        if (prc) *prc = caret_rect_;
        if (pfClipped) *pfClipped = FALSE;
        return S_OK;
    }
    STDMETHODIMP GetScreenExt(RECT* prc) override {
        if (prc) *prc = RECT{0, 0, 1920, 1080};
        return S_OK;
    }
    STDMETHODIMP GetWnd(HWND* phwnd) override {
        if (phwnd) *phwnd = nullptr;
        return S_OK;
    }

    void set_caret_rect(const RECT& rc) { caret_rect_ = rc; }

private:
    ULONG ref_count_;
    RECT caret_rect_;
};

class MockProperty : public ITfProperty {
public:
    ULONG references = 1;
    size_t start = 0, end = 0;
    size_t assignments = 0;
    STDMETHODIMP QueryInterface(REFIID id, void** out) override {
        if (!out) return E_INVALIDARG;
        *out = nullptr;
        if (id != IID_IUnknown && id != IID_ITfProperty && id != IID_ITfReadOnlyProperty) return E_NOINTERFACE;
        *out = static_cast<ITfProperty*>(this); AddRef(); return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++references; }
    STDMETHODIMP_(ULONG) Release() override { auto n = --references; if (!n) delete this; return n; }
    STDMETHODIMP GetType(GUID* out) override { if (!out) return E_INVALIDARG; *out = GUID_PROP_ATTRIBUTE; return S_OK; }
    STDMETHODIMP EnumRanges(TfEditCookie, IEnumTfRanges**, ITfRange*) override { return E_NOTIMPL; }
    STDMETHODIMP GetValue(TfEditCookie, ITfRange*, VARIANT*) override { return E_NOTIMPL; }
    STDMETHODIMP GetContext(ITfContext**) override { return E_NOTIMPL; }
    STDMETHODIMP FindRange(TfEditCookie, ITfRange*, ITfRange**, TfAnchor) override { return E_NOTIMPL; }
    STDMETHODIMP SetValueStore(TfEditCookie, ITfRange*, ITfPropertyStore*) override { return E_NOTIMPL; }
    STDMETHODIMP SetValue(TfEditCookie, ITfRange* range, const VARIANT* value) override {
        if (!range || !value || value->vt != VT_I4) return E_INVALIDARG;
        auto* r = static_cast<MockRange*>(range);
        start = r->start(); end = r->end(); ++assignments;
        return S_OK;
    }
    STDMETHODIMP Clear(TfEditCookie, ITfRange*) override { return S_OK; }
};

// 3. Mock ITfComposition：模擬組字生命週期管理
class MockComposition : public ITfComposition {
public:
    MockComposition(MockRange* range, ITfCompositionSink* sink) : ref_count_(1), range_(range), is_ended_(false), sink_(sink) {
        if (range_) range_->AddRef();
        if (sink_) sink_->AddRef();
    }
    virtual ~MockComposition() {
        if (range_) range_->Release();
        if (sink_) sink_->Release();
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override {
        if (!ppvObj) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfComposition)) {
            *ppvObj = static_cast<ITfComposition*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObj = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++ref_count_; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG r = --ref_count_;
        if (r == 0) delete this;
        return r;
    }

    STDMETHODIMP GetRange(ITfRange** ppRange) override {
        if (!ppRange) return E_INVALIDARG;
        return range_ ? range_->Clone(ppRange) : E_FAIL;
    }
    STDMETHODIMP EndComposition(TfEditCookie ec) override {
        is_ended_ = true;
        if (sink_) {
            auto* sink = sink_;
            sink_ = nullptr;
            sink->OnCompositionTerminated(ec, this);
            sink->Release();
        }
        return S_OK;
    }
    STDMETHODIMP ShiftStart(TfEditCookie, ITfRange*) override { return E_NOTIMPL; }
    STDMETHODIMP ShiftEnd(TfEditCookie, ITfRange*) override { return E_NOTIMPL; }

    bool is_ended() const { return is_ended_; }

private:
    ULONG ref_count_;
    MockRange* range_;
    bool is_ended_;
    ITfCompositionSink* sink_;
};

// 4. Mock ITfContext & ITfContextComposition：模擬宿主文字環境
class MockContext : public ITfContext, public ITfContextComposition {
public:
    MockContext() : ref_count_(1), range_(new MockRange()), view_(new MockContextView()), active_composition_(nullptr) {}
    virtual ~MockContext() {
        if (range_) range_->Release();
        if (view_) view_->Release();
        if (active_composition_) active_composition_->Release();
        property_->Release();
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override {
        if (!ppvObj) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfContext)) {
            *ppvObj = static_cast<ITfContext*>(this);
            AddRef();
            return S_OK;
        } else if (IsEqualIID(riid, IID_ITfContextComposition)) {
            *ppvObj = static_cast<ITfContextComposition*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObj = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++ref_count_; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG r = --ref_count_;
        if (r == 0) delete this;
        return r;
    }

    STDMETHODIMP RequestEditSession(TfClientId, ITfEditSession* pes, DWORD flags, HRESULT* phrSession) override {
        if (!pes) return E_INVALIDARG;
        if (defer_edits) {
            if (flags & TF_ES_SYNC) { if (phrSession) *phrSession = TF_E_SYNCHRONOUS; return S_OK; }
            pes->AddRef();
            pending_.push_back(pes);
            if (phrSession) *phrSession = TF_S_ASYNC;
            return S_OK;
        }
        HRESULT hr = pes->DoEditSession(1); // 立即同步執行 DoEditSession
        if (phrSession) *phrSession = hr;
        return S_OK;
    }

    STDMETHODIMP InWriteSession(TfClientId, BOOL* pfWriteSession) override {
        if (pfWriteSession) *pfWriteSession = TRUE;
        return S_OK;
    }

    STDMETHODIMP GetSelection(TfEditCookie, ULONG, ULONG ulCount, TF_SELECTION* pSelection, ULONG* pcFetched) override {
        if (ulCount > 0 && pSelection) {
            auto document = range_->document();
            pSelection[0].range = new MockRange(document, document->selection_start, document->selection_end);
            pSelection[0].style.ase = TF_AE_NONE;
            pSelection[0].style.fInterimChar = FALSE;
            if (pcFetched) *pcFetched = 1;
            return S_OK;
        }
        return E_FAIL;
    }

    STDMETHODIMP SetSelection(TfEditCookie, ULONG count, const TF_SELECTION* selection) override {
        if (!count || !selection || !selection->range) return E_INVALIDARG;
        auto* range = static_cast<MockRange*>(selection->range);
        range_->document()->selection_start = range->start();
        range_->document()->selection_end = range->end();
        return S_OK;
    }

    STDMETHODIMP GetActiveView(ITfContextView** ppView) override {
        if (!ppView) return E_INVALIDARG;
        *ppView = view_;
        view_->AddRef();
        return S_OK;
    }

    // ITfContextComposition
    STDMETHODIMP StartComposition(TfEditCookie, ITfRange* pCompositionRange, ITfCompositionSink* sink, ITfComposition** ppComposition) override {
        if (!ppComposition) return E_INVALIDARG;
        auto* comp = new MockComposition(static_cast<MockRange*>(pCompositionRange), sink);
        *ppComposition = comp;
        if (active_composition_) active_composition_->Release();
        active_composition_ = comp;
        active_composition_->AddRef();
        return S_OK;
    }
    STDMETHODIMP EnumCompositions(IEnumITfCompositionView**) override { return E_NOTIMPL; }
    STDMETHODIMP FindComposition(TfEditCookie, ITfRange*, IEnumITfCompositionView**) override { return E_NOTIMPL; }
    STDMETHODIMP TakeOwnership(TfEditCookie, ITfCompositionView*, ITfCompositionSink*, ITfComposition**) override { return E_NOTIMPL; }
    STDMETHODIMP EnumViews(IEnumTfContextViews**) override { return E_NOTIMPL; }

    // 其餘 ITfContext 存根
    STDMETHODIMP GetDocumentMgr(ITfDocumentMgr**) override { return E_NOTIMPL; }
    STDMETHODIMP CreateRangeBackup(TfEditCookie, ITfRange*, ITfRangeBackup**) override { return E_NOTIMPL; }
    STDMETHODIMP GetProperty(REFGUID id, ITfProperty** out) override {
        if (!out) return E_INVALIDARG;
        *out = nullptr;
        if (id != GUID_PROP_ATTRIBUTE) return E_NOTIMPL;
        *out = property_; property_->AddRef(); return S_OK;
    }
    STDMETHODIMP GetAppProperty(REFGUID, ITfReadOnlyProperty**) override { return E_NOTIMPL; }
    STDMETHODIMP TrackProperties(const GUID**, ULONG, const GUID**, ULONG, ITfReadOnlyProperty**) override { return E_NOTIMPL; }
    STDMETHODIMP EnumProperties(IEnumTfProperties**) override { return E_NOTIMPL; }
    STDMETHODIMP GetEnd(TfEditCookie, ITfRange**) override { return E_NOTIMPL; }
    STDMETHODIMP GetStart(TfEditCookie, ITfRange**) override { return E_NOTIMPL; }
    STDMETHODIMP GetStatus(TF_STATUS* pdcs) override {
        if (pdcs) { pdcs->dwDynamicFlags = 0; pdcs->dwStaticFlags = 0; }
        return S_OK;
    }

    MockRange* range() const { return range_; }
    MockContextView* view() const { return view_; }
    MockProperty* property() const { return property_; }
    MockComposition* composition() const { return active_composition_; }
    size_t pending_count() const { return pending_.size(); }
    bool defer_edits = false;
    void flush_edits() {
        auto pending = std::move(pending_);
        pending_.clear();
        for (auto* session : pending) {
            session->DoEditSession(1);
            session->Release();
        }
    }

private:
    ULONG ref_count_;
    MockRange* range_;
    MockContextView* view_;
    MockComposition* active_composition_;
    MockProperty* property_ = new MockProperty();
    std::vector<ITfEditSession*> pending_;
};

// 5. Mock ITfThreadMgr & ITfKeystrokeMgr：模擬輸入法管理器
class MockThreadMgr : public ITfThreadMgr, public ITfKeystrokeMgr {
public:
    MockThreadMgr() : ref_count_(1), key_sink_(nullptr), client_id_(1001) {}
    virtual ~MockThreadMgr() {
        if (key_sink_) key_sink_->Release();
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override {
        if (!ppvObj) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfThreadMgr)) {
            *ppvObj = static_cast<ITfThreadMgr*>(this);
            AddRef();
            return S_OK;
        } else if (IsEqualIID(riid, IID_ITfKeystrokeMgr)) {
            *ppvObj = static_cast<ITfKeystrokeMgr*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObj = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++ref_count_; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG r = --ref_count_;
        if (r == 0) delete this;
        return r;
    }

    // ITfKeystrokeMgr
    STDMETHODIMP AdviseKeyEventSink(TfClientId tid, ITfKeyEventSink* pSink, BOOL) override {
        client_id_ = tid;
        if (key_sink_) key_sink_->Release();
        key_sink_ = pSink;
        if (key_sink_) key_sink_->AddRef();
        return S_OK;
    }
    STDMETHODIMP UnadviseKeyEventSink(TfClientId) override {
        if (key_sink_) {
            key_sink_->Release();
            key_sink_ = nullptr;
        }
        return S_OK;
    }
    STDMETHODIMP GetForeground(CLSID*) override { return E_NOTIMPL; }
    STDMETHODIMP TestKeyDown(WPARAM, LPARAM, BOOL*) override { return E_NOTIMPL; }
    STDMETHODIMP TestKeyUp(WPARAM, LPARAM, BOOL*) override { return E_NOTIMPL; }
    STDMETHODIMP KeyDown(WPARAM, LPARAM, BOOL*) override { return E_NOTIMPL; }
    STDMETHODIMP KeyUp(WPARAM, LPARAM, BOOL*) override { return E_NOTIMPL; }
    STDMETHODIMP PreserveKey(TfClientId, REFGUID, const TF_PRESERVEDKEY*, const WCHAR*, ULONG) override { return E_NOTIMPL; }
    STDMETHODIMP UnpreserveKey(REFGUID, const TF_PRESERVEDKEY*) override { return E_NOTIMPL; }
    STDMETHODIMP IsPreservedKey(REFGUID, const TF_PRESERVEDKEY*, BOOL*) override { return E_NOTIMPL; }
    STDMETHODIMP SimulatePreservedKey(ITfContext*, REFGUID, BOOL*) override { return E_NOTIMPL; }
    STDMETHODIMP GetPreservedKey(ITfContext*, const TF_PRESERVEDKEY*, GUID*) override { return E_NOTIMPL; }
    STDMETHODIMP SetPreservedKeyDescription(REFGUID, const WCHAR*, ULONG) override { return E_NOTIMPL; }
    STDMETHODIMP GetPreservedKeyDescription(REFGUID, BSTR*) override { return E_NOTIMPL; }

    // ITfThreadMgr 存根
    STDMETHODIMP Activate(TfClientId* ptid) override { if (ptid) *ptid = client_id_; return S_OK; }
    STDMETHODIMP Deactivate() override { return S_OK; }
    STDMETHODIMP CreateDocumentMgr(ITfDocumentMgr**) override { return E_NOTIMPL; }
    STDMETHODIMP EnumDocumentMgrs(IEnumTfDocumentMgrs**) override { return E_NOTIMPL; }
    STDMETHODIMP GetFocus(ITfDocumentMgr**) override { return E_NOTIMPL; }
    STDMETHODIMP SetFocus(ITfDocumentMgr*) override { return E_NOTIMPL; }
    STDMETHODIMP AssociateFocus(HWND, ITfDocumentMgr*, ITfDocumentMgr**) override { return E_NOTIMPL; }
    STDMETHODIMP IsThreadFocus(BOOL*) override { return E_NOTIMPL; }
    STDMETHODIMP GetFunctionProvider(REFCLSID, ITfFunctionProvider**) override { return E_NOTIMPL; }
    STDMETHODIMP EnumFunctionProviders(IEnumTfFunctionProviders**) override { return E_NOTIMPL; }
    STDMETHODIMP GetGlobalCompartment(ITfCompartmentMgr**) override { return E_NOTIMPL; }

    ITfKeyEventSink* key_sink() const { return key_sink_; }
    TfClientId client_id() const { return client_id_; }

private:
    ULONG ref_count_;
    ITfKeyEventSink* key_sink_;
    TfClientId client_id_;
};

} // namespace eime::test
