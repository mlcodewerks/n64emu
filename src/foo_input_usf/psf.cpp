/*
 * foo_input_usf.cpp - Implements the USF decoder behavior.
 *
 * Copyright (c) 2025-2026 Jimmy Cassis
 */

#include "pch.h"
#include "circular_buffer.h"
#include "preferences.h"
#include "utility.h"
#include "version.h"

extern cfg_bool cfg_infinite;
extern cfg_int cfg_deflength;
extern cfg_int cfg_deffade;
extern cfg_bool cfg_suppressopeningsilence;
extern cfg_bool cfg_suppressendsilence;
extern cfg_int cfg_endsilenceseconds;
extern cfg_bool cfg_resample;
extern cfg_int cfg_resample_hz;
extern cfg_bool cfg_year;
extern cfg_bool cfg_console;

constexpr unsigned int CHANNELS = 2;
constexpr unsigned int SAMPLE_BUFFER_SIZE = 1024;
constexpr unsigned int BYTES_PER_SAMPLE = sizeof(int16_t);
constexpr unsigned int BITS_PER_SAMPLE = 16;
constexpr unsigned int BITS_PER_KILOBIT = 1000;

namespace
{
#pragma region Metadata
void info_meta_ansi(file_info& info)
{
    const size_t j = info.meta_get_count();
    for (size_t i = 0; i < j; ++i)
    {
        const size_t l = info.meta_enum_value_count(i);
        for (size_t k = 0; k < l; ++k)
        {
            const char* value = info.meta_enum_value(i, k);
            info.meta_modify_value(i, k, pfc::stringcvt::string_utf8_from_ansi(value));
        }
    }

    const size_t m = info.info_get_count();
    for (size_t i = 0; i < m; ++i)
    {
        const char* name = info.info_enum_name(i);
        if (name[0] == '_')
        {
            info.info_set(pfc::string(name), pfc::stringcvt::string_utf8_from_ansi(info.info_enum_value(i)));
        }
    }
}

int find_crlf(const pfc::string& str)
{
    const size_t pos = str.find_first('\r');
    if (pos != SIZE_MAX && (pos + 1) < str.length() && *(str.get_ptr() + pos + 1) == '\n')
        return static_cast<int>(pos);
    return -1;
}

constexpr const char* fields_to_split[] = {"ARTIST", "ALBUM ARTIST", "PRODUCER", "COMPOSER", "PERFORMER", "GENRE"};

bool meta_split_value(const char* tag)
{
    for (unsigned i = 0; i < _countof(fields_to_split); ++i)
    {
        if (_stricmp(tag, fields_to_split[i]) == 0)
            return true;
    }
    return false;
}

void info_meta_write(pfc::string_base& tag, const file_info& info, const char* name, int idx, int& first)
{
    pfc::string v = info.meta_enum_value(idx, 0);
    if (meta_split_value(name))
    {
        const size_t count = info.meta_enum_value_count(idx);
        for (size_t i = 1; i < count; ++i)
        {
            v += "; ";
            v += info.meta_enum_value(idx, i);
        }
    }

    int pos = find_crlf(v);

    if (pos == -1)
    {
        if (first == 1)
        {
            first = 0;
        }
        else
        {
            tag.add_byte('\n');
        }
        tag += name;
        tag.add_byte('=');
        //r->write(v.c_str(), v.length());
        tag += v;
        return;
    }
    while (pos != -1)
    {
        pfc::string str;
        str = v;
        str.truncate(pos);
        if (first == 1)
        {
            first = 0;
        }
        else
        {
            tag.add_byte('\n');
        }
        tag += name;
        tag.add_byte('=');
        tag += str;
        v = v.get_ptr() + pos + 2;
        pos = find_crlf(v);
    }
    if (v.length())
    {
        tag.add_byte('\n');
        tag += name;
        tag.add_byte('=');
        tag += v;
    }
}

struct psf_info_meta_state
{
    file_info* info;

    pfc::string name;

    unsigned int tag_song_ms;
    unsigned int tag_fade_ms;

    pfc::string raw_length;
    pfc::string raw_fade;

    bool utf8;

    psf_info_meta_state() noexcept : info(nullptr), utf8(false), tag_song_ms(0), tag_fade_ms(0) {}

    ~psf_info_meta_state() = default;

