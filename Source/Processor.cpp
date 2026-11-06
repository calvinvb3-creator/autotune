#include "Processor.h"
#include "Editor.h"

juce::AudioProcessorValueTreeState::ParameterLayout PitchProcessor::layout() {
    juce::AudioProcessorValueTreeState::ParameterLayout p;
    p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"mode", 1}, "Mode", juce::StringArray{"Automatic", "Note editor"}, 0));
    p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"root", 1}, "Key", juce::StringArray{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}, 0));
    p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"scale", 1}, "Scale", juce::StringArray{"Chromatic", "Major", "Natural minor"}, 0));
    p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"speed", 1}, "Retune time", juce::NormalisableRange<float>(0, 250, 1, 0.5f), 65, juce::AudioParameterFloatAttributes().withLabel("ms")));
    p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"amount", 1}, "Correction", juce::NormalisableRange<float>(0, 100, 1), 100, juce::AudioParameterFloatAttributes().withLabel("%")));
    p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"bypass", 1}, "Bypass", false));
    return p;
}

PitchProcessor::PitchProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::mono(), true)
                                    .withOutput("Output", juce::AudioChannelSet::mono(), true)),
      parameters(*this, nullptr, "PitchWorkbench", layout()) {}

bool PitchProcessor::isBusesLayoutSupported(const BusesLayout& b) const {
    return b.getMainInputChannelSet() == b.getMainOutputChannelSet() &&
           (b.getMainInputChannelSet() == juce::AudioChannelSet::mono() || b.getMainInputChannelSet() == juce::AudioChannelSet::stereo());
}

void PitchProcessor::prepareToPlay(double sr, int) {
    if (!std::isfinite(sr) || sr < 8000 || sr > 192000) throw std::invalid_argument("Unsupported sample rate");
    rate = sr; detector.prepare(sr); shifter.prepare(sr, getTotalNumInputChannels());
    dryDelay = shifter.latency();
    for (auto& channel : dry) channel.assign(static_cast<size_t>(dryDelay), 0);
    previousEnd = -1; wasPlaying = wasCapture = false;
    setLatencySamples(dryDelay); resetStream();
}

void PitchProcessor::resetStream() {
    detector.reset(); correction.reset(); shifter.reset(); ratio = 1;
    notes.stopSegment(); sampleCounter = 0; dryCursor = 0;
    for (auto& channel : dry) std::fill(channel.begin(), channel.end(), 0);
    bypassMix = 0;
}

void PitchProcessor::processBlockBypassed(juce::AudioBuffer<float>& b, juce::MidiBuffer& m) {
    forceBypass = true; processBlock(b, m); forceBypass = false;
}

void PitchProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals guard;
    int channels = juce::jmin(2, buffer.getNumChannels());
    if (dryDelay == 0 || !channels) return;
    bool playing = false, transport = false;
    double start = previousEnd < 0 ? 0 : previousEnd;
    if (auto* ph = getPlayHead()) {
        if (auto position = ph->getPosition()) {
            playing = position->getIsPlaying();
            if (auto seconds = position->getTimeInSeconds()) { start = *seconds; transport = true; }
            else if (auto samples = position->getTimeInSamples()) { start = *samples / rate; transport = true; }
        }
    }
    hostPlaying = playing; hasTransport = transport; playheadSeconds = start;
    bool recording = capture.load() && playing && transport;
    bool jumped = transport && previousEnd >= 0 && std::abs(start - previousEnd) > 2.5 / rate;
    if ((playing && !wasPlaying) || (playing && jumped)) resetStream();
    if (!recording || !wasCapture) notes.stopSegment();
    wasCapture = recording; wasPlaying = playing;
    int mode = static_cast<int>(parameters.getRawParameterValue("mode")->load());
    int root = static_cast<int>(parameters.getRawParameterValue("root")->load());
    int scale = static_cast<int>(parameters.getRawParameterValue("scale")->load());
    double speed = parameters.getRawParameterValue("speed")->load();
    double amount = parameters.getRawParameterValue("amount")->load() * 0.01;
    bool bypass = forceBypass || parameters.getRawParameterValue("bypass")->load() > 0.5f;
    float frame[2]{}, wet[2]{};
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        double mono = 0;
        for (int ch = 0; ch < channels; ++ch) {
            float x = buffer.getSample(ch, i);
            frame[ch] = std::isfinite(x) ? x : 0; mono += frame[ch] / channels;
        }
        ++sampleCounter;
        if (detector.push(static_cast<float>(mono))) {
            auto pitch = detector.value();
            double time = start + (i - detector.centreDelay()) / rate;
            double measured = pitch.voiced() ? pw::midi(pitch.hz) : -1;
            detectedMidi = measured; confidence = pitch.confidence;
            double desired = 0;
            if (recording) notes.capture(time, pitch, sampleCounter / rate);
            if (pitch.voiced() && !recording) {
                if (mode == 0) desired = (pw::nearest(measured, root, scale) - measured) * amount;
                else if (transport && playing) {
                    double shift = 0;
                    if (notes.shiftAt(time, shift)) desired = shift * amount;
                }
            }
            ratio = correction.update(desired, speed, sampleCounter / rate);
            appliedCents = correction.value() * 100; sampleCounter = 0;
        }
        shifter.push(frame, wet, ratio);
        // Smooth both host bypass and the plug-in's bypass control. Dry audio
        // has the same reported latency as wet audio, avoiding comb filtering.
        float goal = (bypass || recording || amount == 0) ? 1.0f : 0.0f;
        bypassMix += std::clamp(goal - bypassMix, -1.0f / 128, 1.0f / 128);
        for (int ch = 0; ch < channels; ++ch) {
            float delayed = dry[ch][dryCursor]; dry[ch][dryCursor] = frame[ch];
            buffer.setSample(ch, i, wet[ch] + bypassMix * (delayed - wet[ch]));
        }
        dryCursor = (dryCursor + 1) % dryDelay;
    }
    previousEnd = start + buffer.getNumSamples() / rate;
    bufferErrors = shifter.errors();
}

void PitchProcessor::clearNotes() {
    const juce::ScopedLock lock(getCallbackLock());
    capture = false; notes.clear();
}

void PitchProcessor::getStateInformation(juce::MemoryBlock& out) {
    const juce::ScopedLock lock(getCallbackLock());
    auto xml = parameters.copyState().createXml();
    auto* list = xml->createNewChildElement("DetectedNotes");
    for (int i = 0; i < notes.count.load(); ++i) {
        auto n = notes.data[i].view(); auto* el = list->createNewChildElement("Note");
        el->setAttribute("start", n.start); el->setAttribute("end", n.end);
        el->setAttribute("original", n.original); el->setAttribute("target", n.target);
    }
    copyXmlToBinary(*xml, out);
}

void PitchProcessor::setStateInformation(const void* bytes, int size) {
    auto xml = getXmlFromBinary(bytes, size);
    if (!xml || !xml->hasTagName("PitchWorkbench")) return;
    const juce::ScopedLock lock(getCallbackLock());
    capture = false; notes.clear();
    if (auto* list = xml->getChildByName("DetectedNotes")) {
        for (auto* el : list->getChildIterator()) {
            if (!el->hasTagName("Note")) continue;
            pw::NoteView n{el->getDoubleAttribute("start"), el->getDoubleAttribute("end"),
                           el->getDoubleAttribute("original"), el->getDoubleAttribute("target")};
            if (!std::isfinite(n.start) || !std::isfinite(n.end) || !std::isfinite(n.original) || !std::isfinite(n.target) ||
                n.start < 0 || n.end <= n.start || n.original < 0 || n.original > 127 || n.target < 0 || n.target > 127) continue;
            n.target = std::clamp(n.target, n.original - 12, n.original + 12);
            int index = notes.count.load(); if (index >= pw::Notes::capacity) break;
            notes.data[index].set(n); notes.count = index + 1;
        }
    }
    xml->deleteAllChildElementsWithTagName("DetectedNotes");
    parameters.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorEditor* PitchProcessor::createEditor() { return new PitchEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PitchProcessor(); }
