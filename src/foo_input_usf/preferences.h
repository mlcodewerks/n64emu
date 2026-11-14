/*
 * preferences.h - Describes the configuration settings for the USF decoder
 *                 component.
 *
 * Copyright (c) 2026 Jimmy Cassis
 */

#pragma once

#include "resource.h"

constexpr GUID guid_usf = {
    0x9b1ccec3, 0x1da6, 0x40f9, {0x93, 0x33, 0x56, 0x42, 0x9d, 0xe6, 0xd9, 0x60}
}; // {9B1CCEC3-1DA6-40F9-9333-56429DE6D960}

constexpr GUID guid_usf_prefs = {
    0x7aad88ee, 0x38eb, 0x456c, {0xb7, 0x29, 0x50, 0x1, 0x2, 0x16, 0x64, 0xa0}
}; // {7AAD88EE-38EB-456C-B729-5001021664A0}

constexpr GUID guid_usf_tag_length = {
    0x8d0b14b6, 0xe331, 0x404a, {0x90, 0x4f, 0xdd, 0x10, 0x3a, 0x5a, 0x47, 0xb0}
}; // {8D0B14B6-E331-404A-904F-DD103A5A47B0}

constexpr GUID guid_cfg_infinite = {
    0x4704b0d3, 0x5695, 0x42d1, {0x9f, 0x20, 0x7, 0x94, 0xdc, 0x5a, 0xdd, 0x21}
}; // {4704B0D3-5695-42D1-9F20-0794DC5ADD21}

constexpr GUID guid_cfg_deflength = {
    0x492e2289, 0x284e, 0x4571, {0xb1, 0x3c, 0x71, 0x74, 0xf1, 0x18, 0xb8, 0x40}
}; // {492E2289-284E-4571-B13C-7174F118B840}

constexpr GUID guid_cfg_deffade = {
    0xe573114, 0xa35e, 0x45a6, {0x88, 0xaf, 0x9d, 0x71, 0x54, 0x51, 0xf, 0x36}
}; // {0E573114-A35E-45A6-88AF-9D7154510F36}

constexpr GUID guid_cfg_suppressopeningsilence = {
    0x24f5e4a9, 0x2ae1, 0x46b3, {0x9a, 0x58, 0x25, 0x62, 0x3a, 0x1d, 0xfa, 0x1e}
}; // {24F5E4A9-2AE1-46B3-9A58-25623A1DFA1E}

constexpr GUID guid_cfg_suppressendsilence = {
    0xe89022d7, 0x87e9, 0x4f78, {0x9b, 0x60, 0x6c, 0xa1, 0x95, 0x50, 0xd, 0x55}
}; // {E89022D7-87E9-4F78-9B60-6CA195500D55}

constexpr GUID guid_cfg_endsilenceseconds = {
    0x81dfca33, 0x64e4, 0x4cd6, {0x97, 0x6c, 0xd8, 0x7b, 0xc7, 0xa3, 0x48, 0x7c}
}; // {81DFCA33-64E4-4CD6-976C-D87BC7A3487C}

constexpr GUID guid_cfg_resample = {
    0x3bb2f74d, 0xdffb, 0x4b46, {0x89, 0xd4, 0x9a, 0x4f, 0xdf, 0xc6, 0x6d, 0x33}
}; // {3BB2F74D-DFFB-4B46-89D4-9A4FDFC66D33}

constexpr GUID guid_cfg_resample_hz = {
    0x6a1f11b8, 0x52b2, 0x4133, {0xb7, 0x47, 0x83, 0xf7, 0x4b, 0xf2, 0x7a, 0x3d}
}; // {6A1F11B8-52B2-4133-B747-83F74BF27A3D}

constexpr GUID guid_cfg_year = {
    0x5092a43b, 0xe833, 0x423e, {0xa2, 0x7e, 0x81, 0x69, 0x69, 0x70, 0x5c, 0x20}
}; // {5092A43B-E833-423E-A27E-816969705C20}

constexpr GUID guid_cfg_console = {
    0xb362de6f, 0xdea9, 0x41ef, {0x9d, 0x4c, 0xd4, 0x2b, 0xbd, 0x17, 0x97, 0x57}
}; // {B362DE6F-DEA9-41EF-9D4C-D42BBD179757}

//constexpr GUID guid_cfg_placement = {
//    0xb6b4a385, 0x7f56, 0x4558, {0xb6, 0x30, 0x78, 0xeb, 0x95, 0x28, 0x0, 0x34}
//}; // {B6B4A385-7F56-4558-B630-78EB95280034}