    psf_info_meta_state(const psf_info_meta_state&) = default;
    psf_info_meta_state& operator=(const psf_info_meta_state&) = default;
    psf_info_meta_state(psf_info_meta_state&&) = default;
    psf_info_meta_state& operator=(psf_info_meta_state&&) = default;
};

int psf_info_meta(void* context, const char* name, const char* value)
{
    psf_info_meta_state* state = static_cast<psf_info_meta_state*>(context);
    pfc::string& tag = state->name;
    tag = name;

    if (stricmp_utf8(tag, "game") == 0)
    {
        DBG("Reading \"game\" as \"album\"");
        tag = "album";
    }
    else if (stricmp_utf8(tag, "year") == 0)
    {
        DBG("Reading \"year\" as \"date\"");
        tag = "date";
    }

    if (stricmp_utf8_partial(tag, "replaygain_", 11) == 0)
    {
        DBG("Reading \"replaygain\"");
        //info.info_set(tag, value);
        state->info->info_set_replaygain(tag, value);
    }
    else if (stricmp_utf8(tag, "length") == 0)
    {
        DBG("Reading \"length\"");
        const unsigned int temp = parse_first_time(value);
        if (temp != BORK_TIME)
        {
            state->tag_song_ms = temp;
            state->info->info_set_int(field_length, state->tag_song_ms);
        }
        else
        {
            state->raw_length = value;
        }
    }
    else if (stricmp_utf8(tag, "fade") == 0)
    {
        DBG("Reading \"fade\"");
        const unsigned int temp = parse_first_time(value);
        if (temp != BORK_TIME)
        {
            state->tag_fade_ms = temp;
            state->info->info_set_int(field_fade, state->tag_fade_ms);
        }
        else
        {
            state->raw_fade = value;
        }
    }
    else if (stricmp_utf8(tag, "utf8") == 0)
    {
        state->utf8 = true;
    }
    else if (stricmp_utf8_partial(tag, "_lib") == 0)
    {
        DBG("Found \"_lib\"");
        state->info->info_set(tag, value);
    }
    else if (stricmp_utf8(tag, "_enablecompare") == 0)
    {
        DBG("Found \"_enablecompare\"");
        state->info->info_set(tag, value);
    }
    else if (stricmp_utf8(tag, "_enablefifofull") == 0)
    {
        DBG("Found \"_enablefifofull\"");
        state->info->info_set(tag, value);
    }
    else if (stricmp_utf8(tag, "_64thbug1") == 0 || stricmp_utf8(tag, "_64thbug2") == 0)
    {
        DBG("Found \"_64thbug*\" tag, ignoring");
        state->info->info_set(tag, value);
    }
    else if (tag[0] == '_')
    {
        DBG("Found unknown required tag, failing");
        USF_CONSOLE_ERROR("Unsupported tag found - \"", tag, "\" is required to play file.")
        return -1;
    }
    else
    {
        if (cfg_year && stricmp_utf8(tag, "date") == 0 && strnlen(value, 10) == 10)
        {
            char year[5];
            strncpy_s(year, value, 4);
            state->info->meta_add(tag, year);
        }
        else
        {
            state->info->meta_add(tag, value);
        }
    }

    return 0;
}
#pragma endregion

#pragma region File I/O
struct usf_loader_state
{
    uint32_t enable_compare;
    uint32_t enable_fifo_full;
    void* emu_state;
    bool emu_cleared;

    usf_loader_state() : enable_compare(0), enable_fifo_full(0), emu_state(nullptr), emu_cleared(false) {}

    usf_loader_state(const usf_loader_state&) = delete;
    usf_loader_state& operator=(const usf_loader_state&) = delete;
    usf_loader_state(usf_loader_state&&) = delete;
    usf_loader_state& operator=(usf_loader_state&&) = delete;

    ~usf_loader_state()
    {
        if (emu_state)
        {
            if (emu_cleared)
            {
                usf_shutdown(emu_state);
            }
            free(emu_state);
        }
    }
};

int usf_loader(void* context, const uint8_t* exe, size_t exe_size, const uint8_t* reserved, size_t reserved_size)
{
    UNREFERENCED_PARAMETER(exe);

    usf_loader_state* state = static_cast<struct usf_loader_state*>(context);

    if (exe_size > 0)
        return -1;

    return usf_upload_section(state->emu_state, reserved, reserved_size);
}

int usf_info(void* context, const char* name, const char* value)
{
    usf_loader_state* state = static_cast<struct usf_loader_state*>(context);

    if (pfc::stricmp_ascii(name, "_enablecompare") == 0)
    {
        state->enable_compare = parse_tag_bool(value) ? 1 : 0;
    }
    else if (pfc::stricmp_ascii(name, "_enablefifofull") == 0)
    {
        state->enable_fifo_full = parse_tag_bool(value) ? 1 : 0;
    }

    return 0;
}

class psf_file_container
{
  public:
    psf_file_container() = default;

