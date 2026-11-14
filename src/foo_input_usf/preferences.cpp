/*
 * preferences.cpp - Implements the configuration settings for the USF decoder
 *                   component in a preferences page.
 *
 * Copyright (c) 2023-2025 Jimmy Cassis
 */

#include "pch.h"
#include "preferences.h"
#include "resource.h"
#include "utility.h"
#include "version.h"

void usf_logo_ctrl::OnPaint(CDCHandle dc)
{
    UNREFERENCED_PARAMETER(dc);

    PAINTSTRUCT ps;
    const HDC hdc = BeginPaint(&ps);

    RECT rc;
    GetClientRect(&rc);
    const HBRUSH background = reinterpret_cast<HBRUSH>(::SendMessage(GetParent(), WM_CTLCOLORSTATIC, reinterpret_cast<WPARAM>(hdc), reinterpret_cast<LPARAM>(m_hWnd)));
    if (background != nullptr)
    {
        FillRect(hdc, &rc, background);
    }

    if (m_image != nullptr)
    {
        const UINT iw = m_image->GetWidth();
        const UINT ih = m_image->GetHeight();
        const int cw = rc.right - rc.left;
        const int ch = rc.bottom - rc.top;
        const double sx = static_cast<double>(cw) / iw;
        const double sy = static_cast<double>(ch) / ih;
        const double scale = (sx < sy) ? sx : sy;
        const int dw = static_cast<int>(iw * scale);
        const int dh = static_cast<int>(ih * scale);
        const int dx = rc.left + (cw - dw) / 2;
        const int dy = rc.top + (ch - dh) / 2;

        Gdiplus::Graphics graphics(hdc);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        graphics.DrawImage(m_image, dx, dy, dw, dh);
    }

    EndPaint(&ps);
}

cfg_bool cfg_infinite(guid_cfg_infinite, default_cfg_infinite);
cfg_int cfg_deflength(guid_cfg_deflength, default_cfg_deflength);
cfg_int cfg_deffade(guid_cfg_deffade, default_cfg_deffade);
cfg_bool cfg_suppressopeningsilence(guid_cfg_suppressopeningsilence, default_cfg_suppressopeningsilence);
cfg_bool cfg_suppressendsilence(guid_cfg_suppressendsilence, default_cfg_suppressendsilence);
cfg_int cfg_endsilenceseconds(guid_cfg_endsilenceseconds, default_cfg_endsilenceseconds);
cfg_bool cfg_resample(guid_cfg_resample, default_cfg_resample);
cfg_int cfg_resample_hz(guid_cfg_resample_hz, default_cfg_resample_hz);
cfg_bool cfg_year(guid_cfg_year, default_cfg_year);
cfg_bool cfg_console(guid_cfg_console, default_cfg_console);

#pragma region Preferences Page
BOOL usf_preferences_page::OnInitDialog(CWindow wndFocus, LPARAM lInitParam)
{
    UNREFERENCED_PARAMETER(wndFocus);
    UNREFERENCED_PARAMETER(lInitParam);

    m_dark.AddDialogWithControls(m_hWnd);

    SendDlgItemMessage(IDC_INDEFINITE, BM_SETCHECK, cfg_infinite);
    SendDlgItemMessage(IDC_SOS, BM_SETCHECK, cfg_suppressopeningsilence);
    SendDlgItemMessage(IDC_SES, BM_SETCHECK, cfg_suppressendsilence);
    SendDlgItemMessage(IDC_YEAR, BM_SETCHECK, cfg_year);
    SendDlgItemMessage(IDC_CONSOLE, BM_SETCHECK, cfg_console);
    SendDlgItemMessage(IDC_RESAMPLE, BM_SETCHECK, cfg_resample);

    SetDlgItemInt(IDC_SILENCE, static_cast<UINT>(cfg_endsilenceseconds), FALSE);
    SetDlgItemInt(IDC_RESAMPLE_HZ, static_cast<UINT>(cfg_resample_hz), FALSE);

    char temp[16] = {'\0'};
    //sprintf(temp, "= %d Hz", 33868800 / cfg_divider);
    //SetDlgItemTextA(m_hWnd, IDC_HZ, temp);

    print_time(static_cast<int>(cfg_deflength), temp);
    SetDlgItemTextA(m_hWnd, IDC_DLENGTH, temp);

    print_time(static_cast<int>(cfg_deffade), temp);
    SetDlgItemTextA(m_hWnd, IDC_DFADE, temp);

    GdiplusScope::Once();
    try
    {
        m_logo_image = GdiplusLoadResource(IDB_LOGOPNG, TEXT("PNG"));
        if (m_logo_image)
        {
            m_logo_ctrl.set_image(m_logo_image.get());
            m_logo_ctrl.SubclassWindow(GetDlgItem(IDC_LOGO));
        }
    }
    catch (...)
    {
    }

    //m_link_github.SetLabel(L"USF Source Code Repository"));
    //m_link_github.SetHyperLink(TEXT(APPLICATION_SOURCE_URL));
    //m_link_github.SubclassWindow(GetDlgItem(IDC_URL));

    //OSVERSIONINFO ovi = {sizeof(OSVERSIONINFO)};
    //BOOL bRet = ::GetVersionEx(&ovi);
    //if (bRet && (ovi.dwMajorVersion >= 5))*/
    //{
    //    DWORD color = GetSysColor(COLOR_HOTLIGHT);
    //    m_link_github.m_clrLink = color;
    //    m_link_github.m_clrVisited = color;
    //}

    return FALSE;
}

