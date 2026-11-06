#include "Processor.h"
#include <iostream>
#include <cstdlib>

namespace {
void check(bool success, const char* message) {
    if (!success) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
class PlayHead final : public juce::AudioPlayHead {
public:
    double rate = 48000;
    int64_t samples = 0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo p; p.setTimeInSamples(samples); p.setTimeInSeconds(samples / rate);
        p.setIsPlaying(playing); p.setBpm(120); return p;
    }
};
void parameter(PitchProcessor& p, const char* id, float value) {
    auto* param = p.parameters.getParameter(id);
    check(param != nullptr, "parameter exists");
    param->setValueNotifyingHost(param->convertTo0to1(value));
}
std::vector<float> render(PitchProcessor& p, PlayHead& ph, double hz, double duration, int channels,
                          bool bypass = false) {
    const int blocks[] = {32, 64, 127, 256, 511, 1024};
    std::vector<float> result;
    int remaining = static_cast<int>(duration * ph.rate), iteration = 0;
    result.reserve(static_cast<size_t>(remaining));
    while (remaining > 0) {
        int n = std::min(remaining, blocks[iteration++ % 6]);
        juce::AudioBuffer<float> b(channels, n);
        for (int i = 0; i < n; ++i) {
            double angle = 2 * pw::pi * hz * (ph.samples + i) / ph.rate;
            float x = static_cast<float>(0.3 * std::sin(angle) + 0.05 * std::sin(2 * angle));
            for (int ch = 0; ch < channels; ++ch) b.setSample(ch, i, x);
        }
        juce::MidiBuffer midi;
        if (bypass) p.processBlockBypassed(b, midi); else p.processBlock(b, midi);
        for (int i = 0; i < n; ++i) {
            for (int ch = 0; ch < channels; ++ch) check(std::isfinite(b.getSample(ch, i)), "finite processor output");
            if (channels == 2) check(std::abs(b.getSample(0, i) - b.getSample(1, i)) < 1e-5, "stereo image stays aligned");
            result.push_back(b.getSample(0, i));
        }
        ph.samples += n; remaining -= n;
    }
    check(p.bufferErrors == 0, "processor has no buffer underruns or overflows");
    return result;
}
double pitch(const std::vector<float>& samples, double sr) {
    pw::Detector detector; detector.prepare(sr); std::vector<double> hz;
    for (size_t i = static_cast<size_t>(sr); i < samples.size(); ++i)
        if (detector.push(samples[i]) && detector.value().voiced()) hz.push_back(detector.value().hz);
    check(!hz.empty(), "processor output contains pitched audio");
    std::sort(hz.begin(), hz.end()); return hz[hz.size() / 2];
}
void setup(PitchProcessor& p, PlayHead& ph, double sr, int channels) {
    ph.rate = sr; ph.samples = 0; ph.playing = true; p.setPlayHead(&ph);
    juce::AudioProcessor::BusesLayout layout;
    auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    layout.inputBuses.add(set); layout.outputBuses.add(set);
    check(p.setBusesLayout(layout), "mono/stereo layout accepted");
    p.setRateAndBufferSizeDetails(sr, 1024); p.prepareToPlay(sr, 1024);
}
}

