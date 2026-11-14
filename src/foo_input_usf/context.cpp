/*
 * context.cpp - Implements the context menu entry and time dialog.
 *
 * Copyright (c) 2025-2026 Jimmy Cassis
 */

#include "pch.h"
#include "preferences.h"
#include "resource.h"
#include "utility.h"

typedef struct
{
    unsigned int song, fade;
} INFOSTRUCT;

#pragma region Time/Length Dialog
class TimeDlg : public CDialogImpl<TimeDlg>
{
  public:
    explicit TimeDlg(INFOSTRUCT& info) : m_info(info), m_bMsgHandled(FALSE) {}
    TimeDlg(const TimeDlg&) = delete;
    TimeDlg& operator=(const TimeDlg&) = delete;
    TimeDlg(TimeDlg&&) = delete;
    TimeDlg& operator=(TimeDlg&&) = delete;

    ~TimeDlg() = default;

    enum time_dialog_id : uint8_t
    {
        IDD = IDD_TIME
    };

    BEGIN_MSG_MAP_EX(TimeDlg)
        MSG_WM_INITDIALOG(OnInitDialog)
        MSG_WM_DESTROY(OnDestroy)
        COMMAND_ID_HANDLER(IDOK, OnOK)
        COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
    END_MSG_MAP()

  private:
    INFOSTRUCT& m_info;
    fb2k::CCoreDarkModeHooks m_dark_mode;

    BOOL OnInitDialog(CWindow wndFocus, LPARAM lInitParam)
    {
        UNREFERENCED_PARAMETER(wndFocus);
        UNREFERENCED_PARAMETER(lInitParam);

        CHAR temp[16];
        if (!m_info.song && !m_info.fade)
        {
            SetWindowText(L"Set length:");
        }
        else
        {
            SetWindowText(L"Edit length:");
        }
        if (m_info.song != UINT_MAX)
        {
            print_time(m_info.song, temp);
            SetDlgItemText(IDC_LENGTH, pfc::stringcvt::string_wide_from_ansi(temp, 16));
        }
        if (m_info.fade != UINT_MAX)
        {
            print_time(m_info.fade, temp);
            SetDlgItemText(IDC_FADE, pfc::stringcvt::string_wide_from_ansi(temp, 16));
        }
        //cfg_placement.on_window_creation(wnd, true);

        m_dark_mode.AddDialogWithControls(m_hWnd);

        return TRUE;
    }

