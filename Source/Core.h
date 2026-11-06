#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace pw {
constexpr double pi = 3.14159265358979323846;
inline double midi(double hz) { return 69.0 + 12.0 * std::log2(hz / 440.0); }
inline double frequency(double note) { return 440.0 * std::exp2((note - 69.0) / 12.0); }
inline int pitchClass(int n) { return (n % 12 + 12) % 12; }
inline bool allowed(int note, int root, int scale) {
    constexpr unsigned masks[] = { 0xfff, 0xab5, 0x5ad }; // chromatic, major, natural minor
    return (masks[std::clamp(scale, 0, 2)] & (1u << pitchClass(note - root))) != 0;
}
inline double nearest(double note, int root, int scale) {
    int best = static_cast<int>(std::round(note));
    double distance = std::numeric_limits<double>::max();
    for (int n = best - 12; n <= static_cast<int>(std::round(note)) + 12; ++n) {
        if (allowed(n, root, scale) && std::abs(n - note) < distance) {
            distance = std::abs(n - note); best = n;
        }
    }
    return best;
}

struct Pitch { double hz = 0, confidence = 0, rms = 0; bool voiced() const { return hz > 0; } };

// YIN difference function. Fixed storage, no allocation in the audio callback.
// Runs at ~12 kHz after a boxcar anti-alias filter; intended for a single voice.
class Detector {
public:
    void prepare(double sr) {
        factor = std::max(1, static_cast<int>(std::round(sr / 12000.0)));
        rate = sr / factor; reset();
    }
    void reset() { ring.fill(0); write = filled = decimated = since = 0; sum = 0; latest = {}; }
    double centreDelay() const { return (size * 0.5 * factor + (factor - 1) * 0.5); }
    bool push(float sample) {
        sum += std::isfinite(sample) ? sample : 0.0f;
        if (++decimated < factor) return false;
        ring[write] = static_cast<float>(sum / factor);
        sum = 0; decimated = 0; write = (write + 1) % size;
        filled = std::min(size, filled + 1);
        if (++since < hop || filled < size) return false;
        since = 0; latest = analyse(); return true;
    }
    Pitch value() const { return latest; }
private:
    static constexpr int size = 1024, hop = 128;
    std::array<float, size> ring{}, ordered{};
    std::array<double, size / 2> difference{};
    int write = 0, filled = 0, decimated = 0, since = 0, factor = 4;
    double rate = 12000, sum = 0;
    Pitch latest;
    Pitch analyse() {
        double energy = 0, mean = 0;
        for (int i = 0; i < size; ++i) { ordered[i] = ring[(write + i) % size]; mean += ordered[i]; }
        mean /= size;
        for (auto& x : ordered) { x -= static_cast<float>(mean); energy += x * x; }
        Pitch result; result.rms = std::sqrt(energy / size);
        if (result.rms < 0.003) return result;
        int low = std::max(2, static_cast<int>(rate / 1100.0));
        int high = std::min(size / 2 - 2, static_cast<int>(std::ceil(rate / 55.0)) + 1);
        double running = 0;
        difference[0] = 1;
        // Same comparison window for every lag avoids a bias toward longer lags.
        for (int lag = 1; lag <= high + 1; ++lag) {
            double d = 0;
            for (int i = 0; i < size / 2; ++i) {
                double delta = ordered[i] - ordered[i + lag]; d += delta * delta;
            }
            running += d; difference[lag] = running > 1e-15 ? d * lag / running : 1;
        }
        int candidate = 0;
        for (int lag = low; lag < high; ++lag) {
            if (difference[lag] < 0.15) {
                while (lag + 1 < high && difference[lag + 1] < difference[lag]) ++lag;
                candidate = lag; break;
            }
        }
        if (!candidate) return result;
        double a = difference[candidate - 1], b = difference[candidate], c = difference[candidate + 1];
        double divisor = a - 2 * b + c;
        double offset = std::abs(divisor) > 1e-12 ? 0.5 * (a - c) / divisor : 0;
        result.hz = rate / (candidate + std::clamp(offset, -1.0, 1.0));
        result.confidence = std::clamp(1.0 - b, 0.0, 1.0);
        return result;
    }
};

class Correction {
public:
    void reset() { semitones = 0; }
    double update(double desired, double milliseconds, double elapsedSeconds) {
        desired = std::clamp(desired, -12.0, 12.0);
        double alpha = milliseconds <= 0.1 ? 1.0 : -std::expm1(-elapsedSeconds / (milliseconds * 0.001));
        semitones += alpha * (desired - semitones);
        return std::exp2(semitones / 12.0);
    }
    double value() const { return semitones; }
private:
    double semitones = 0;
};
}
