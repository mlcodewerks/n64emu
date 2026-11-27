#pragma once
#include "dsp_chain.h"
#include "rom_browser.h"

class dsp_chain_window {
public:
    void draw_settings(dsp_chain &chain, const std::filesystem::path &directory);
    void draw_picker(bool audio_tab);
private:
    struct editor {
        bool initialized = false;
        bool dirty = false;
        dsp_settings draft;
        std::string status;
        int add_filter = 0;
    } edit;
    RomBrowser picker;
    bool picker_save = false;
};
