#include "audio.h"
#include <audio/audio_resampler.h>
#include <audioclient.h>
#include <avrt.h>
#include <cmath>
#include <mmdeviceapi.h>
namespace {
constexpr size_t period_batch = 256;
constexpr auto write_timeout = std::chrono::milliseconds(250);
}

void Audio::start(double new_rate) {
    if (!std::isfinite(new_rate) || new_rate < 8000 || new_rate > 384000)
        throw std::runtime_error("Unsupported audio rate");
    if (active && source_rate == new_rate)
        return;
    stop();
    try {
        {
            std::lock_guard<std::mutex> lock(mutex);
            source_rate = new_rate;
            rate = unsigned(std::lround(new_rate));
            ratio = rate / source_rate;
            queue.reset(size_t(std::ceil(rate * 0.256)));
            playback.reset(rate);
            device_started = flush_pending = false;
            ++generation;
            stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            control_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        }
        dsp.reset(float(new_rate));
        if (!stop_event || !control_event)
            throw std::runtime_error("Cannot create WASAPI control events");
        std::promise<void> ready;
        auto result = ready.get_future();
        thread = std::thread(&Audio::worker, this, std::move(ready));
        result.get();
    } catch (...) {
        stop();
        throw;
    }
}
void Audio::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        active = false;
        if (stop_event)
            SetEvent(stop_event);
    }
    space_ready.notify_all();
    if (thread.joinable())
        thread.join();
    std::lock_guard<std::mutex> producer(producer_mutex);
    std::lock_guard<std::mutex> lock(mutex);
    if (converter_data)
        converter->free(converter_data);
    converter_data = nullptr;
    converter = nullptr;
    write_deadline = {};
    queue.clear();
    device_started = false;
    if (stop_event)
        CloseHandle(stop_event);
    if (control_event)
        CloseHandle(control_event);
    stop_event = control_event = nullptr;
}

void Audio::flush_locked() {
    queue.clear();
    playback.restart();
    device_started = false;
    flush_pending = true;
    ++generation;
    if (control_event)
        SetEvent(control_event);
}
void Audio::clear() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        flush_locked();
    }
    space_ready.notify_all();
}
void Audio::set_frame_limit(bool enabled) {
    double current_rate;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (capped == enabled)
            return;
        capped = enabled;
        current_rate = source_rate;
        flush_locked();
    }
    space_ready.notify_all();
    std::lock_guard<std::mutex> producer(producer_mutex);
    dsp.reset(float(current_rate));
    write_deadline = {};
}
void Audio::begin_frame() {
    std::lock_guard<std::mutex> producer(producer_mutex);
    std::lock_guard<std::mutex> lock(mutex);
    if (!active || !capped)
        return;
    write_deadline = Clock::now() + write_timeout;
    ratio = audio_output_ratio(source_rate, rate, queue, device_started);
}
std::string Audio::error() {
    std::lock_guard<std::mutex> lock(mutex);
    return last_error;
}
double Audio::queued_seconds() {
    std::lock_guard<std::mutex> lock(mutex);
    return double(queue.frames()) / rate;
}
void Audio::reset_converter() {
    if (!retro_resampler_realloc(&converter_data, &converter, "sinc", RESAMPLER_QUALITY_NORMAL, 0.994))
        throw std::runtime_error("Cannot initialize audio rate conversion");
}

bool Audio::enqueue(const int16_t *samples, size_t frames, uint64_t batch, Clock::time_point deadline) {
    std::unique_lock<std::mutex> lock(mutex);
    while (frames) {
        if (!space_ready.wait_until(lock, deadline, [&] {
                return !active || !capped || generation != batch || queue.space();
            })) {
            dropped += frames;
            ++producer_timeouts;
            return false;
        }
        if (!active || !capped || generation != batch)
            return false;
        const auto written = queue.push(samples, frames);
        samples += written * 2;
        frames -= written;
        if (!device_started && playback.ready(queue))
            SetEvent(control_event);
    }
    return true;
}