    ~psf_file_container() = default;

    psf_file_container(const psf_file_container&) = delete;
    psf_file_container& operator=(const psf_file_container&) = delete;
    psf_file_container(psf_file_container&&) = delete;
    psf_file_container& operator=(psf_file_container&&) = delete;

    void add_hint(const char* path, file::ptr p_file)
    {
        insync(lock);
        hints.add_item(psf_file_opened(path, p_file));
    }

    void remove_hint(const char* path)
    {
        insync(lock);
        hints.remove_item(psf_file_opened(path));
    }

    bool try_hint(const char* path, file::ptr& p_out)
    {
        insync(lock);
        const size_t index = hints.find_item(psf_file_opened(path));
        if (index == SIZE_MAX)
            return false;
        p_out = hints[index].f;
        p_out->reopen(fb2k::noAbort);
        return true;
    }

  private:
    critical_section lock;

    struct psf_file_opened
    {
        pfc::string p;
        file::ptr f;

        psf_file_opened() noexcept {}

        psf_file_opened(const char* p_) : p(p_) {}

        psf_file_opened(const char* p_, file::ptr f_) : p(p_), f(f_) {}

        ~psf_file_opened() = default;

        psf_file_opened(const psf_file_opened&) = default;
        psf_file_opened& operator=(const psf_file_opened&) = default;
        psf_file_opened(psf_file_opened&&) = default;
        psf_file_opened& operator=(psf_file_opened&&) = default;

        bool operator==(psf_file_opened const& in) const { return strcmp(p, in.p) == 0; }
    };

    pfc::list_t<psf_file_opened> hints;
} g_hint_list;

struct psf_file_state
{
    file::ptr f;

    psf_file_state() = default;

    ~psf_file_state() = default;

    psf_file_state(const psf_file_state&) = default;
    psf_file_state& operator=(const psf_file_state&) = default;
    psf_file_state(psf_file_state&&) = default;
    psf_file_state& operator=(psf_file_state&&) = default;
};

void* psf_file_fopen(void* context, const char* uri)
{
    UNREFERENCED_PARAMETER(context);

    psf_file_state* state = nullptr;
    try
    {
        state = new psf_file_state;
        if (!g_hint_list.try_hint(uri, state->f))
        {
            filesystem::g_open(state->f, uri, filesystem::open_mode_read, fb2k::noAbort);
        }
        return state;
    }
    catch (...)
    {
        delete state;
        return nullptr;
    }
}

size_t psf_file_fread(void* buffer, size_t size, size_t count, void* handle)
{
    try
    {
        if (size == 0 || count == 0 || size > (SIZE_MAX / count))
            return 0;

        const psf_file_state* state = static_cast<psf_file_state*>(handle);
        const size_t bytes_read = state->f->read(buffer, size * count, fb2k::noAbort);
        return bytes_read / size;
    }
    catch (...)
    {
        return 0;
    }
}

int psf_file_fseek(void* handle, int64_t offset, int whence)
{
    try
    {
        const psf_file_state* state = static_cast<psf_file_state*>(handle);
        state->f->seek_ex(offset, static_cast<file::t_seek_mode>(whence), fb2k::noAbort);
        return 0;
    }
    catch (...)
    {
        return -1;
    }
}

int psf_file_fclose(void* handle)
{
    try
    {
        const psf_file_state* state = static_cast<psf_file_state*>(handle);
        delete state;
        return 0;
    }
    catch (...)
    {
        return -1;
    }
}

long psf_file_ftell(void* handle)
{
    try
    {
        const psf_file_state* state = static_cast<psf_file_state*>(handle);
        return static_cast<long>(state->f->get_position(fb2k::noAbort));
    }
    catch (...)
    {
        return -1;
    }
}

const psf_file_callbacks psf_file_system = {
    "\\/|:",
    NULL,
    psf_file_fopen,
    psf_file_fread,
    psf_file_fseek,
    psf_file_fclose,
    psf_file_ftell,
};

void print_message(void* context, const char* message)
{
    pfc::string* msgbuf = static_cast<pfc::string*>(context);
    msgbuf->add_string(message);
}
#pragma endregion

#pragma region Decode
class input_usf : public input_stubs
{
  public:
    input_usf() noexcept = default;

    ~input_usf()
    {
        g_hint_list.remove_hint(m_path);

        shutdown();
    }

    input_usf(const input_usf&) = delete;
    input_usf& operator=(const input_usf&) = delete;
    input_usf(input_usf&&) = delete;
    input_usf& operator=(input_usf&&) = delete;