constexpr bool default_cfg_infinite = false;
constexpr int default_cfg_deflength = 170000;
constexpr int default_cfg_deffade = 10000;
constexpr bool default_cfg_suppressopeningsilence = true;
constexpr bool default_cfg_suppressendsilence = true;
constexpr int default_cfg_endsilenceseconds = 10;
constexpr bool default_cfg_resample = true;
constexpr int default_cfg_resample_hz = 44100;
constexpr bool default_cfg_year = false;
constexpr bool default_cfg_console = false;

constexpr const char field_length[] = "usf_length";
constexpr const char field_fade[] = "usf_fade";

namespace Gdiplus
{
class Bitmap;
class Image;
} // namespace Gdiplus

// Static control that paints a PNG with alpha over the dialog background.
class usf_logo_ctrl : public CWindowImpl<usf_logo_ctrl>
{
  public:
    usf_logo_ctrl() : m_image(nullptr) {}
    usf_logo_ctrl(const usf_logo_ctrl&) = delete;
    usf_logo_ctrl& operator=(const usf_logo_ctrl&) = delete;
    usf_logo_ctrl(usf_logo_ctrl&&) = delete;
    usf_logo_ctrl& operator=(usf_logo_ctrl&&) = delete;

    ~usf_logo_ctrl() = default;

    void set_image(Gdiplus::Image* image) { m_image = image; }

    BEGIN_MSG_MAP_EX(usf_logo_ctrl)
        MSG_WM_ERASEBKGND(OnEraseBkgnd)
        MSG_WM_PAINT(OnPaint)
    END_MSG_MAP()

  private:
    BOOL OnEraseBkgnd(CDCHandle dc) { UNREFERENCED_PARAMETER(dc); return TRUE; }
    void OnPaint(CDCHandle dc);

    Gdiplus::Image* m_image;
};

#pragma region Preferences Page
class usf_preferences_page : public CDialogImpl<usf_preferences_page>, public preferences_page_instance
{
  public:
    usf_preferences_page(preferences_page_callback::ptr callback) : m_callback(callback), m_bMsgHandled(FALSE) {}
    usf_preferences_page(const usf_preferences_page&) = delete;
    usf_preferences_page& operator=(const usf_preferences_page&) = delete;
    usf_preferences_page(usf_preferences_page&&) = delete;
    usf_preferences_page& operator=(usf_preferences_page&&) = delete;

    ~usf_preferences_page() = default;

    enum
    {
        IDD = IDD_USF_CONFIG
    };

    uint32_t get_state() override;
    void apply() override;
    void reset() override;

    BEGIN_MSG_MAP_EX(usf_preferences_page)
        MSG_WM_INITDIALOG(OnInitDialog)
        COMMAND_HANDLER_EX(IDC_INDEFINITE, BN_CLICKED, OnButtonClick)
        COMMAND_HANDLER_EX(IDC_SOS, BN_CLICKED, OnButtonClick)
        COMMAND_HANDLER_EX(IDC_SES, BN_CLICKED, OnButtonClick)
        COMMAND_HANDLER_EX(IDC_YEAR, BN_CLICKED, OnButtonClick)
        COMMAND_HANDLER_EX(IDC_CONSOLE, BN_CLICKED, OnButtonClick)
        COMMAND_HANDLER_EX(IDC_RESAMPLE, BN_CLICKED, OnButtonClick)
        COMMAND_HANDLER_EX(IDC_SILENCE, EN_CHANGE, OnEditChange)
        COMMAND_HANDLER_EX(IDC_DLENGTH, EN_CHANGE, OnEditChange)
        COMMAND_HANDLER_EX(IDC_RESAMPLE_HZ, EN_CHANGE, OnEditChange)
        COMMAND_HANDLER_EX(IDC_DFADE, EN_CHANGE, OnEditChange)
        MSG_WM_DESTROY(OnDestroy)
    END_MSG_MAP()

  private:
    BOOL OnInitDialog(CWindow wndFocus, LPARAM lInitParam);
    void OnDestroy();
    void OnEditChange(UINT uNotifyCode, int nID, CWindow wndCtl);
    void OnButtonClick(UINT uNotifyCode, int nID, CWindow wndCtl);
    bool HasChanged();
    void OnChanged();

    const preferences_page_callback::ptr m_callback;
    fb2k::CCoreDarkModeHooks m_dark;

    std::unique_ptr<Gdiplus::Bitmap> m_logo_image;
    usf_logo_ctrl m_logo_ctrl;

    //CHyperLink m_link_github;
};
#pragma endregion
