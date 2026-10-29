#include <windows.h>
#include <msctf.h>
#include <iostream>
#include "../tsf/guids.h"
#include "../testbench/mock_tsf.h"
#include "../engine/include/input_state_machine.h"
#include "../engine/include/dict_loader.h"

#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL line " << __LINE__ << ": " #x << std::endl; return 1; } } while (0)

// Send key events to the DLL using per-thread synthetic keyboard state; no desktop input.
static HRESULT key(ITfKeyEventSink* sink, ITfContext* context, WPARAM vk, bool ctrl = false, bool shift = false) {
    BYTE state[256] = {};
    if (ctrl) state[VK_CONTROL] = 0x80;
    if (shift) state[VK_SHIFT] = 0x80;
    if (vk >= VK_NUMPAD0 && vk <= VK_DIVIDE) state[VK_NUMLOCK] = 1;
    SetKeyboardState(state);
    BOOL eaten = FALSE;
    HRESULT hr = sink->OnTestKeyDown(context, vk, 0, &eaten);
    if (FAILED(hr) || !eaten) return E_FAIL;
    eaten = FALSE;
    hr = sink->OnKeyDown(context, vk, 0, &eaten);
    ZeroMemory(state, sizeof(state));
    SetKeyboardState(state);
    return SUCCEEDED(hr) && eaten ? S_OK : E_FAIL;
}

static HRESULT shift_toggle(ITfKeyEventSink* sink, ITfContext* context) {
    BYTE state[256] = {};
    state[VK_SHIFT] = 0x80;
    SetKeyboardState(state);
    BOOL eaten = FALSE;
    sink->OnTestKeyDown(context, VK_SHIFT, 0, &eaten);
    state[VK_SHIFT] = 0;
    SetKeyboardState(state);
    sink->OnTestKeyUp(context, VK_SHIFT, 0, &eaten);
    if (!eaten) return E_FAIL;
    return sink->OnKeyUp(context, VK_SHIFT, 0, &eaten);
}

static HRESULT type(ITfKeyEventSink* sink, ITfContext* context, const std::string& keys) {
    for (unsigned char c : keys) {
        WPARAM vk = c == ' ' ? VK_SPACE : c == '\r' ? VK_RETURN : static_cast<WPARAM>(toupper(c));
        if (FAILED(key(sink, context, vk))) return E_FAIL;
    }
    return S_OK;
}

