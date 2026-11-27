#pragma once
#include "audio_playback.h"
#include "common.h"
#include "dsp_chain.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>
class Audio {
  public:
    ~Audio() { stop(); }
    void start(double rate);
    void stop();
    void clear();
    void set_frame_limit(bool enabled);
    void begin_frame();
    size_t push(const int16_t *samples, size_t frames);
    double queued_seconds();
    std::string error();
    bool running() const { return active; }
    dsp_chain dsp;
    std::atomic<uint64_t> underruns{0}, dropped{0};
    std::atomic<uint64_t> shortage_events{0}, producer_timeouts{0};

  private:
    using Clock = std::chrono::steady_clock;
    std::mutex mutex;
    std::mutex producer_mutex;
    std::condition_variable space_ready;
    std::thread thread;
    HANDLE stop_event = nullptr, control_event = nullptr;
    AudioQueue queue;
    AudioPlayback playback;
    std::vector<int16_t> processed;
    bool capped = true, device_started = false, flush_pending = false;
    uint64_t generation = 0, converter_generation = 0;
    double source_rate = 48000, ratio = 1.0;
    unsigned rate = 48000;
    Clock::time_point write_deadline{};
    const struct retro_resampler *converter = nullptr;
    void *converter_data = nullptr;
    std::string last_error;
    std::atomic<bool> active{false};
    void flush_locked();
    void reset_converter();
    bool enqueue(const int16_t *samples, size_t frames, uint64_t batch, Clock::time_point deadline);
    void worker(std::promise<void> ready);
};