    void shutdown()
    {
        if (m_state)
        {
            delete m_state;
            m_state = nullptr;
        }
    }

    void open(service_ptr_t<file> p_filehint, const char* p_path, t_input_open_reason p_reason, abort_callback& p_abort)
    {
        input_open_file_helper(p_filehint, p_path, p_reason, p_abort);

        m_path = p_path;
        g_hint_list.add_hint(p_path, p_filehint);

        psf_info_meta_state info_state;
        info_state.info = &m_info;

        pfc::string msgbuf;

        int ret = psf_load(p_path, &psf_file_system, 0x21, 0, 0, psf_info_meta, &info_state, 0, print_message, &msgbuf);

        if (cfg_console) { USF_CONSOLE_INFO(msgbuf) }
        msgbuf.reset();

        if (ret <= 0)
            throw exception_io_data("Not a USF file");

        if (!info_state.utf8)
        {
            info_meta_ansi(m_info);
        }

        m_tag_song_ms = info_state.tag_song_ms;
        m_tag_fade_ms = info_state.tag_fade_ms;
        m_raw_length = info_state.raw_length;
        m_raw_fade = info_state.raw_fade;

        if (!m_tag_song_ms)
        {
            m_tag_song_ms = static_cast<int>(cfg_deflength);
            m_tag_fade_ms = static_cast<int>(cfg_deffade);
        }

        m_info.set_length((m_tag_song_ms + m_tag_fade_ms) * 0.001);
        m_info.info_set_int("channels", CHANNELS);
        m_info.info_set_bitspersample(BITS_PER_SAMPLE);
        m_info.info_set("codec", "USF");
        m_info.info_set("encoding", "synthesized");

        m_file = p_filehint;
    }

    void get_info(file_info& p_info, abort_callback& p_abort)
    {
        UNREFERENCED_PARAMETER(p_abort);

        const char* srate_text = m_info.info_get("samplerate");
        if (!srate_text)
        {
            if (cfg_resample)
            {
                m_info.info_set_int("samplerate", cfg_resample_hz);
                m_info.info_set_bitrate((BITS_PER_SAMPLE * CHANNELS * cfg_resample_hz + BITS_PER_KILOBIT / 2) / BITS_PER_KILOBIT);
            }
            else
            {
                int32_t rate = 0;
                usf_loader_state* p_state = nullptr;
                try
                {
                    p_state = new usf_loader_state;

                    p_state->emu_state = malloc(usf_get_state_size());
                    if (!p_state->emu_state)
                        throw std::bad_alloc();

                    usf_clear(p_state->emu_state);
                    p_state->emu_cleared = true;


                    pfc::string msgbuf;

                    int ret = psf_load(m_path, &psf_file_system, 0x21, usf_loader, p_state, usf_info, p_state, 1, print_message, &msgbuf);

                    if (cfg_console) { USF_CONSOLE_INFO(msgbuf) }
                    msgbuf.reset();

                    if (ret < 0)
                        throw exception_io_data("Failed to load USF file");

                    usf_set_compare(p_state->emu_state, p_state->enable_compare);
                    usf_set_fifo_full(p_state->emu_state, p_state->enable_fifo_full);

                    const char* error = usf_render(p_state->emu_state, 0, 0, &rate);
                    if (error != 0)
                    {
                        USF_CONSOLE_ERROR(error)
                        throw exception_io_data(error);
                    }

                    delete p_state;
                    p_state = nullptr;
                }
                catch (...)
                {
                    delete p_state;
                    p_state = nullptr;
                    throw;
                }

                m_info.info_set_int("samplerate", rate);
                m_info.info_set_bitrate((BITS_PER_SAMPLE * CHANNELS * rate + BITS_PER_KILOBIT / 2) / BITS_PER_KILOBIT);
            }
        }
        p_info.copy(m_info);
    }

    t_filestats get_file_stats(abort_callback& p_abort)
    {
        return m_file->get_stats(p_abort);
    }

    t_filestats2 get_stats2(uint32_t f, abort_callback& a)
    {
        return m_file->get_stats2_(f, a);
    }