void usf_preferences_page::OnDestroy()
{
    if (m_logo_ctrl.IsWindow())
    {
        m_logo_ctrl.UnsubclassWindow();
    }
}

void usf_preferences_page::OnEditChange(UINT uNotifyCode, int nID, CWindow wndCtl)
{
    UNREFERENCED_PARAMETER(uNotifyCode);
    UNREFERENCED_PARAMETER(nID);
    UNREFERENCED_PARAMETER(wndCtl);

    OnChanged();
}

void usf_preferences_page::OnButtonClick(UINT uNotifyCode, int nID, CWindow wndCtl)
{
    UNREFERENCED_PARAMETER(uNotifyCode);
    UNREFERENCED_PARAMETER(nID);
    UNREFERENCED_PARAMETER(wndCtl);

    OnChanged();
}

uint32_t usf_preferences_page::get_state()
{
    uint32_t state = preferences_state::resettable;
    state |= preferences_state::dark_mode_supported;
    if (HasChanged())
    {
        state |= preferences_state::changed;
    }
    return state;
}

void usf_preferences_page::reset()
{
    char temp[16];
    SendDlgItemMessage(IDC_INDEFINITE, BM_SETCHECK, default_cfg_infinite);
    SendDlgItemMessage(IDC_SOS, BM_SETCHECK, default_cfg_suppressopeningsilence);
    SendDlgItemMessage(IDC_SES, BM_SETCHECK, default_cfg_suppressendsilence);
    SendDlgItemMessage(IDC_YEAR, BM_SETCHECK, default_cfg_year);
    SendDlgItemMessage(IDC_CONSOLE, BM_GETCHECK, default_cfg_console);
    SendDlgItemMessage(IDC_RESAMPLE, BM_SETCHECK, default_cfg_resample);
    SetDlgItemInt(IDC_SILENCE, default_cfg_endsilenceseconds, FALSE);
    SetDlgItemInt(IDC_RESAMPLE_HZ, default_cfg_resample_hz, FALSE);
    print_time(default_cfg_deflength, reinterpret_cast<char*>(&temp));
    uSetDlgItemText(m_hWnd, IDC_DLENGTH, reinterpret_cast<char*>(&temp));
    print_time(default_cfg_deffade, reinterpret_cast<char*>(&temp));
    uSetDlgItemText(m_hWnd, IDC_DFADE, reinterpret_cast<char*>(&temp));

    OnChanged();
}

