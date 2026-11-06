#pragma once
#include "RubberBandABI.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace pw {
// Fixed-rate adapter for Rubber Band's variable-rate output. We pad and discard
// vendor-reported startup delay, then insert a fixed, host-reported reserve.
// No heap growth, mutexes, file access, or thread creation in push().
class Shifter {
public:
    static constexpr int hop = 256;
    Shifter() = default;
    ~Shifter() { if (state) rubberband_delete(state); }
    Shifter(const Shifter&) = delete;
    Shifter& operator=(const Shifter&) = delete;
    void prepare(double sr, int count) {
        if (state) rubberband_delete(state);
        channels = std::clamp(count, 1, 2);
        // RealTime | ThreadingNever | FormantPreserved | PitchHighConsistency | EngineFiner.
        state = rubberband_new(static_cast<unsigned>(sr), static_cast<unsigned>(channels),
                               0x00000001 | 0x00010000 | 0x01000000 | 0x04000000 | 0x20000000, 1, 1);
        if (!state) throw std::runtime_error("Rubber Band could not initialise");
        rubberband_set_max_process_size(state, hop);
        reserve = static_cast<int>(std::ceil(sr * 0.125 / hop)) * hop;
        for (auto& queue : fifo) queue.assign(static_cast<size_t>(reserve * 8), 0);
        reset();
    }
    void reset() {
        if (!state) return;
        rubberband_reset(state); rubberband_set_pitch_scale(state, 1);
        input = {}; output = {}; cursor = 0; read = 0; write = reserve; available = reserve;
        underruns = overflows = 0;
        for (auto& queue : fifo) std::fill(queue.begin(), queue.end(), 0);
        discard = rubberband_get_start_delay(state);
        unsigned pad = rubberband_get_preferred_start_pad(state);
        const float* inputs[] = {input[0].data(), input[1].data()};
        while (pad) {
            unsigned block = std::min(pad, static_cast<unsigned>(hop));
            rubberband_process(state, inputs, block, 0); drain(); pad -= block;
        }
    }
    int latency() const { return reserve; }
    int errors() const { return underruns + overflows; }
    void push(const float* frame, float* result, double ratio) {
        for (int ch = 0; ch < channels; ++ch) input[ch][cursor] = frame[ch];
        if (++cursor == hop) {
            cursor = 0;
            rubberband_set_pitch_scale(state, std::clamp(ratio, 0.5, 2.0));
            const float* inputs[] = {input[0].data(), input[1].data()};
            rubberband_process(state, inputs, hop, 0); drain();
        }
        if (available > 0) {
            for (int ch = 0; ch < channels; ++ch) result[ch] = fifo[ch][read];
            read = (read + 1) % static_cast<int>(fifo[0].size()); --available;
        } else {
            for (int ch = 0; ch < channels; ++ch) result[ch] = 0;
            ++underruns;
        }
    }
private:
    RubberBandState state = nullptr;
    int channels = 1, reserve = 0, cursor = 0, read = 0, write = 0, available = 0;
    int underruns = 0, overflows = 0;
    unsigned discard = 0;
    std::array<std::array<float, hop>, 2> input{}, output{};
    std::array<std::vector<float>, 2> fifo;
    void drain() {
        float* outputs[] = {output[0].data(), output[1].data()};
        while (rubberband_available(state) > 0) {
            unsigned request = static_cast<unsigned>(std::min(hop, rubberband_available(state)));
            unsigned count = rubberband_retrieve(state, outputs, request);
            if (!count) break;
            for (unsigned i = 0; i < count; ++i) {
                if (discard) { --discard; continue; }
                if (available >= static_cast<int>(fifo[0].size())) { ++overflows; continue; }
                for (int ch = 0; ch < channels; ++ch) fifo[ch][write] = output[ch][i];
                write = (write + 1) % static_cast<int>(fifo[0].size()); ++available;
            }
        }
    }
};
}