    void decode_initialize(unsigned int p_flags, abort_callback& p_abort)
    {
        shutdown();

        m_state = new usf_loader_state;
        m_state->emu_state = malloc(usf_get_state_size());
        if (!m_state->emu_state)
            throw std::bad_alloc();

        usf_clear(m_state->emu_state);
        m_state->emu_cleared = true;


        pfc::string msgbuf;

        int ret = psf_load(m_path, &psf_file_system, 0x21, usf_loader, m_state, usf_info, m_state, 1, print_message, &msgbuf);

        if (cfg_console) { USF_CONSOLE_PRINT(msgbuf) }
        msgbuf.reset();

        if (ret < 0)
            throw exception_io_data("Failed to load USF file");

        usf_set_compare(m_state->emu_state, m_state->enable_compare);
        usf_set_fifo_full(m_state->emu_state, m_state->enable_fifo_full);

        m_start_silence = 0.0;
        m_silence = 0.0;

        m_eof = false;
        m_err = 0;
        m_data_written = 0;
        m_remainder = 0;
        m_no_loop = (p_flags & input_flag_no_looping) || !cfg_infinite;
        m_resample = cfg_resample;

        m_sample_rate = m_resample ? static_cast<uint32_t>(cfg_resample_hz) : 0;
        m_last_sample_rate = 0;

        sample_buffer.grow_size(SAMPLE_BUFFER_SIZE * CHANNELS);

        m_suppressendsilence = !!cfg_suppressendsilence;

        m_in_playback = !!(p_flags & input_flag_playback);

        constexpr double start_skip_max = 60.0;
        const double end_skip_max = static_cast<double>(cfg_endsilenceseconds);

        if (cfg_suppressopeningsilence)
        {
            for (;;)
            {
                p_abort.check();

                unsigned skip_howmany = SAMPLE_BUFFER_SIZE;
                const char* error = m_resample ? usf_render_resampled(m_state->emu_state, sample_buffer.get_ptr(), skip_howmany, m_sample_rate)
                                               : usf_render(m_state->emu_state, sample_buffer.get_ptr(), skip_howmany, &m_sample_rate);
                if (error != 0)
                    throw exception_io_data(error);
                if (m_sample_rate <= 0)
                    throw exception_io_data("USF emulator reported an invalid sample rate");
                int16_t* samples = sample_buffer.get_ptr();
                unsigned int i;
                for (i = 0; i < skip_howmany; ++i)
                {
                    if (samples[0] || samples[1])
                        break;
                    samples += CHANNELS;
                }
                m_silence += double(i) / double(m_sample_rate);
                if (i < skip_howmany)
                {
                    m_remainder = skip_howmany - i;
                    memmove(sample_buffer.get_ptr(), samples, m_remainder * BYTES_PER_SAMPLE * CHANNELS);
                    break;
                }
                if (m_silence >= start_skip_max)
                {
                    m_eof = true;
                    break;
                }
            }

            m_start_silence += m_silence;
            m_silence = 0.0;
        }

        if (m_suppressendsilence)
        {
            if (!m_resample && !m_sample_rate)
            {
                const char* error = usf_render(m_state->emu_state, 0, 0, &m_sample_rate);
                if (error != 0)
                    throw exception_io_data(error);
            }

            m_silence_buffer.resize(m_sample_rate * static_cast<int>(end_skip_max) * CHANNELS);
            if (m_remainder)
            {
                unsigned long count;
                int16_t* wptr = m_silence_buffer.get_write_ptr(count);
                memcpy(wptr, sample_buffer.get_ptr(), m_remainder * BYTES_PER_SAMPLE * CHANNELS);
                m_silence_buffer.samples_written(m_remainder * CHANNELS);
                m_remainder = 0;
            }
        }

        if (m_sample_rate)
        {
            calculate_fade();
        }
    }

