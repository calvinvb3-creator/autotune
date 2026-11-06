#include "Core.h"
#include "Notes.h"
#include "Shifter.h"
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

int checks = 0;
void require(bool value, const std::string& message) {
    ++checks;
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
double cents(double a, double b) { return 1200 * std::log2(a / b); }
pw::Pitch detect(double sr, double hz, bool harmonics = false) {
    pw::Detector d; d.prepare(sr);
    for (int i = 0; i < static_cast<int>(sr * 0.4); ++i) {
        double phase = 2 * pw::pi * hz * i / sr;
        double x = harmonics ? 0.12 * std::sin(phase) + 0.3 * std::sin(phase * 2) + 0.18 * std::sin(phase * 3) : 0.4 * std::sin(phase);
        d.push(static_cast<float>(x));
    }
    return d.value();
}
void writeWav(const std::string& path, const std::vector<float>& data, int sr) {
    std::ofstream out(path, std::ios::binary);
    auto u16 = [&](unsigned value) { out.put(static_cast<char>(value)); out.put(static_cast<char>(value >> 8)); };
    auto u32 = [&](unsigned value) { u16(value); u16(value >> 16); };
    out.write("RIFF", 4); u32(36 + static_cast<unsigned>(data.size() * 2)); out.write("WAVEfmt ", 8);
    u32(16); u16(1); u16(1); u32(sr); u32(sr * 2); u16(2); u16(16); out.write("data", 4); u32(static_cast<unsigned>(data.size() * 2));
    for (float value : data) u16(static_cast<uint16_t>(static_cast<int16_t>(std::clamp(value, -1.0f, 1.0f) * 32767)));
}
double outputPitch(const std::vector<float>& data, double sr, double start, double length) {
    pw::Detector d; d.prepare(sr); std::vector<double> values;
    for (int i = static_cast<int>(start * sr); i < std::min(static_cast<int>(data.size()), static_cast<int>((start + length) * sr)); ++i)
        if (d.push(data[i]) && d.value().voiced()) values.push_back(d.value().hz);
    require(!values.empty(), "rendered audio has a measurable fundamental");
    std::sort(values.begin(), values.end()); return values[values.size() / 2];
}

std::vector<float> render(double sr, double hz, double targetMidi, int channels, bool autoTune,
                          bool editNote = false) {
    pw::Shifter s; s.prepare(sr, channels);
    pw::Detector detector; detector.prepare(sr); pw::Correction c;
    int total = static_cast<int>(sr * 3);
    std::vector<float> output(static_cast<size_t>(total));
    double ratio = autoTune || editNote ? 1 : std::exp2((targetMidi - pw::midi(hz)) / 12.0);
    int hop = 0;
    for (int i = 0; i < total; ++i) {
        double phase = 2 * pw::pi * hz * i / sr;
        float x = static_cast<float>(0.3 * std::sin(phase) + 0.08 * std::sin(phase * 2));
        float input[2]{x, x}, wet[2]{};
        ++hop;
        if (detector.push(x)) {
            auto p = detector.value();
            if ((autoTune || editNote) && p.voiced()) {
                double measured = pw::midi(p.hz);
                double target = autoTune ? pw::nearest(measured, 0, 0) : targetMidi;
                ratio = c.update(target - measured, 35, hop / sr);
            }
            hop = 0;
        }
        s.push(input, wet, ratio);
        for (int ch = 0; ch < channels; ++ch) require(std::isfinite(wet[ch]), "finite output");
        if (channels == 2) require(std::abs(wet[0] - wet[1]) < 1e-5, "identical stereo inputs stay aligned");
        output[i] = wet[0];
    }
    require(s.errors() == 0, "no underruns or overflows during steady render");
    return output;
}

int main(int argc, char** argv) {
    std::string outputDirectory = argc > 1 ? argv[1] : ".";
    require(pw::nearest(60.49, 0, 0) == 60, "chromatic target");
    require(pw::nearest(61.2, 0, 1) == 62, "C major excludes C sharp");
    require(pw::nearest(68.2, 9, 2) == 69, "A minor excludes G sharp");
    require(pw::allowed(-12, 0, 1), "negative note pitch class");
    for (double sr : {44100.0, 48000.0, 88200.0, 96000.0}) {
        for (double hz : {55.0, 65.406, 110.0, 220.0, 440.0, 880.0}) {
            auto p = detect(sr, hz);
            require(p.voiced() && std::abs(cents(p.hz, hz)) < 8, "sine detector accuracy at " + std::to_string(sr) + "/" + std::to_string(hz));
        }
        auto p = detect(sr, 173.0, true);
        require(p.voiced() && std::abs(cents(p.hz, 173)) < 8, "harmonic-rich signal does not jump octave");
    }
    pw::Detector d; d.prepare(48000);
    for (int i = 0; i < 24000; ++i) d.push(0);
    require(!d.value().voiced(), "silence is unvoiced");
    for (int i = 0; i < 24000; ++i) d.push(0.25f);
    require(!d.value().voiced(), "DC is unvoiced");
    std::mt19937 rng(440); std::normal_distribution<float> noise(0, 0.15f);
    for (int i = 0; i < 24000; ++i) d.push(noise(rng));
    require(!d.value().voiced(), "noise is unvoiced");
    pw::Correction correction;
    for (int i = 0; i < 100; ++i) correction.update(1, 50, 0.01);
    require(std::abs(correction.value() - 1) < 0.001, "retune smoothing reaches target");
    require(correction.update(20, 0, 0.01) <= 2.0, "pitch shift range bounded");

    pw::Notes notes;
    pw::Pitch p{pw::frequency(60.2), 0.99, 0.3};
    notes.capture(1, p, 0.01); notes.capture(1.01, p, 0.01);
    require(notes.count == 1, "adjacent frames merge into one note");
    double noteShift = 99;
    require(notes.shiftAt(1.005, noteShift) && std::abs(noteShift) < 1e-9, "untouched captured notes have no manual shift");
    notes.data[0].target = 64;
    double target = 0;
    require(notes.targetAt(1.005, target) && target == 64, "manual target read by playback");
    require(notes.shiftAt(1.005, noteShift) && std::abs(noteShift - 3.8) < 1e-9, "manual editing applies a constant offset, preserving modulation");
    require(!notes.shiftAt(0.5, noteShift), "manual offset does not leak outside its interval");
    require(!notes.targetAt(0.5, target), "manual edits do not leak outside note interval");
    p.hz = pw::frequency(67); notes.capture(1.02, p, 0.01);
    require(notes.count == 2, "pitch step starts a new note");
    notes.stopSegment(); notes.capture(0.2, p, 0.01);
    require(notes.targetAt(0.2, target), "seek lookup works without ordered insertion");
    notes.clear(); require(notes.count == 0, "clear removes edits");

    for (double sr : {44100.0, 48000.0, 96000.0}) {
        double flatA = pw::frequency(68.65);
        auto automatic = render(sr, flatA, 69, 1, true);
        double measured = outputPitch(automatic, sr, 1.2, 1.0);
        require(std::abs(cents(measured, 440)) < 12, "actual automatic output reaches A4 at " + std::to_string(sr));
        std::cout << "Automatic " << sr << " Hz: " << measured << " Hz, " << cents(measured, 440) << " cents error\n";
        auto manual = render(sr, 220, 60, 2, false, true);
        measured = outputPitch(manual, sr, 1.2, 1.0);
        std::cout << "Manual " << sr << " Hz: " << measured << " Hz, " << cents(measured, pw::frequency(60)) << " cents error\n";
        require(std::abs(cents(measured, pw::frequency(60))) < 12, "manual note target changes actual audio");
        if (sr == 48000) {
            writeWav(outputDirectory + "/automatic-A4.wav", automatic, 48000);
            writeWav(outputDirectory + "/manual-A3-to-C4.wav", manual, 48000);
        }
    }
    // Unity impulse measures the adapter delay rather than trusting a constant.
    pw::Shifter shift; shift.prepare(48000, 1);
    std::vector<float> impulse(48000, 0);
    for (int i = 0; i < 48000; ++i) {
        float in[2]{i == 12000 ? 0.5f : 0.0f, 0}, out[2]{}; shift.push(in, out, 1); impulse[i] = out[0];
    }
    int peak = static_cast<int>(std::max_element(impulse.begin(), impulse.end(), [](float a, float b) { return std::abs(a) < std::abs(b); }) - impulse.begin());
    std::cout << "Unity impulse peak delay: " << peak - 12000 << "; reported delay: " << shift.latency() << '\n';
    require(std::abs(peak - 12000 - shift.latency()) <= 2, "actual latency matches reported latency");
    require(shift.errors() == 0, "impulse render has no underruns");
    shift.reset(); require(shift.errors() == 0, "reset clears buffer counters");
    for (double sr : {44100.0, 48000.0, 96000.0}) {
        pw::Shifter stress; stress.prepare(sr, 2);
        for (int i = 0; i < static_cast<int>(sr * 8); ++i) {
            int section = static_cast<int>(i / (sr * 0.5)) % 4;
            double ratio = section == 0 ? 0.5 : section == 1 ? 2 : section == 2 ? 1 : std::exp2(-0.37 / 12);
            float x = static_cast<float>(0.25 * std::sin(2 * pw::pi * 173 * i / sr));
            float input[2]{x, x}, out[2]{}; stress.push(input, out, ratio);
            require(std::isfinite(out[0]) && std::isfinite(out[1]), "finite output during extreme ratio changes");
        }
        require(stress.errors() == 0, "no buffer errors during eight-second ratio stress test at " + std::to_string(sr));
    }
    std::cout << "PASS: " << checks << " assertions; pitch, scale, segmentation, audio correction, stereo, latency.\n";
}
