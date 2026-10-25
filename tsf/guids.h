#pragma once

#include <windows.h>
#include <msctf.h>

#ifndef TF_CLIENTID_NULL
#define TF_CLIENTID_NULL ((TfClientId)0)
#endif

namespace eime {

// {D8E42B10-7A5C-4E91-9B2A-1F3C5D7E9A01}
static constexpr GUID CLSID_EIME_TextService = {
    0xd8e42b10, 0x7a5c, 0x4e91, {0x9b, 0x2a, 0x1f, 0x3c, 0x5d, 0x7e, 0x9a, 0x01}
};

// {D8E42B11-7A5C-4E91-9B2A-1F3C5D7E9A01}
static constexpr GUID GUID_EIME_Profile = {
    0xd8e42b11, 0x7a5c, 0x4e91, {0x9b, 0x2a, 0x1f, 0x3c, 0x5d, 0x7e, 0x9a, 0x01}
};

// GUID_TFCAT_TIPCAP_SECUREMODE: {49D2F9CE-1F5E-11D7-A6D3-00065B84435C}
static constexpr GUID EIME_GUID_TFCAT_TIPCAP_SECUREMODE = {
    0x49d2f9ce, 0x1f5e, 0x11d7, {0xa6, 0xd3, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c}
};

// GUID_TFCAT_TIPCAP_UIELEMENTENABLED: {49D2F9CF-1F5E-11D7-A6D3-00065B84435C}
static constexpr GUID EIME_GUID_TFCAT_TIPCAP_UIELEMENTENABLED = {
    0x49d2f9cf, 0x1f5e, 0x11d7, {0xa6, 0xd3, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c}
};

// GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT: {13A016DF-560B-46CD-947A-4C3AF1E0E35D}
static constexpr GUID EIME_GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT = {
    0x13a016df, 0x560b, 0x46cd, {0x94, 0x7a, 0x4c, 0x3a, 0xf1, 0xe0, 0xe3, 0x5d}
};

// GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT: {25504FB4-7BAB-4BC1-9C69-CF81890F0EF5}
static constexpr GUID EIME_GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT = {
    0x25504fb4, 0x7bab, 0x4bc1, {0x9c, 0x69, 0xcf, 0x81, 0x89, 0x0f, 0x0e, 0xf5}
};

// 顯示屬性 (Display Attributes): 組字實線底線 (Input)
// {D8E42B12-7A5C-4E91-9B2A-1F3C5D7E9A01}
static constexpr GUID GUID_EIME_DisplayAttr_Input = {
    0xd8e42b12, 0x7a5c, 0x4e91, {0x9b, 0x2a, 0x1f, 0x3c, 0x5d, 0x7e, 0x9a, 0x01}
};

// 顯示屬性 (Display Attributes): 當前選字/目標詞粗實線底線 (Target)
// {D8E42B13-7A5C-4E91-9B2A-1F3C5D7E9A01}
static constexpr GUID GUID_EIME_DisplayAttr_Target = {
    0xd8e42b13, 0x7a5c, 0x4e91, {0x9b, 0x2a, 0x1f, 0x3c, 0x5d, 0x7e, 0x9a, 0x01}
};

constexpr LANGID kTaiwanChineseLangId = MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL); // 0x0404
constexpr const wchar_t* kEimeDisplayName = L"E-IME (改進型注音)";

} // namespace eime