    bool decode_run(audio_chunk& p_chunk, abort_callback& p_abort)
    {
        p_abort.check();

        if (m_eof || m_err < 0)
            return false;

        if (m_no_loop && m_tag_song_ms && m_sample_rate && (m_data_written >= m_song_len + m_fade_len))
            return false;

        UINT written = 0;

        int16_t* wptr = nullptr;

        if (m_suppressendsilence)
        {
            unsigned long done = 0;
            unsigned long count;
            wptr = m_silence_buffer.get_write_ptr(count);
            if (m_no_loop && m_tag_song_ms)
            {
                const int64_t remaining = (static_cast<int64_t>(m_song_len) + m_fade_len - m_data_written) * CHANNELS - static_cast<int64_t>(m_silence_buffer.data_available());
                if (remaining <= 0)
                {
                    count = 0;
                }
                else if (static_cast<uint64_t>(count) > static_cast<uint64_t>(remaining))
                {
                    count = static_cast<unsigned long>(remaining);
                }
            }

            count /= CHANNELS;

            while (count)
            {
                p_abort.check();
                unsigned int todo = SAMPLE_BUFFER_SIZE;
                if (todo > count)
                {
                    todo = count;
                }
                const char* error = m_resample ? usf_render_resampled(m_state->emu_state, wptr, todo, m_sample_rate)
                                               : usf_render(m_state->emu_state, wptr, todo, &m_sample_rate);
                if (error != 0)
                {
                    USF_CONSOLE_ERROR(error);
                    throw exception_io_data(error);
                }
                wptr += todo * CHANNELS;
                done += todo;
                count -= todo;
            }

            m_silence_buffer.samples_written(done * CHANNELS);

            if (m_silence_buffer.test_silence())
            {
                m_eof = true;
                return false;
            }

            written = m_silence_buffer.data_available() / CHANNELS;
            if (written > SAMPLE_BUFFER_SIZE)
            {
                written = SAMPLE_BUFFER_SIZE;
            }
            sample_buffer.grow_size(SAMPLE_BUFFER_SIZE * CHANNELS);
            m_silence_buffer.read(sample_buffer.get_ptr(), written * CHANNELS);
            wptr = sample_buffer.get_ptr();
        }
        else
        {
            if (m_remainder)
            {
                written = m_remainder;
                m_remainder = 0;
            }
            else
            {
                const char* error = m_resample ? usf_render_resampled(m_state->emu_state, sample_buffer.get_ptr(), SAMPLE_BUFFER_SIZE, m_sample_rate)
                                               : usf_render(m_state->emu_state, sample_buffer.get_ptr(), SAMPLE_BUFFER_SIZE, &m_sample_rate);
                if (error != 0)
                    throw exception_io_data(error);
                written = SAMPLE_BUFFER_SIZE;
            }

            wptr = sample_buffer.get_ptr();
        }

        const int d_start = m_data_written;
        m_data_written += written;
        const int d_end = m_data_written;

        calculate_fade();

        if (m_tag_song_ms && d_end > m_song_len && m_no_loop)
        {
            int16_t* sample_ptr = sample_buffer.get_ptr();
            for (int n = d_start; n < d_end; ++n)
            {
                if (n > m_song_len)
                {
                    if (n > m_song_len + m_fade_len)
                    {
                        *reinterpret_cast<DWORD*>(sample_ptr) = 0;
                    }
                    else
                    {
                        const int fade = m_song_len + m_fade_len - n;
                        sample_ptr[0] = static_cast<short>(MulDiv(sample_ptr[0], fade, m_fade_len));
                        sample_ptr[1] = static_cast<short>(MulDiv(sample_ptr[1], fade, m_fade_len));
                    }
                }
                sample_ptr += CHANNELS;
            }
        }

        p_chunk.set_data_fixedpoint(wptr, written * BYTES_PER_SAMPLE * CHANNELS, m_sample_rate, CHANNELS, BITS_PER_SAMPLE, audio_chunk::channel_config_stereo);

        return true;
    }

    void decode_seek(double p_seconds, abort_callback& p_abort)
    {
        m_eof = false;

        if (p_seconds != p_seconds || p_seconds < 0.0)
        {
            p_seconds = 0.0;
        }

        double usfemu_pos = 0.0;

        if (m_sample_rate)
        {
            usfemu_pos = static_cast<double>(m_data_written) / static_cast<double>(m_sample_rate);

            if (m_suppressendsilence)
            {
                double buffered_time = static_cast<double>(m_silence_buffer.data_available() / CHANNELS) / static_cast<double>(m_sample_rate);

                m_silence_buffer.reset();

                usfemu_pos += buffered_time;
            }
        }

        m_last_sample_rate = 0;

        if (p_seconds < usfemu_pos)
        {
            usf_restart(m_state->emu_state);

            // The skipped opening silence is rendered again but is not part of the output timeline.
            usfemu_pos = -m_start_silence;
            m_data_written = -static_cast<int>(m_start_silence * static_cast<double>(m_sample_rate) + 0.5);
        }

        p_seconds -= usfemu_pos;

        // More abortable and emulator struggles doing huge numbers of samples per call.
        while (p_seconds > 0.0)
        {
            p_abort.check();

            const char* error = m_resample ? usf_render_resampled(m_state->emu_state, sample_buffer.get_ptr(), SAMPLE_BUFFER_SIZE, m_sample_rate)
                                           : usf_render(m_state->emu_state, sample_buffer.get_ptr(), SAMPLE_BUFFER_SIZE, &m_sample_rate);
            if (error != 0)
                throw exception_io_data(error);

            usfemu_pos += static_cast<double>(SAMPLE_BUFFER_SIZE) / static_cast<double>(m_sample_rate);

            m_data_written += SAMPLE_BUFFER_SIZE;

            p_seconds -= static_cast<double>(SAMPLE_BUFFER_SIZE) / static_cast<double>(m_sample_rate);

            if (p_seconds < 0)
            {
                unsigned int remainder = static_cast<unsigned int>(-p_seconds * static_cast<double>(m_sample_rate));
                if (remainder > SAMPLE_BUFFER_SIZE)
                {
                    remainder = SAMPLE_BUFFER_SIZE;
                }
                m_remainder = remainder;

                m_data_written -= m_remainder;

                if (m_remainder)
                {
                    memmove(sample_buffer.get_ptr(), &sample_buffer[(SAMPLE_BUFFER_SIZE - m_remainder) * CHANNELS], m_remainder * BYTES_PER_SAMPLE * CHANNELS);
                }

                break;
            }
        }

        if (m_suppressendsilence && m_remainder)
        {
            unsigned long count;
            int16_t* ptr = m_silence_buffer.get_write_ptr(count);
            memcpy(ptr, sample_buffer.get_ptr(), m_remainder * BYTES_PER_SAMPLE * CHANNELS);
            m_silence_buffer.samples_written(m_remainder * CHANNELS);
            m_remainder = 0;
        }
    }

