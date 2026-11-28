#include <math.h>
#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif

enum { WIDTH = 96, HEIGHT = 72, FEATURES = 24 * 18, BANDS = 4 };

EXPORT void video_features(const uint8_t *frame, float *features, size_t count)
{
    for (size_t n = 0; n < count; ++n, frame += WIDTH * HEIGHT, features += FEATURES) {
        float mean = 0, energy = 0;
        for (unsigned y = 0; y < 18; ++y) {
            for (unsigned x = 0; x < 24; ++x) {
                unsigned center = (2 + y * 4) * WIDTH + 2 + x * 4;
                float value = (2 * frame[center] + frame[center - WIDTH] + frame[center + WIDTH] +
                               frame[center - 1] + frame[center + 1]) / 6.0f;
                features[y * 24 + x] = value;
                mean += value;
            }
        }
        mean /= FEATURES;
        for (unsigned y = 0; y < 18; ++y) {
            for (unsigned x = 0; x < 24; ++x) {
                unsigned i = y * 24 + x;
                features[i] = x && x < 23 && y && y < 17 ? features[i] - mean : 0;
                energy += features[i] * features[i];
            }
        }
        if (energy) {
            float norm = sqrtf(energy);
            for (unsigned i = 0; i < FEATURES; ++i) features[i] /= norm;
        }
    }
}

static float dot(const float *a, const float *b)
{
    float result = 0;
    for (unsigned i = 0; i < FEATURES; ++i) result += a[i] * b[i];
    return result;
}

EXPORT void video_scores(const float *reference, const float *replay, size_t count, float *scores)
{
    /* Both buffers start at the first frame in the window. The reference is
     * 30 Hz and the replay approximately 60 fields/s. Match three frames. */
    for (size_t i = 0; i < count; ++i, replay += FEATURES)
        scores[i] = (dot(reference, replay) +
                     2 * dot(reference + 5 * FEATURES, replay + 10 * FEATURES) +
                     dot(reference + 10 * FEATURES, replay + 20 * FEATURES)) / 4;
}

static double audio_score(const double *reference, const double *replay, size_t count)
{
    double covariance = 0, reference_energy = 0, replay_energy = 0;
    for (size_t band = 0; band < BANDS; ++band) {
        double x = 0, y = 0, xx = 0, yy = 0, xy = 0;
        for (size_t i = band; i < count * BANDS; i += BANDS) {
            const double a = reference[i], b = replay[i];
            x += a;
            y += b;
            xx += a * a;
            yy += b * b;
            xy += a * b;
        }
        covariance += xy - x * y / count;
        reference_energy += xx - x * x / count;
        replay_energy += yy - y * y / count;
    }
    return covariance / sqrt(fmax(reference_energy * replay_energy, 1e-10));
}

EXPORT void audio_scores(const double *reference, const double *replay, size_t length,
                         size_t count, double *scores)
{
    for (size_t i = 0; i < count; ++i, replay += BANDS)
        scores[i] = audio_score(reference, replay, length);
}
