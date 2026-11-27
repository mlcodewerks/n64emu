#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

class AudioQueue {
  public:
    void reset(size_t capacity) {
        samples.resize(capacity * stereo);
        clear();
    }
    void clear() { head = tail = count = 0; }
    size_t frames() const { return count; }
    size_t capacity() const { return samples.size() / stereo; }
    size_t space() const { return capacity() - count; }

    size_t push(const int16_t *pcm, size_t frames) {
        const auto keep = std::min(frames, space());
        if (!keep)
            return 0;

        const auto first = std::min(keep, capacity() - tail);
        std::copy_n(pcm, first * stereo, samples.data() + tail * stereo);
        std::copy_n(pcm + first * stereo, (keep - first) * stereo, samples.data());
        tail = advance(tail, keep, capacity());
        count += keep;
        return keep;
    }

    size_t take(int16_t *pcm, size_t frames) {
        const auto available = std::min(frames, count);
        if (!available)
            return 0;

        const auto capacity = samples.size() / stereo;
        const auto first = std::min(available, capacity - head);
        std::copy_n(samples.data() + head * stereo, first * stereo, pcm);
        std::copy_n(samples.data(), (available - first) * stereo, pcm + first * stereo);
        head = advance(head, available, capacity);
        count -= available;
        return available;
    }

  private:
    static constexpr size_t stereo = 2;
    static size_t advance(size_t position, size_t frames, size_t capacity) {
        position += frames;
        return position < capacity ? position : position - capacity;
    }
    std::vector<int16_t> samples;
    size_t head = 0, tail = 0, count = 0;
};