    bool decode_can_seek() noexcept
    {
        return true;
    }

    size_t extended_param(const GUID& type, size_t arg1, void* arg2, size_t arg2size) noexcept
    {
        UNREFERENCED_PARAMETER(arg1);
        UNREFERENCED_PARAMETER(arg2);
        UNREFERENCED_PARAMETER(arg2size);

        // Seeking re-emulates from the start (backward) or renders forward.
        if (type == input_params::seeking_expensive)
            return 1;
        return 0;
    }

    bool decode_get_dynamic_info(file_info& p_out, double& p_timestamp_delta) noexcept
    {
        if (m_sample_rate != m_last_sample_rate)
        {
            m_last_sample_rate = m_sample_rate;
            p_out.info_set_int("samplerate", m_sample_rate);
            p_timestamp_delta = 0;
            return true;
        }

        return false;
    }

    bool decode_get_dynamic_info_track(file_info& p_out, double& p_timestamp_delta) noexcept
    {
        UNREFERENCED_PARAMETER(p_out);
        UNREFERENCED_PARAMETER(p_timestamp_delta);

        return false;
    }

    void decode_on_idle(abort_callback& p_abort) noexcept
    {
        UNREFERENCED_PARAMETER(p_abort);
    }

    void retag(const file_info& p_info, abort_callback& p_abort)
    {
        m_info.copy(p_info);

        pfc::array_t<uint8_t> buffer;
        buffer.set_size(16);

        m_file->seek(0, p_abort);

        BYTE* ptr = buffer.get_ptr();
        m_file->read_object(ptr, 16, p_abort);
        if (ptr[0] != 'P' || ptr[1] != 'S' || ptr[2] != 'F' || ptr[3] != 0x21)
            throw exception_io_data("Not a USF file");
        const unsigned int reserved_size = pfc::byteswap_if_be_t((reinterpret_cast<unsigned int*>(ptr))[1]);
        const unsigned int exe_size = pfc::byteswap_if_be_t((reinterpret_cast<unsigned int*>(ptr))[2]);
        const uint64_t data_end = 16ull + reserved_size + exe_size;
        if (data_end > m_file->get_size_ex(p_abort))
            throw exception_io_data("USF header sizes exceed file size");
        m_file->seek(data_end, p_abort);
        m_file->set_eof(p_abort);

        pfc::string tag = "[TAG]utf8=1\n";

        int first = 1;

        // "_lib" and "_refresh" tags first.
        size_t n, p = p_info.info_get_count();
        for (n = 0; n < p; ++n)
        {
            const char* t = p_info.info_enum_name(n);
            if (*t == '_')
            {
                if (first == 1)
                {
                    first = 0;
                }
                else
                {
                    tag.add_byte('\n');
                }
                tag += t;
                tag.add_byte('=');
                tag += p_info.info_enum_value(n);
            }
        }

        // Then info.
        p = p_info.meta_get_count();
        for (n = 0; n < p; ++n)
        {
            const char* t = p_info.meta_enum_name(n);
            if (*t == '_' || _stricmp(t, "length") == 0 || _stricmp(t, "fade") == 0)
                continue; // dummy protection
            if (_stricmp(t, "album") == 0)
            {
                info_meta_write(tag, p_info, "game", static_cast<int>(n), first);
            }
            else if (_stricmp(t, "date") == 0)
            {
                const char* val = p_info.meta_enum_value(n, 0);
                char* end;
                std::ignore = strtoul(p_info.meta_enum_value(n, 0), &end, 10);
                if (static_cast<size_t>(end - val) < strlen(val))
                {
                    info_meta_write(tag, p_info, t, static_cast<int>(n), first);
                }
                else
                {
                    info_meta_write(tag, p_info, "year", static_cast<int>(n), first);
                }
            }
            else
            {
                info_meta_write(tag, p_info, t, static_cast<int>(n), first);
            }
        }

        // Then time and fade.
        {
            const auto write_line = [&tag, &first](const char* name, const char* value)
            {
                if (first == 1)
                {
                    first = 0;
                }
                else
                {
                    tag.add_byte('\n');
                }
                tag += name;
                tag.add_byte('=');
                tag += value;
            };

            // Unparseable values go back unchanged, one line per joined duplicate.
            const auto write_raw = [&write_line](const char* name, const pfc::string& raw)
            {
                const char* line = raw.get_ptr();
                for (;;)
                {
                    const char* nl = strchr(line, '\n');
                    write_line(name, std::string(line, nl ? static_cast<size_t>(nl - line) : strlen(line)).c_str());
                    if (!nl)
                    {
                        break;
                    }
                    line = nl + 1;
                }
            };

            char temp[16];
            m_tag_song_ms = 0;
            m_tag_fade_ms = 0;
            const char* t = p_info.info_get(field_length);
            if (t)
            {
                m_tag_song_ms = atoi(t);
                print_time(m_tag_song_ms, temp);
                write_line("length", temp);
            }
            else if (m_raw_length.length())
            {
                write_raw("length", m_raw_length);
            }
            t = p_info.info_get(field_fade);
            if (t)
            {
                m_tag_fade_ms = atoi(t);
                print_time(m_tag_fade_ms, temp);
                write_line("fade", temp);
            }
            else if (m_raw_fade.length())
            {
                write_raw("fade", m_raw_fade);
            }
        }

        // Then ReplayGain.
        const replaygain_info rg = p_info.get_replaygain();
        rg.for_each([&](const char* name, const char* value) {
            if (first == 1)
            {
                first = 0;
            }
            else
            {
                tag.add_byte('\n');
            }
            tag += name;
            tag.add_byte('=');
            tag += value;
        });

        m_file->write_object(tag.get_ptr(), tag.length(), p_abort);
    }