size_t Audio::push(const int16_t *samples, size_t frames) {
    if (!samples || !frames || !active)
        return frames;
    std::lock_guard<std::mutex> producer(producer_mutex);
    uint64_t batch;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!active || !capped)
            return frames;
        batch = generation;
        if (converter_generation != batch)
            ratio = rate / source_rate;
    }
    if (converter_generation != batch) {
        reset_converter();
        converter_generation = batch;
    }
    const auto limit = Clock::now() + write_timeout;
    const auto deadline = write_deadline == Clock::time_point{} ? limit : std::min(limit, write_deadline);
    const auto pcm = dsp.process(samples, frames, processed);
    std::array<float, period_batch * 2> input;
    // The clock correction is bounded to 0.5%, with room for a fractional
    // nominal rate and the converter's carried frame phase.
    std::array<float, (period_batch + 4) * 2> output;
    std::array<int16_t, (period_batch + 4) * 2> converted;
    for (size_t offset = 0; offset < pcm.frames; offset += period_batch) {
        const auto count = std::min(period_batch, pcm.frames - offset);
        for (size_t i = 0; i < count * 2; ++i)
            input[i] = pcm.samples[offset * 2 + i] * (1.0f / 32768.0f);
        resampler_data conversion{input.data(), output.data(), count, 0, ratio};
        converter->process(converter_data, &conversion);
        for (size_t i = 0; i < conversion.output_frames * 2; ++i)
            converted[i] = int16_t(std::lrint(std::clamp(output[i] * 32768.0f, -32768.0f, 32767.0f)));
        if (!enqueue(converted.data(), conversion.output_frames, batch, deadline))
            break;
    }
    return frames;
}
void Audio::worker(std::promise<void> ready) {
    HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    HANDLE event = nullptr, priority = nullptr;
    bool signalled = false;
    try {
        check(com, "Initialize WASAPI COM");
        ComPtr<IMMDeviceEnumerator> enumerator;
        check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)),
              "WASAPI enumerator");
        ComPtr<IMMDevice> endpoint;
        check(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &endpoint), "Default audio endpoint");
        ComPtr<IAudioClient> client;
        check(endpoint->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                 reinterpret_cast<void **>(client.GetAddressOf())),
              "WASAPI client");
        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = 2;
        format.nSamplesPerSec = rate;
        format.wBitsPerSample = 16;
        format.nBlockAlign = 4;
        format.nAvgBytesPerSec = rate * 4;
        REFERENCE_TIME device_period = 0;
        check(client->GetDevicePeriod(&device_period, nullptr), "WASAPI device period");
        const REFERENCE_TIME buffer_duration = std::max<REFERENCE_TIME>(300000, device_period * 3);
        check(client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                 AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                     AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                 buffer_duration, 0, &format, nullptr),
              "WASAPI shared PCM stream");
        event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event)
            throw std::runtime_error("WASAPI event creation failed");
        check(client->SetEventHandle(event), "WASAPI event handle");
        ComPtr<IAudioRenderClient> render;
        check(client->GetService(IID_PPV_ARGS(&render)), "WASAPI render service");
        UINT32 capacity = 0;
        check(client->GetBufferSize(&capacity), "WASAPI buffer size");
        DWORD task = 0;
        priority = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task);
        {
            std::lock_guard<std::mutex> lock(mutex);
            active = true;
            last_error.clear();
        }
        ready.set_value();
        signalled = true;
        HANDLE handles[] = {stop_event, control_event, event};
        bool playing = false;
        while (true) {
            const DWORD wake = WaitForMultipleObjects(3, handles, FALSE, 2000);
            if (wake == WAIT_OBJECT_0)
                break;
            if (wake != WAIT_OBJECT_0 + 1 && wake != WAIT_OBJECT_0 + 2 && wake != WAIT_TIMEOUT)
                throw std::runtime_error("WASAPI event wait failed");
            bool flush;
            {
                std::lock_guard<std::mutex> lock(mutex);
                flush = flush_pending;
                flush_pending = false;
            }
            if (flush && playing) {
                check(client->Stop(), "Stop WASAPI for flush");
                check(client->Reset(), "Flush WASAPI padding");
                playing = false;
            }
            UINT32 padding = 0;
            if (playing)
                check(client->GetCurrentPadding(&padding), "WASAPI padding");
            const UINT32 available = capacity - std::min(capacity, padding);
            if (!available)
                continue;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (!active || !capped || flush_pending || (!playing && !playback.ready(queue)))
                    continue;
                const auto request = playing ? available : std::min(available, UINT32(queue.frames()));
                BYTE *output = nullptr;
                check(render->GetBuffer(request, &output), "WASAPI buffer");
                const auto read = playback.read(queue, reinterpret_cast<int16_t *>(output), request);
                underruns += request - read.played;
                if (read.shortage_started)
                    ++shortage_events;
                check(render->ReleaseBuffer(request, read.has_samples ? 0 : AUDCLNT_BUFFERFLAGS_SILENT), "WASAPI submit");
                if (!playing) {
                    check(client->Start(), "Start WASAPI");
                    playing = device_started = true;
                }
            }
            space_ready.notify_one();
        }
        if (playing)
            client->Stop();
    } catch (const std::exception &failure) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            last_error = failure.what();
            active = false;
            device_started = false;
        }
        if (!signalled)
            ready.set_exception(std::current_exception());
    }
    space_ready.notify_all();
    if (priority)
        AvRevertMmThreadCharacteristics(priority);
    if (event)
        CloseHandle(event);
    if (SUCCEEDED(com))
        CoUninitialize();
}
