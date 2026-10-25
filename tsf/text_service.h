#pragma once

#include <windows.h>
#include <msctf.h>
#include <string>
#include <memory>
#include <atomic>
#include <deque>
#include <optional>

#include "../engine/include/trie.h"
#include "../engine/include/viterbi.h"
#include "../engine/include/dict_loader.h"
#include "../engine/include/input_state_machine.h"
#include "../sync/include/sync_journal.h"
#include "../ui/candidate_window.h"
#include "display_attribute.h"

namespace eime {

extern HINSTANCE g_hInst;
extern std::atomic<long> g_dllRefCount;

struct EditSnapshot {
    std::string committed;
    std::string preview;
    std::string display_with_cursor;
    size_t caret_offset = 0;
    size_t target_offset = 0;
    bool candidates_open = false;
    std::vector<WordCandidate> candidates;
    size_t selected = 0;
    size_t page = 0;
    size_t pages = 0;
    bool simplified = false;
};

class EimeTextService : public ITfTextInputProcessor,
                        public ITfKeyEventSink,
                        public ITfCompositionSink,
                        public ITfDisplayAttributeProvider {
public:
    EimeTextService();
    virtual ~EimeTextService();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfTextInputProcessor
    STDMETHODIMP Activate(ITfThreadMgr* ptim, TfClientId tid) override;
    STDMETHODIMP Deactivate() override;

    // ITfKeyEventSink
    STDMETHODIMP OnSetFocus(BOOL fForeground) override;
    STDMETHODIMP OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnTestKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnPreservedKey(ITfContext* pic, REFGUID rguid, BOOL* pfEaten) override;

    // ITfCompositionSink
    STDMETHODIMP OnCompositionTerminated(TfEditCookie ecWrite, ITfComposition* pComposition) override;

    // ITfDisplayAttributeProvider
    STDMETHODIMP EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo **ppEnum) override;
    STDMETHODIMP GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo **ppInfo) override;

    // 由 EditSession 回呼，執行實際的宿主組字緩衝區更新與游標追蹤
    void drain_edit_sessions(TfEditCookie ec, ITfContext* context, unsigned long generation);
    unsigned long activation_generation() const { return activation_generation_; }

    static char vk_to_daqian_char(WPARAM vk);

private:
    ULONG ref_count_;
    ITfThreadMgr* thread_mgr_;
    TfClientId client_id_;
    ITfComposition* composition_;

    TfGuidAtom atom_input_ = 0;
    TfGuidAtom atom_target_ = 0;

    ZhuyinTrie trie_;
    std::unique_ptr<ViterbiDecoder> decoder_;
    std::unique_ptr<SyncJournal> journal_;
    std::unique_ptr<InputStateMachine> state_machine_;
    CandidateWindow candidate_window_;

    bool shift_pressed_alone_;
    unsigned long activation_generation_ = 0;
    ITfContext* edit_context_ = nullptr;
    std::deque<EditSnapshot> pending_edits_;
    bool edit_requested_ = false;
    bool draining_edits_ = false;

    struct KeyInput {
        WPARAM vk;
        LPARAM flags;
        bool ctrl;
        bool shift;
        bool alt;
        std::string text;
        bool rctrl = false;
        bool rshift = false;
    };
    std::optional<KeyInput> tested_key_;
    unsigned ctrl_keys_ = 0;
    bool ctrl_state_known_ = false;
    unsigned shift_keys_ = 0;
    bool shift_state_known_ = false;

    void initialize_engine_resources();
    void end_composition(TfEditCookie ec);
    void prepare_context(ITfContext* context);
    HRESULT queue_edit(ITfContext* context, const std::string& committed);
    void execute_edit_session(TfEditCookie ec, ITfContext* context, const EditSnapshot& snapshot);
    static std::string printable_key(WPARAM vk, LPARAM lParam);
    void observe_ctrl_key(WPARAM vk, LPARAM flags, bool down);
    void observe_shift_key(WPARAM vk, LPARAM flags, bool down);
    KeyInput capture_key(WPARAM vk, LPARAM flags) const;
    bool would_eat_key(const KeyInput& input) const;
    void update_ui(const POINT& caret_pt, const EditSnapshot& snapshot);
    POINT get_fallback_caret_position() const;
};

} // namespace eime
