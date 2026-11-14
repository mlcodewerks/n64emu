// Minimal SDK host: exercises the actual component DLL without a foobar install.
#include "../pch.h"
#include <SDK/foobar2000.h>
#include <SDK/component.h>
#include <SDK/file_info_impl.h>
#include <cstdio>
#include <map>
#include <vector>
#include <stdexcept>
#include <cmath>
#include "../preferences.h"

extern "C" foobar2000_client* foobar2000_get_interface(foobar2000_api*, HINSTANCE);

static void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

class local_file : public file_readonly {
    FILE* handle;
public:
    explicit local_file(const char* path) : handle(nullptr) {
        if (strncmp(path, "file://", 7) == 0) path += 7;
        fopen_s(&handle, path, "rb");
        if (!handle) throw exception_io_not_found();
    }
    ~local_file() { fclose(handle); }
    size_t read(void* out, size_t bytes, abort_callback& abort) override {
        abort.check();
        size_t count = fread(out, 1, bytes, handle);
        if (ferror(handle)) throw exception_io_data();
        return count;
    }
    t_filesize get_size(abort_callback&) override {
        auto pos = _ftelli64(handle);
        _fseeki64(handle, 0, SEEK_END);
        auto size = _ftelli64(handle);
        _fseeki64(handle, pos, SEEK_SET);
        return size;
    }
    t_filesize get_position(abort_callback&) override { return _ftelli64(handle); }
    void seek(t_filesize pos, abort_callback&) override {
        if (_fseeki64(handle, pos, SEEK_SET)) throw exception_io_seek_out_of_range();
    }
    void reopen(abort_callback& abort) override { seek(0, abort); }
    bool can_seek() override { return true; }
    bool get_content_type(pfc::string_base&) override { return false; }
    bool is_remote() override { return false; }
};

class local_filesystem : public filesystem {
public:
    bool get_canonical_path(const char* path, pfc::string_base& out) override { out = path; return true; }
    bool get_display_path(const char* path, pfc::string_base& out) override { out = path; return true; }
    bool is_our_path(const char*) override { return true; }
    bool is_remote(const char*) override { return false; }
    bool supports_content_types() override { return false; }
    void open(file::ptr& out, const char* path, t_open_mode mode, abort_callback&) override {
        if (mode != open_mode_read) throw exception_io_denied();
        out = new service_impl_t<local_file>(path);
    }
    void get_stats(const char* path, t_filestats& stats, bool& writable, abort_callback& abort) override {
        file::ptr f; open(f, path, open_mode_read, abort);
        stats = f->get_stats(abort); writable = false;
    }
    void remove(const char*, abort_callback&) override { throw exception_io_denied(); }
    void move(const char*, const char*, abort_callback&) override { throw exception_io_denied(); }
    void create_directory(const char*, abort_callback&) override { throw exception_io_denied(); }
    void list_directory(const char*, directory_callback&, abort_callback&) override { throw exception_io_denied(); }
};
static service_factory_single_t<local_filesystem> filesystem_factory;

struct guid_less {
    bool operator()(const GUID& a, const GUID& b) const { return memcmp(&a, &b, sizeof(a)) < 0; }
};
class test_api : public foobar2000_api {
    using factories = std::vector<service_factory_base*>;
    std::map<GUID, factories, guid_less> classes;
public:
    void add(service_factory_base* factory) {
        for (; factory; factory = factory->__internal__next)
            classes[factory->get_class_guid()].push_back(factory);
    }
    service_class_ref service_enum_find_class(const GUID& id) override { return &classes[id]; }
    size_t service_enum_get_count(service_class_ref ref) override { return static_cast<const factories*>(ref)->size(); }
    bool service_enum_create(service_ptr& out, service_class_ref ref, size_t index) override {
        auto& list = *static_cast<const factories*>(ref);
        if (index >= list.size()) return false;
        list[index]->instance_create(out); return true;
    }
    HWND get_main_window() override { return nullptr; }
    bool assert_main_thread() override { return true; }
    bool is_main_thread() override { return true; }
    bool is_shutting_down() override { return false; }
    const char* get_profile_path() override { return "file://C:\\"; }
    bool is_initializing() override { return false; }
    bool is_portable_mode_enabled() override { return true; }
    bool is_quiet_mode_enabled() override { return true; }
};