int wmain(int argc, wchar_t** argv) {
    CHECK(argc == 2);
    CHECK(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)));
    auto dll = LoadLibraryW(argv[1]);
    CHECK(dll);
    auto get = reinterpret_cast<HRESULT(WINAPI*)(REFCLSID, REFIID, void**)>(GetProcAddress(dll, "DllGetClassObject"));
    auto unload = reinterpret_cast<HRESULT(WINAPI*)()>(GetProcAddress(dll, "DllCanUnloadNow"));
    IClassFactory* factory = nullptr;
    CHECK(SUCCEEDED(get(eime::CLSID_EIME_TextService, IID_IClassFactory, reinterpret_cast<void**>(&factory))));
    ITfTextInputProcessor* tip = nullptr;
    CHECK(SUCCEEDED(factory->CreateInstance(nullptr, IID_ITfTextInputProcessor, reinterpret_cast<void**>(&tip))));
    factory->Release();
    auto* mgr = new eime::test::MockThreadMgr();
    auto* context = new eime::test::MockContext();
    CHECK(SUCCEEDED(tip->Activate(mgr, 1001)));
    auto* sink = mgr->key_sink();

    CHECK(SUCCEEDED(type(sink, context, "su3cl3")));
    auto* composition = context->composition();
    CHECK(composition && !composition->is_ended());
    CHECK(SUCCEEDED(shift_toggle(sink, context)));
    CHECK(context->composition() == composition && !composition->is_ended());
    CHECK(context->range()->current_text() == L"你好");
    CHECK(context->property()->end - context->property()->start == 2);
    CHECK(SUCCEEDED(type(sink, context, "abc")));
    CHECK(SUCCEEDED(key(sink, context, '1', false, true)));
    CHECK(context->range()->current_text() == L"你好abc!");
    CHECK(context->range()->document()->selection_start == 6);
    CHECK(context->property()->end - context->property()->start == 6);
    CHECK(SUCCEEDED(shift_toggle(sink, context)));
    CHECK(SUCCEEDED(key(sink, context, VK_OEM_COMMA, true)));
    CHECK(context->range()->current_text() == L"你好abc!，");
    CHECK(context->range()->document()->selection_start == 7);
    CHECK(context->property()->end - context->property()->start == 7);
    CHECK(SUCCEEDED(type(sink, context, "su3cl3\r")));
    CHECK(context->range()->current_text() == L"你好abc!，你好");
    CHECK(SUCCEEDED(type(sink, context, "su")));
    CHECK(SUCCEEDED(shift_toggle(sink, context)));
    CHECK(context->range()->current_text() == L"你好abc!，你好"); // pending ㄋㄧ discarded
    CHECK(SUCCEEDED(shift_toggle(sink, context)));
    CHECK(SUCCEEDED(key(sink, context, VK_OEM_COMMA, true)));
    CHECK(context->composition() && !context->composition()->is_ended());
    CHECK(context->property()->end - context->property()->start == 1);
    // Repeated punctuation choices must keep the SAME host composition and underline.
    composition = context->composition();
    CHECK(SUCCEEDED(key(sink, context, VK_DOWN)));
    CHECK(SUCCEEDED(key(sink, context, '2'))); // ， -> 、
    CHECK(context->composition() == composition && !composition->is_ended());
    CHECK(context->range()->current_text().ends_with(L"、"));
    CHECK(context->property()->end - context->property()->start == 1);
    CHECK(SUCCEEDED(key(sink, context, VK_DOWN)));
    CHECK(SUCCEEDED(key(sink, context, VK_RETURN)));
    CHECK(context->composition() == composition && !composition->is_ended());
    CHECK(SUCCEEDED(key(sink, context, VK_ESCAPE)));

    // Whole-phrase and single-character choices stay editable until explicit commit.
    for (const auto& phonetics : {"su3", "su3cl3"}) {
        const auto prefix = context->range()->current_text();
        CHECK(SUCCEEDED(type(sink, context, phonetics)));
        composition = context->composition();
        const auto length = context->range()->current_text().size() - prefix.size();
        for (int i = 0; i < 3; ++i) {
            CHECK(SUCCEEDED(key(sink, context, VK_DOWN)));
            CHECK(SUCCEEDED(key(sink, context, '1')));
            CHECK(context->composition() == composition && !composition->is_ended());
            CHECK(context->property()->end - context->property()->start == static_cast<LONG>(length));
            CHECK(context->range()->document()->selection_start == context->range()->current_text().size());
        }
        const auto before_commit = context->range()->current_text();
        CHECK(SUCCEEDED(key(sink, context, VK_RETURN)));
        CHECK(composition->is_ended());
        CHECK(context->range()->current_text() == before_commit);
    }

    // Mid-sentence comma insertion: dotted underline covers whole composition, caret after comma
    CHECK(SUCCEEDED(type(sink, context, "su3cl3")));
    auto cur_text = context->range()->current_text();
    CHECK(cur_text.ends_with(L"你好"));
    CHECK(SUCCEEDED(key(sink, context, VK_LEFT))); // Move between 你 and 好
    CHECK(context->range()->document()->selection_start == cur_text.size() - 1);
    CHECK(SUCCEEDED(key(sink, context, VK_OEM_COMMA, true))); // Ctrl+, in the middle
    CHECK(context->composition() && !context->composition()->is_ended());
    CHECK(context->range()->current_text().ends_with(L"你，好"));
    CHECK(context->range()->document()->selection_start == context->range()->current_text().size() - 1); // After comma
    CHECK(context->property()->end - context->property()->start == 3); // Dotted underline covers "你，好"
    CHECK(SUCCEEDED(key(sink, context, VK_RETURN)));

    // First Ctrl+, sees the generic Ctrl bit. Later keys may only carry the
    // Ctrl-down/up events, so retain both sides until their matching release.
    for (WPARAM ctrl : {static_cast<WPARAM>(VK_LCONTROL), static_cast<WPARAM>(VK_RCONTROL)}) {
        BOOL eaten = FALSE;
        CHECK(SUCCEEDED(sink->OnTestKeyDown(context, ctrl, 0, &eaten)) && !eaten);
        auto expected_commas = context->range()->current_text();
        for (int i = 0; i < 8; ++i) {
            CHECK(SUCCEEDED(key(sink, context, VK_OEM_COMMA, i == 0)));
            expected_commas += L"，";
            CHECK(context->range()->current_text() == expected_commas);
            CHECK(context->composition() && !context->composition()->is_ended());
            CHECK(context->property()->end - context->property()->start == i + 1);
        }
        CHECK(SUCCEEDED(sink->OnTestKeyUp(context, ctrl, 0, &eaten)) && !eaten);
        CHECK(SUCCEEDED(key(sink, context, VK_OEM_COMMA)));
        CHECK(context->range()->current_text() == expected_commas + L"ㄝ"); // Bare comma remains Zhuyin.
        CHECK(SUCCEEDED(key(sink, context, VK_BACK)));
        CHECK(SUCCEEDED(key(sink, context, VK_RETURN)));
        CHECK(context->range()->current_text() == expected_commas);
        CHECK(SUCCEEDED(sink->OnSetFocus(FALSE)));
        CHECK(SUCCEEDED(sink->OnSetFocus(TRUE)));
    }

    // Modifier state must belong to the accepted event, not a later host state.
    {
        BYTE state[256] = {};
        state[VK_CONTROL] = 0x80;
        CHECK(SetKeyboardState(state));
        BOOL eaten = FALSE;
        CHECK(SUCCEEDED(sink->OnTestKeyDown(context, VK_OEM_COMMA, 0, &eaten)) && eaten);
        ZeroMemory(state, sizeof(state));
        CHECK(SetKeyboardState(state));
        const auto before = context->range()->current_text();
        CHECK(SUCCEEDED(sink->OnKeyDown(context, VK_OEM_COMMA, 0, &eaten)) && eaten);
        CHECK(context->range()->current_text() == before + L"，");
        CHECK(SUCCEEDED(key(sink, context, VK_RETURN)));
    }

    // Numpad digits are literals at the composition caret, never Zhuyin or choices.
    {
        const auto prefix = context->range()->current_text();
        CHECK(SUCCEEDED(type(sink, context, "su3cl3")));
        composition = context->composition();
        CHECK(SUCCEEDED(key(sink, context, VK_LEFT)));
        std::wstring digits;
        for (int i = 0; i < 10; ++i) {
            CHECK(SUCCEEDED(key(sink, context, VK_NUMPAD0 + i)));
            digits += static_cast<wchar_t>(L'0' + i);
            CHECK(context->range()->current_text() == prefix + L"你" + digits + L"好");
            CHECK(context->composition() == composition && !composition->is_ended());
            CHECK(context->property()->end - context->property()->start == i + 3);
            CHECK(context->range()->document()->selection_start == prefix.size() + 1 + digits.size());
        }
        CHECK(SUCCEEDED(type(sink, context, "su3")));
        CHECK(context->range()->current_text() == prefix + L"你0123456789你好");
        CHECK(SUCCEEDED(key(sink, context, VK_RETURN)));
        CHECK(context->range()->current_text() == prefix + L"你0123456789你好");

        CHECK(SUCCEEDED(type(sink, context, "su3")));
        CHECK(SUCCEEDED(key(sink, context, VK_DOWN)));
        CHECK(SUCCEEDED(key(sink, context, VK_NUMPAD1)));
        CHECK(context->range()->current_text().ends_with(L"你1"));
        CHECK(SUCCEEDED(shift_toggle(sink, context)));
        CHECK(SUCCEEDED(key(sink, context, VK_NUMPAD2)));
        CHECK(context->range()->current_text().ends_with(L"你12"));
        CHECK(context->property()->end - context->property()->start == 3);
        CHECK(SUCCEEDED(key(sink, context, VK_RETURN)));
        CHECK(SUCCEEDED(shift_toggle(sink, context)));
    }

    // Ctrl+Shift+1 inserts fullwidth punctuation in both idle and composed text.
    {
        const auto prefix = context->range()->current_text();
        for (int i = 0; i < 2; ++i) {
            CHECK(SUCCEEDED(key(sink, context, '1', true, true)));
            CHECK(context->range()->current_text() == prefix + (i == 0 ? L"！" : L"！！"));
            CHECK(context->composition() && !context->composition()->is_ended());
            CHECK(context->property()->end - context->property()->start == i + 1);
        }
        CHECK(SUCCEEDED(key(sink, context, VK_RETURN)));
        CHECK(SUCCEEDED(type(sink, context, "su3cl3")));
        composition = context->composition();
        CHECK(SUCCEEDED(key(sink, context, VK_LEFT)));
        CHECK(SUCCEEDED(key(sink, context, '1', true, true)));
        CHECK(context->range()->current_text() == prefix + L"！！你！好");
        CHECK(context->composition() == composition && !composition->is_ended());
        CHECK(context->property()->end - context->property()->start == 3);
        CHECK(context->range()->document()->selection_start == prefix.size() + 4);
        CHECK(SUCCEEDED(key(sink, context, VK_RETURN)));
        CHECK(SUCCEEDED(shift_toggle(sink, context)));
        BYTE state[256] = {};
        state[VK_CONTROL] = state[VK_SHIFT] = 0x80;
        CHECK(SetKeyboardState(state));
        BOOL eaten = TRUE;
        CHECK(SUCCEEDED(sink->OnTestKeyDown(context, '1', 0, &eaten)) && !eaten);
        ZeroMemory(state, sizeof(state));
        CHECK(SetKeyboardState(state));
        CHECK(SUCCEEDED(shift_toggle(sink, context)));
    }

    // Ctrl+Shift+; inserts fullwidth colon L"：" while Ctrl+; inserts fullwidth semicolon L"；"
    // and consecutive typing 次 (h4) + 我 (ji3) does not turn 次 into 促
    {
        const auto prefix = context->range()->current_text();
        CHECK(SUCCEEDED(key(sink, context, VK_OEM_1, true, true)));
        CHECK(context->range()->current_text() == prefix + L"：");
        CHECK(SUCCEEDED(key(sink, context, VK_RETURN)));
        CHECK(SUCCEEDED(key(sink, context, VK_OEM_1, true, false)));
        CHECK(context->range()->current_text() == prefix + L"：；");
        CHECK(SUCCEEDED(key(sink, context, VK_RETURN)));

        CHECK(SUCCEEDED(type(sink, context, "h4ji3\r")));
        CHECK(context->range()->current_text() == prefix + L"：；次我");

        // Test comma in various conditions:
        // A. Idle Ctrl+,
        CHECK(SUCCEEDED(key(sink, context, VK_OEM_COMMA, true, false)));
        CHECK(context->range()->current_text() == prefix + L"：；次我，");
        CHECK(SUCCEEDED(key(sink, context, VK_RETURN)));

        // B. Idle Shift+,
        CHECK(SUCCEEDED(key(sink, context, VK_OEM_COMMA, false, true)));
        CHECK(context->range()->current_text() == prefix + L"：；次我，，");
        CHECK(SUCCEEDED(key(sink, context, VK_RETURN)));

        // C. Leader key ` then ,
        CHECK(SUCCEEDED(key(sink, context, VK_OEM_3)));
        CHECK(SUCCEEDED(key(sink, context, VK_OEM_COMMA)));
        CHECK(context->range()->current_text() == prefix + L"：；次我，，，");
        CHECK(SUCCEEDED(key(sink, context, VK_RETURN)));

        // D. Composing then Ctrl+,
        CHECK(SUCCEEDED(type(sink, context, "su3"))); // 你
        CHECK(SUCCEEDED(key(sink, context, VK_OEM_COMMA, true, false)));
        CHECK(context->range()->current_text() == prefix + L"：；次我，，，你，");
        CHECK(SUCCEEDED(key(sink, context, VK_RETURN)));

        // E. Composing then Shift+,
        CHECK(SUCCEEDED(type(sink, context, "cl3"))); // 好
        CHECK(SUCCEEDED(key(sink, context, VK_OEM_COMMA, false, true)));
        CHECK(context->range()->current_text() == prefix + L"：；次我，，，你，好，");
        CHECK(SUCCEEDED(key(sink, context, VK_RETURN)));

        // F. Single syllable 次 (h4) without enter, then Ctrl+,
        CHECK(SUCCEEDED(type(sink, context, "h4"))); // 次
        CHECK(SUCCEEDED(key(sink, context, VK_OEM_COMMA, true, false)));
        CHECK(context->range()->current_text() == prefix + L"：；次我，，，你，好，次，");
        CHECK(SUCCEEDED(key(sink, context, VK_RETURN)));
    }

    // Unmapped printable keys remain in the same composition, even before Zhuyin.
    {
        const auto prefix = context->range()->current_text();
        CHECK(SUCCEEDED(key(sink, context, VK_OEM_PLUS))); // = at idle
        composition = context->composition();
        CHECK(composition && !composition->is_ended());
        CHECK(SUCCEEDED(type(sink, context, "su3")));
        CHECK(SUCCEEDED(key(sink, context, VK_LEFT)));
        CHECK(SUCCEEDED(key(sink, context, VK_OEM_7))); // '
        CHECK(SUCCEEDED(key(sink, context, VK_OEM_5))); // backslash
        CHECK(context->range()->current_text() == prefix + L"='\\你");
        CHECK(context->composition() == composition && !composition->is_ended());
        CHECK(context->property()->end - context->property()->start == 4);
        CHECK(context->range()->document()->selection_start == prefix.size() + 3);
        CHECK(SUCCEEDED(key(sink, context, VK_DELETE)));
        CHECK(context->range()->current_text() == prefix + L"='\\");
        CHECK(SUCCEEDED(key(sink, context, VK_DOWN)));
        CHECK(SUCCEEDED(key(sink, context, '1'))); // literal candidate, not new Zhuyin
        CHECK(context->range()->current_text() == prefix + L"='\\");
        CHECK(context->composition() == composition && !composition->is_ended());
        CHECK(SUCCEEDED(key(sink, context, VK_BACK)));
        CHECK(SUCCEEDED(key(sink, context, VK_RETURN)));
        CHECK(context->range()->current_text() == prefix + L"='");

        CHECK(SUCCEEDED(key(sink, context, VK_NUMPAD5))); // numpad can start a composition
        CHECK(context->composition() && !context->composition()->is_ended());
        CHECK(SUCCEEDED(type(sink, context, "su3")));
        CHECK(SUCCEEDED(key(sink, context, VK_RETURN)));
        CHECK(context->range()->current_text() == prefix + L"='5你");
    }

    // Correct a pending rhyme directly; a tone still closes this syllable.
    {
        const auto prefix = context->range()->current_text();
        CHECK(SUCCEEDED(type(sink, context, "c0l"))); // ㄏㄢ -> ㄏㄠ
        CHECK(context->range()->current_text() == prefix + L"ㄏㄠ");
        CHECK(SUCCEEDED(key(sink, context, VK_BACK)));
        CHECK(context->range()->current_text() == prefix + L"ㄏ");
        CHECK(SUCCEEDED(type(sink, context, "l3su3")));
        CHECK(context->range()->current_text() == prefix + L"好你");
        CHECK(SUCCEEDED(key(sink, context, VK_RETURN)));
    }

    // MS-IME typo phonotactic permutation ('k' ㄜ + '2' ㄉ -> 'ㄉㄜ', + '7' -> '的')
    CHECK(SUCCEEDED(type(sink, context, "k"))); // ㄜ
    CHECK(SUCCEEDED(type(sink, context, "2"))); // ㄉ -> reorders to ㄉㄜ
    CHECK(context->composition() && !context->composition()->is_ended());
    CHECK(context->range()->current_text().ends_with(L"ㄉㄜ")); // Not '婀ㄉ'!
    CHECK(SUCCEEDED(type(sink, context, "7"))); // 7 -> 的
    CHECK(context->range()->current_text().ends_with(L"的"));
    CHECK(SUCCEEDED(key(sink, context, VK_RETURN)));

    context->defer_edits = true;
    {
        const auto prefix = context->range()->current_text();
        CHECK(SUCCEEDED(type(sink, context, "c0l3\r")));
        CHECK(context->pending_count() == 1);
        context->flush_edits();
        CHECK(context->range()->current_text() == prefix + L"好");
    }
    // Ordinary symbols and spaces cannot overtake a queued Chinese commit.
    {
        const auto prefix = context->range()->current_text();
        CHECK(SUCCEEDED(type(sink, context, "su3\r")));
        CHECK(SUCCEEDED(key(sink, context, VK_SPACE)));
        CHECK(SUCCEEDED(key(sink, context, VK_OEM_PLUS)));
        CHECK(SUCCEEDED(type(sink, context, "cl3")));
        CHECK(SUCCEEDED(key(sink, context, VK_OEM_7)));
        CHECK(SUCCEEDED(key(sink, context, VK_RETURN)));
        CHECK(context->pending_count() == 1);
        context->flush_edits();
        CHECK(context->range()->current_text() == prefix + L"你 =好'");
    }
    // A numpad digit must not overtake a commit waiting for an edit session.
    {
        auto before = context->range()->current_text();
        CHECK(SUCCEEDED(type(sink, context, "su3\r")));
        CHECK(SUCCEEDED(key(sink, context, VK_NUMPAD5)));
        CHECK(context->pending_count() == 1);
        context->flush_edits();
        CHECK(context->range()->current_text() == before + L"你5");
    }
    auto expected = context->range()->current_text();
    for (int i = 0; i < 100; ++i) {
        CHECK(SUCCEEDED(type(sink, context, "su3\rcl3\r")));
        expected += L"你好";
    }
    CHECK(context->pending_count() == 1); // one FIFO drain, never competing callbacks
    context->flush_edits();
    CHECK(context->range()->current_text() == expected);
    CHECK(SUCCEEDED(type(sink, context, "su3")));
    CHECK(SUCCEEDED(shift_toggle(sink, context)));
    CHECK(SUCCEEDED(type(sink, context, "abc\rxyz"))); // English must not overtake a queued commit.
    expected += L"你abcxyz";
    context->flush_edits();
    CHECK(context->range()->current_text() == expected);

    // Test Right Shift + Right Ctrl + S (Simplified) and Right Shift + Right Ctrl + T (Traditional)
    {
        CHECK(SUCCEEDED(shift_toggle(sink, context))); // ensure Chinese mode
        BYTE state[256] = {};
        state[VK_RCONTROL] = 0x80;
        state[VK_RSHIFT] = 0x80;
        state[VK_CONTROL] = 0x80;
        state[VK_SHIFT] = 0x80;
        SetKeyboardState(state);
        BOOL eaten = FALSE;
        CHECK(SUCCEEDED(sink->OnTestKeyDown(context, 'S', 0, &eaten)) && eaten);
        eaten = FALSE;
        CHECK(SUCCEEDED(sink->OnKeyDown(context, 'S', 0, &eaten)) && eaten);
        ZeroMemory(state, sizeof(state));
        SetKeyboardState(state);

        CHECK(SUCCEEDED(type(sink, context, "wu3\r")));
        expected += L"体";
        context->flush_edits();
        CHECK(context->range()->current_text() == expected);

        // Switch back to Traditional with Right Shift + Right Ctrl + T
        state[VK_RCONTROL] = 0x80;
        state[VK_RSHIFT] = 0x80;
        state[VK_CONTROL] = 0x80;
        state[VK_SHIFT] = 0x80;
        SetKeyboardState(state);
        eaten = FALSE;
        CHECK(SUCCEEDED(sink->OnTestKeyDown(context, 'T', 0, &eaten)) && eaten);
        eaten = FALSE;
        CHECK(SUCCEEDED(sink->OnKeyDown(context, 'T', 0, &eaten)) && eaten);
        ZeroMemory(state, sizeof(state));
        SetKeyboardState(state);

        CHECK(SUCCEEDED(type(sink, context, "wu3\r")));
        expected += L"體";
        context->flush_edits();
        CHECK(context->range()->current_text() == expected);
    }

    CHECK(SUCCEEDED(tip->Deactivate()));
    tip->Release(); context->Release(); mgr->Release();
    CHECK(unload() == S_OK);
    CHECK(FreeLibrary(dll));

    eime::ZhuyinTrie trie;
    CHECK(eime::DictLoader::load_from_file("data/core_dict.txt", trie, false));
    eime::ViterbiDecoder decoder(trie);
    eime::InputStateMachine sm(trie, decoder);
    std::string out;
    CHECK(sm.process_literal("😀，", out));
    CHECK(sm.cursor_utf16_offset() == 3);
    for (char c : std::string("su3cl3")) sm.process_key(c, out);
    CHECK(sm.get_display_line() == "😀，你好");
    CHECK(sm.cursor_utf16_offset() == 5);
    sm.process_special_key(eime::SpecialKey::LeftArrow, out);
    sm.process_literal("!", out);
    CHECK(sm.get_display_line() == "😀，你!好");
    CHECK(sm.cursor_utf16_offset() == 5);
    sm.process_special_key(eime::SpecialKey::Backspace, out);
    CHECK(sm.get_display_line() == "😀，你好");
    sm.reset();
    CHECK(sm.process_key('=', out));
    CHECK(out.empty() && sm.state() == eime::EngineState::Composing);
    CHECK(sm.get_display_line() == "=");
    CHECK(sm.process_special_key(eime::SpecialKey::DownArrow, out));
    CHECK(sm.state() == eime::EngineState::CandidateOpen);
    CHECK(sm.current_page_candidates().size() == 1);
    CHECK(sm.current_page_candidates()[0].text == "=");
    CHECK(sm.process_key('1', out));
    CHECK(out.empty() && sm.get_display_line() == "=");
    sm.reset();
    CHECK(sm.process_literal("😀", out));
    sm.process_special_key(eime::SpecialKey::Backspace, out);
    CHECK(sm.get_display_line().empty());
    // Space is Tone 1 while phonetics are pending, otherwise a literal, never selection.
    for (char c : std::string("ru ")) sm.process_key(c, out);
    CHECK(sm.state() == eime::EngineState::Composing);
    const auto before_space = sm.get_display_line();
    CHECK(sm.process_key(' ', out));
    CHECK(sm.get_display_line() == before_space + " ");
    CHECK(sm.state() == eime::EngineState::Composing);
    sm.reset();
    for (char c : std::string("su3cl3")) sm.process_key(c, out);
    sm.process_special_key(eime::SpecialKey::DownArrow, out);
    CHECK(sm.state() == eime::EngineState::CandidateOpen);
    CHECK(sm.process_key(' ', out));
    CHECK(out.empty() && sm.state() == eime::EngineState::Composing);
    CHECK(sm.get_display_line() == "你好 ");
    CoUninitialize();
    std::cout << "PASS: non-Zhuyin literals at idle/caret and candidate selection, queued symbol/space ordering, numpad digits at caret with underline and queued-commit ordering, repeated Ctrl+comma (left/right held Ctrl and event snapshot), repeated candidate selection, FIFO burst (200 commits), English commit ordering, persistent underline, comma caret, Shift+1, discard phonetics, UTF-16 mixed buffer" << std::endl;
    return 0;
}