    void remove_tags(abort_callback& p_abort)
    {
        file_info_impl temp(m_info);
        temp.meta_remove_all();
        temp.reset_replaygain();
        retag(temp, p_abort);
    }

    static bool g_is_our_content_type(const char* p_content_type) noexcept
    {
        UNREFERENCED_PARAMETER(p_content_type);

        return false;
    }

    static bool g_is_our_path(const char* p_path, const char* p_extension) noexcept
    {
        UNREFERENCED_PARAMETER(p_path);

        return (stricmp_utf8(p_extension, "usf") == 0 || stricmp_utf8(p_extension, "miniusf") == 0);
    }

    static GUID g_get_guid() noexcept
    {
        return guid_usf;
    }

    static const char* g_get_name() noexcept
    {
        return APPLICATION_PRODUCT_NAME;
    }

    static GUID g_get_preferences_guid() noexcept
    {
        return guid_usf_prefs;
    }

  private:
    void calculate_fade() noexcept
    {
        m_song_len = MulDiv(m_tag_song_ms, m_sample_rate, 1000);
        m_fade_len = MulDiv(m_tag_fade_ms, m_sample_rate, 1000);
    }

    bool m_no_loop = false;
    bool m_eof = false;
    bool m_in_playback = false;
    bool m_resample = false;
    bool m_suppressendsilence = false;

    usf_loader_state* m_state = nullptr;

    pfc::array_t<int16_t> sample_buffer;
    circular_buffer<int16_t> m_silence_buffer;
    service_ptr_t<file> m_file;
    pfc::string m_path;

    int32_t m_sample_rate = 0;
    int32_t m_last_sample_rate = 0;
    int m_err = 0;
    int m_data_written = 0;
    int m_remainder = 0;
    double m_start_silence = 0.0;
    double m_silence = 0.0;
    int m_song_len = 0;
    int m_fade_len = 0;
    int m_tag_song_ms = 0;
    int m_tag_fade_ms = 0;
    pfc::string m_raw_length;
    pfc::string m_raw_fade;

    file_info_impl m_info;
};

static input_singletrack_factory_t<input_usf> g_input_usf_factory;
#pragma endregion

#pragma region File Types
DECLARE_FILE_TYPE_EX("USF;MINIUSF", "USF file", "USF files");
#pragma endregion
} // namespace