// Legacy SDK configuration stream: GUID, uint32 size, little-endian payload.
static void configure(foobar2000_client* client, bool resample) {
    std::vector<uint8_t> data;
    auto setting = [&](const GUID& id, const auto& value) {
        auto bytes = [&](const void* ptr, size_t size) {
            auto start = static_cast<const uint8_t*>(ptr);
            data.insert(data.end(), start, start + size);
        };
        uint32_t size = sizeof(value);
        bytes(&id, sizeof(id)); bytes(&size, sizeof(size)); bytes(&value, size);
    };
    setting(guid_cfg_suppressopeningsilence, false);
    setting(guid_cfg_suppressendsilence, false);
    setting(guid_cfg_resample, resample);
    setting(guid_cfg_resample_hz, int32_t(48000));
    stream_reader_memblock_ref stream(data.data(), data.size());
    client->set_config(&stream, fb2k::noAbort);
}

static std::vector<audio_sample> render(input_decoder::ptr decoder, size_t frames, unsigned rate) {
    std::vector<audio_sample> pcm;
    audio_chunk_impl chunk;
    while (pcm.size() < frames * 2) {
        require(decoder->run(chunk, fb2k::noAbort), "Unexpected EOF");
        require(chunk.get_srate() == rate && chunk.get_channels() == 2, "Invalid PCM format");
        size_t samples = std::min(chunk.get_sample_count() * 2, frames * 2 - pcm.size());
        pcm.insert(pcm.end(), chunk.get_data(), chunk.get_data() + samples);
    }
    return pcm;
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    try {
        require(argc == 2 || argc == 3, "usage: component_smoke component.dll [track.miniusf]");
        test_api api;
        auto host = foobar2000_get_interface(&api, GetModuleHandleW(nullptr));
        api.add(host->get_service_list());
        host->services_init(true);
        // The test executable directory supplies SDK shared.dll. Parallax must
        // be resolved beside the component by its own delay-load hook.
        HMODULE module = LoadLibraryExA(argv[1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!module) {
            fprintf(stderr, "LoadLibrary error: %lu\n", GetLastError());
            throw std::runtime_error("Cannot load component");
        }
        auto entry = reinterpret_cast<decltype(&foobar2000_get_interface)>(GetProcAddress(module, "foobar2000_get_interface"));
        require(entry != nullptr, "Missing component entry point");
        auto client = entry(&api, module);
        client->set_library_path(argv[1], "foo_input_usf.dll");
        api.add(client->get_service_list());
        client->services_init(true);
        input_entry::ptr input;
        require(input_entry::enumerate().first(input), "Missing input service");
        const char* track = argc == 3 ? argv[2] : "track.miniusf";
        require(input->is_our_path(track, "miniusf") && input->is_our_path("track.usf", "usf"), "USF file registration failed");
        require(!input->is_our_path("track.mp3", "mp3"), "Unexpected format registration");
        if (argc == 2) {
            printf("PASS component load, entry point, input service and USF/miniUSF registration\n");
            return 0;
        }
        for (bool resample : {false, true}) {
                configure(client, resample);
                input_decoder::ptr decoder;
                input->open_for_decoding(decoder, nullptr, argv[2], fb2k::noAbort);
                file_info_impl info;
                decoder->get_info(0, info, fb2k::noAbort);
                unsigned rate = resample ? 48000 : 22018;
                require(info.meta_get("title", 0) && strcmp(info.meta_get("title", 0), "Air Force One") == 0, "Title metadata mismatch");
                require(std::abs(info.get_length() - 355.0) < 0.001, "Duration metadata mismatch");
                decoder->initialize(0, input_flag_no_looping, fb2k::noAbort);
                auto sequential = render(decoder, rate * 3, rate);
                require(std::any_of(sequential.begin(), sequential.end(), [](auto s) { return s != 0; }), "Silent PCM");
                decoder->seek(2, fb2k::noAbort);
                auto window = render(decoder, rate, rate);
                require(std::equal(window.begin(), window.end(), sequential.begin() + rate * 4), "Backward seek PCM mismatch");
                printf("PASS component load, miniUSF/usflib, metadata, stereo PCM, backward seek: rate=%u\n", rate);
        }
        return 0;
    } catch (const std::exception& e) {
        fprintf(stderr, "FAIL: %s\n", e.what()); return 1;
    }
}