void usf_preferences_page::apply()
{
    int t;
    char temp[16];
    cfg_infinite = SendDlgItemMessage(IDC_INDEFINITE, BM_GETCHECK);
    cfg_suppressopeningsilence = SendDlgItemMessage(IDC_SOS, BM_GETCHECK);
    cfg_suppressendsilence = SendDlgItemMessage(IDC_SES, BM_GETCHECK);
    cfg_year = SendDlgItemMessage(IDC_YEAR, BM_GETCHECK);
    cfg_console = SendDlgItemMessage(IDC_CONSOLE, BM_GETCHECK);
    cfg_resample = SendDlgItemMessage(IDC_RESAMPLE, BM_GETCHECK);

    t = GetDlgItemInt(IDC_SILENCE, NULL, FALSE);
    if (t > 0)
    {
        cfg_endsilenceseconds = t;
    }
    t = GetDlgItemInt(IDC_RESAMPLE_HZ, NULL, FALSE);
    if (t < 8000)
    {
        t = 8000;
    }
    else if (t > 192000)
    {
        t = 192000;
    }
    cfg_resample_hz = t;
    SetDlgItemInt(IDC_RESAMPLE_HZ, t, FALSE);
    SetDlgItemInt(IDC_SILENCE, static_cast<UINT>(cfg_endsilenceseconds), FALSE);
    t = parse_time(uGetDlgItemText(m_hWnd, IDC_DLENGTH).c_str());
    if (t != BORK_TIME)
    {
        cfg_deflength = t;
    }
    else
    {
        print_time(static_cast<int>(cfg_deflength), temp);
        uSetDlgItemText(m_hWnd, IDC_DLENGTH, temp);
    }
    t = parse_time(uGetDlgItemText(m_hWnd, IDC_DFADE).c_str());
    if (t != BORK_TIME)
    {
        cfg_deffade = t;
    }
    else
    {
        print_time(static_cast<int>(cfg_deffade), temp);
        uSetDlgItemText(m_hWnd, IDC_DFADE, temp);
    }

    // The dialog content has not changed but the flags have;
    // the currently shown values now match the settings so the
    // "Apply" button can be disabled.
    OnChanged();
}

// Returns whether the dialog content is different from the current configuration;
// whether the "Apply" button should be enabled or not.
bool usf_preferences_page::HasChanged()
{
    bool changed = false;
    if (!changed && SendDlgItemMessage(IDC_INDEFINITE, BM_GETCHECK) != cfg_infinite)
        changed = true;
    if (!changed && SendDlgItemMessage(IDC_SOS, BM_GETCHECK) != cfg_suppressopeningsilence)
        changed = true;
    if (!changed && SendDlgItemMessage(IDC_SES, BM_GETCHECK) != cfg_suppressendsilence)
        changed = true;
    if (!changed && SendDlgItemMessage(IDC_YEAR, BM_GETCHECK) != cfg_year)
        changed = true;
    if (!changed && SendDlgItemMessage(IDC_CONSOLE, BM_GETCHECK) != cfg_console)
        changed = true;
    if (!changed && SendDlgItemMessage(IDC_RESAMPLE, BM_GETCHECK) != cfg_resample)
        changed = true;
    if (!changed && GetDlgItemInt(IDC_SILENCE, NULL, FALSE) != static_cast<UINT>(cfg_endsilenceseconds))
        changed = true;
    if (!changed && GetDlgItemInt(IDC_RESAMPLE_HZ, NULL, FALSE) != static_cast<UINT>(cfg_resample_hz))
        changed = true;
    if (!changed)
    {
        const unsigned int t = parse_time(uGetDlgItemText(m_hWnd, IDC_DLENGTH).c_str());
        if (t != BORK_TIME && t != static_cast<unsigned int>(cfg_deflength))
            changed = true;
    }
    if (!changed)
    {
        const unsigned int t = parse_time(uGetDlgItemText(m_hWnd, IDC_DFADE).c_str());
        if (t != BORK_TIME && t != static_cast<unsigned int>(cfg_deffade))
            changed = true;
    }
    return changed;
}

void usf_preferences_page::OnChanged()
{
    m_callback->on_state_changed();
}

class preferences_page_usf : public preferences_page_v4
{
  public:
    preferences_page_usf() = default;

    ~preferences_page_usf() = default;

    preferences_page_usf(const preferences_page_usf&) = delete;
    preferences_page_usf& operator=(const preferences_page_usf&) = delete;
    preferences_page_usf(preferences_page_usf&&) = delete;
    preferences_page_usf& operator=(preferences_page_usf&&) = delete;

    const char* get_name() override { return APPLICATION_PRODUCT_NAME; }
    GUID get_guid() override { return guid_usf_prefs; }
    GUID get_parent_guid() override { return guid_input; }
    bool get_help_url(pfc::string_base& p_out) override
    {
        p_out.reset();
        p_out << "https://wiki.hydrogenaudio.org/index.php?title=Foobar2000:Components/USF_Decoder_(foo_input_usf)";
        return true;
    }
    preferences_page_instance::ptr instantiate(HWND parent, preferences_page_callback::ptr callback) override
    {
        return fb2k::service_new_window<preferences_page_instance_impl<usf_preferences_page>>(parent, callback);
    }
};

static preferences_page_factory_t<preferences_page_usf> g_config_usf_factory;
#pragma endregion