int main() {
    juce::ScopedJuceInitialiser_GUI init;
    for (double sr : {44100.0, 48000.0, 96000.0}) {
        for (int channels : {1, 2}) {
            PitchProcessor processor; PlayHead head; setup(processor, head, sr, channels);
            parameter(processor, "speed", 25);
            auto automatic = render(processor, head, pw::frequency(68.65), 2.5, channels);
            check(std::abs(1200 * std::log2(pitch(automatic, sr) / 440)) < 12, "automatic tuning changes processor output to A4");
            processor.clearNotes(); processor.capture = true; head.samples = 0;
            render(processor, head, 220, 2, channels);
            processor.capture = false;
            check(processor.notes.count.load() > 0, "transport-linked note capture produces notes");
            double untouchedShift = 99;
            auto firstNote = processor.notes.data[0].view();
            check(processor.notes.shiftAt((firstNote.start + firstNote.end) * 0.5, untouchedShift) && std::abs(untouchedShift) < 1e-9,
                  "captured unedited note does not flatten pitch modulation");
            for (int n = 0; n < processor.notes.count.load(); ++n) processor.notes.data[n].target = 60;
            parameter(processor, "mode", 1); head.samples = 0;
            auto edited = render(processor, head, 220, 2.5, channels);
            check(std::abs(1200 * std::log2(pitch(edited, sr) / pw::frequency(60))) < 12, "manual note edits change processor output to C4");
            juce::MemoryBlock state; processor.getStateInformation(state);
            PitchProcessor restored; restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
            check(restored.notes.count == processor.notes.count, "note count survives session save/restore");
            check(restored.parameters.getRawParameterValue("mode")->load() == 1, "mode survives session restore");
            check(restored.notes.data[0].target == 60, "manual edit survives session restore");
            PlayHead restoredHead; setup(restored, restoredHead, sr, channels);
            auto restoredAudio = render(restored, restoredHead, 220, 2.5, channels);
            check(std::abs(1200 * std::log2(pitch(restoredAudio, sr) / pw::frequency(60))) < 12, "restored session renders corrected audio");
            head.samples = 0;
            auto bypass = render(processor, head, 220, 2, channels, true);
            int delay = processor.getLatencySamples();
            for (size_t i = static_cast<size_t>(sr); i < bypass.size(); ++i) {
                double angle = 2 * pw::pi * 220 * (static_cast<double>(i) - delay) / sr;
                float expected = static_cast<float>(0.3 * std::sin(angle) + 0.05 * std::sin(2 * angle));
                check(std::abs(bypass[i] - expected) < 2e-6, "bypass is sample-aligned delayed original audio");
            }
            processor.clearNotes(); check(processor.notes.count == 0 && !processor.capture, "clear also disarms capture");
            std::cout << "PASS processor: " << sr << " Hz, " << channels << " channels, variable block sizes\n";
        }
    }
    {
        PitchProcessor captured; PlayHead transport; setup(captured,transport,48000,1);
        check(captured.beginCapture(), "audio capture arms");
        render(captured,transport,220,2.5,1); captured.finishCapture();
        while(captured.renderBusy.load()) juce::Thread::sleep(1);
        check(captured.hasAudioTake(), "captured source audio exists");
        check(captured.renderError().isEmpty(), "initial background render succeeds");
        auto edits=captured.notes.snapshot(); check(!edits.empty(), "captured notes exist");
        for(auto& note:edits) {note.target=60;double duration=note.end-note.start;note.start+=0.5;note.end=note.start+duration*1.2;note.gainDb=-3;}
        captured.setNoteEdits(edits);
        while(captured.renderBusy.load()) juce::Thread::sleep(1);
        check(captured.renderError().isEmpty(), "edited background render succeeds");
        parameter(captured,"mode",1); transport.samples=0;
        auto result=render(captured,transport,220,3.5,1);
        check(std::abs(1200*std::log2(pitch(result,48000)/pw::frequency(60)))<15, "captured audio pitch and timing edits reach processor output");
        juce::MemoryBlock session;captured.getStateInformation(session);
        PitchProcessor restored;restored.setStateInformation(session.getData(),static_cast<int>(session.getSize()));
        while(restored.renderBusy.load()) juce::Thread::sleep(1);
        check(restored.hasAudioTake(), "source audio survives session restore");
        check(restored.renderError().isEmpty(), "restored captured take renders");
        auto recalled=restored.notes.snapshot();check(recalled.size()==edits.size(), "all edited notes survive session restore");
        check(std::abs(recalled[0].start-edits[0].start)<1e-8 && recalled[0].gainDb==-3, "timing and inspector settings survive restore");
        PlayHead recalledTransport;setup(restored,recalledTransport,48000,1);
        auto recalledAudio=render(restored,recalledTransport,220,3.5,1);
        check(std::abs(1200*std::log2(pitch(recalledAudio,48000)/pw::frequency(60)))<15,"restored captured audio plays edited pitch");
        auto file=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("pw-export-test",".wav");
        restored.exportTake(file);juce::WavAudioFormat format;std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(file.createInputStream().release(),true));
        check(reader!=nullptr && reader->lengthInSamples>96000, "edited take exports as a real audio file");reader.reset();file.deleteFile();
        transport.samples=0;captured.clearNotes();check(captured.beginCapture(), "new capture arms after clearing");render(captured,transport,220,0.5,1);transport.samples=0;render(captured,transport,220,0.1,1);
        check(captured.captureFault.load() && !captured.capture.load(), "loop/seek during capture fails visibly without corrupting the take");captured.finishCapture();
        std::cout << "PASS captured audio, background editing, embedded session recall, WAV export, capture discontinuity checks\n";
    }
    PitchProcessor processor;
    processor.setStateInformation("invalid state", 13);
    check(processor.notes.count == 0, "invalid state does not corrupt notes");
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    check(editor != nullptr && editor->getWidth() >= 900, "editor constructs with controls");
    std::cout << "PASS host tests: capture, replay, state, bypass, UI construction.\n";
}