    LRESULT OnOK(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
    {
        UNREFERENCED_PARAMETER(wNotifyCode);
        UNREFERENCED_PARAMETER(wID);
        UNREFERENCED_PARAMETER(hWndCtl);
        UNREFERENCED_PARAMETER(bHandled);

        UINT time = BORK_TIME;
        WCHAR buf[16] = {0};

        GetDlgItemText(IDC_LENGTH, buf, ARRAYSIZE(buf));
        time = parse_time(pfc::stringcvt::string_ansi_from_wide(buf, 16));
        if (time != BORK_TIME)
        {
            m_info.song = time;
        }
        else
        {
            m_info.song = UINT_MAX;
        }

        GetDlgItemText(IDC_FADE, buf, ARRAYSIZE(buf));
        time = parse_time(pfc::stringcvt::string_ansi_from_wide(buf, 16));
        if (time != BORK_TIME)
        {
            m_info.fade = time;
        }
        else
        {
            m_info.fade = UINT_MAX;
        }

        EndDialog(IDOK);
        return 0;
    }

    LRESULT OnCancel(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
    {
        UNREFERENCED_PARAMETER(wNotifyCode);
        UNREFERENCED_PARAMETER(wID);
        UNREFERENCED_PARAMETER(hWndCtl);
        UNREFERENCED_PARAMETER(bHandled);

        EndDialog(IDCANCEL);
        return 0;
    }

    void OnDestroy()
    {
        //cfg_placement.on_window_destruction(wnd);
    }
};

static bool context_time_dialog(unsigned int* song_ms, unsigned int* fade_ms)
{
    INFOSTRUCT i;
    i.song = *song_ms;
    i.fade = *fade_ms;
    const HWND hwnd = core_api::get_main_window();
    TimeDlg dlg(i);
    const INT_PTR ret = dlg.DoModal(hwnd);
    if (ret == IDOK)
    {
        *song_ms = i.song;
        *fade_ms = i.fade;
        return true;
    }
    return false;
}
#pragma endregion

#pragma region Tag Update
class length_info_filter : public file_info_filter_v2
{
  public:
    length_info_filter(const pfc::list_base_const_t<metadb_handle_ptr>& p_list)
    {
        set_length = false;
        set_fade = false;
        m_length = 0;
        m_fade = 0;

        pfc::array_t<size_t> order;
        order.set_size(p_list.get_count());
        order_helper::g_fill(order.get_ptr(), order.get_size());
        p_list.sort_get_permutation_t(pfc::compare_t<metadb_handle_ptr, metadb_handle_ptr>, order.get_ptr());
        m_handles.set_count(order.get_size());
        for (size_t n = 0; n < order.get_size(); ++n)
        {
            m_handles[n] = p_list[order[n]];
        }
    }

    ~length_info_filter() = default;

    length_info_filter(const length_info_filter&) = delete;
    length_info_filter& operator=(const length_info_filter&) = delete;
    length_info_filter(length_info_filter&&) = delete;
    length_info_filter& operator=(length_info_filter&&) = delete;

    void length(unsigned int p_length)
    {
        set_length = true;
        m_length = p_length;
    }

    void fade(unsigned int p_fade)
    {
        set_fade = true;
        m_fade = p_fade;
    }

    bool apply_filter(trackRef p_location, t_filestats p_stats, file_info& p_info) override
    {
        UNREFERENCED_PARAMETER(p_stats);

        size_t index;
        if (m_handles.bsearch_t(pfc::compare_t<metadb_handle_ptr, metadb_handle_ptr>, p_location, index))
        {
            if (set_length)
            {
                if (m_length > 0)
                {
                    p_info.info_set_int(field_length, m_length);
                }
                else
                {
                    p_info.info_remove(field_length);
                }
            }
            if (set_fade)
            {
                if (m_fade > 0)
                {
                    p_info.info_set_int(field_fade, m_fade);
                }
                else
                {
                    p_info.info_remove(field_fade);
                }
            }
            return set_length || set_fade;
        }
        else
        {
            return false;
        }
    }

    filterStatus_t before_tag_update(const char* location, file::ptr fileIfAlreadyOpened, abort_callback& aborter) override
    {
        UNREFERENCED_PARAMETER(location);
        UNREFERENCED_PARAMETER(fileIfAlreadyOpened);
        UNREFERENCED_PARAMETER(aborter);

        return filterProceed;
    }

    void after_tag_update(const char* location, service_ptr_t<input_info_reader> reader, abort_callback& aborter) override
    {
        UNREFERENCED_PARAMETER(location);
        UNREFERENCED_PARAMETER(reader);
        UNREFERENCED_PARAMETER(aborter);
    }

    void after_all_tag_updates(abort_callback& aborter) override
    {
        UNREFERENCED_PARAMETER(aborter);
    }

    bool filter_error(const char* location, const char* msg) override
    {
        UNREFERENCED_PARAMETER(location);
        UNREFERENCED_PARAMETER(msg);

        return false;
    }

  private:
    bool set_length, set_fade;
    unsigned int m_length, m_fade;

    metadb_handle_list m_handles;
};
#pragma endregion

#pragma region Tagging Context Menu
class context_usf : public contextmenu_item_simple
{
  public:
    context_usf() = default;

    ~context_usf() = default;

    context_usf(const context_usf&) = delete;
    context_usf& operator=(const context_usf&) = delete;
    context_usf(context_usf&&) = delete;
    context_usf& operator=(context_usf&&) = delete;

    unsigned int get_num_items() override
    {
        return 1;
    }

    void get_item_name(unsigned int p_index, pfc::string_base& p_out) override
    {
        if (p_index > 0)
            uBugCheck();
        p_out = "Modify USF tag length...";
    }

    GUID get_parent() override
    {
        return contextmenu_groups::tagging;
    }

    bool get_item_description(unsigned int p_index, pfc::string_base& p_out) override
    {
        if (p_index > 0)
            uBugCheck();
        p_out = "Edits the length of the selected USF file, or sets the length of all selected USF files.";
        return true;
    }

    GUID get_item_guid(unsigned int p_index) override
    {
        if (p_index > 0)
            uBugCheck();
        return guid_usf_tag_length;
    }

    bool context_get_display(unsigned int p_index, metadb_handle_list_cref p_data, pfc::string_base& p_out, unsigned int& p_displayflags, const GUID& p_caller) override
    {
        UNREFERENCED_PARAMETER(p_displayflags);
        UNREFERENCED_PARAMETER(p_caller);

        if (p_index > 0)
            uBugCheck();
        const size_t i = p_data.get_count();
        for (size_t j = 0; j < i; ++j)
        {
            pfc::string ext = pfc::string_extension(p_data.get_item(j)->get_path());
            if (pfc::stricmp_ascii(ext, "USF") != 0 && pfc::stricmp_ascii(ext, "MINIUSF") != 0)
                return false;
        }
        if (i == 1)
        {
            p_out = "Edit length...";
        }
        else
        {
            p_out = "Set length...";
        }
        return true;
    }

    void context_command(unsigned int p_index, metadb_handle_list_cref p_data, const GUID& p_caller) override
    {
        UNREFERENCED_PARAMETER(p_caller);

        if (p_index > 0)
            uBugCheck();
        unsigned int tag_song_ms = UINT_MAX, tag_fade_ms = UINT_MAX;
        const size_t i = p_data.get_count();
        if (i == 1)
        {
            // Fetch information from single file.
            const metadb_handle_ptr handle = p_data.get_item(0);
            service_ptr_t<metadb_info_container> p_info_container;
            if (handle->get_async_info_ref(p_info_container))
            {
                const file_info& p_info = p_info_container->info();
                const char* t = p_info.info_get(field_length);
                if (t)
                {
                    tag_song_ms = atoi(t);
                }
                t = p_info.info_get(field_fade);
                if (t)
                {
                    tag_fade_ms = atoi(t);
                }
            }
        }
        if (!context_time_dialog(&tag_song_ms, &tag_fade_ms))
            return;

        service_ptr_t<length_info_filter> p_filter = new service_impl_t<length_info_filter>(p_data);
        if (tag_song_ms != UINT_MAX)
        {
            p_filter->length(tag_song_ms);
        }
        if (tag_fade_ms != UINT_MAX)
        {
            p_filter->fade(tag_fade_ms);
        }

        static_api_ptr_t<metadb_io_v5> p_imgr;
        p_imgr->update_info_async(p_data, p_filter, core_api::get_main_window(), 0, nullptr);
    }
};

static contextmenu_item_factory_t<context_usf> g_contextmenu_item_usf_factory;
#pragma endregion