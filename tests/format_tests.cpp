#include <juce_audio_utils/juce_audio_utils.h>
#include "Core.h"
#include <iostream>
#include <cstdlib>

namespace {
void check(bool success, const juce::String& message) {
    if (!success) { std::cerr << message << '\n'; std::exit(1); }
}
void setParameter(juce::AudioPluginInstance& instance, const juce::String& name, float normalised) {
    for (auto* p : instance.getParameters()) {
        if (p->getName(128) == name) { p->setValueNotifyingHost(normalised); return; }
    }
    check(false, "Missing native plug-in parameter: " + name);
}
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    check(argc == 2, "Pass the actual .vst3 bundle path");
    juce::AudioPluginFormatManager manager;
    auto* format = new juce::VST3PluginFormat(); manager.addFormat(format);
    juce::OwnedArray<juce::PluginDescription> descriptions;
    format->findAllTypesForFile(descriptions, juce::String::fromUTF8(argv[1]));
    check(descriptions.size() == 1, "Native VST3 did not expose exactly one audio effect");
    juce::String error;
    auto instance = manager.createPluginInstance(*descriptions[0], 48000, 512, error);
    check(instance != nullptr, "Native VST3 failed to load: " + error);
    check(!descriptions[0]->isInstrument, "Expected an audio effect, not an instrument");
    setParameter(*instance, "Retune time", 0.0f);
    setParameter(*instance, "Correction", 1.0f);
    instance->setRateAndBufferSizeDetails(48000, 512); instance->prepareToPlay(48000, 512);
    int channels = std::max(1, instance->getTotalNumInputChannels());
    pw::Detector detector; detector.prepare(48000); std::vector<double> measured;
    for (int position = 0; position < 48000 * 3; position += 512) {
        juce::AudioBuffer<float> b(channels, 512); juce::MidiBuffer midi;
        for (int i = 0; i < 512; ++i) {
            float x = static_cast<float>(0.3 * std::sin(2 * pw::pi * pw::frequency(68.65) * (position + i) / 48000));
            for (int ch = 0; ch < channels; ++ch) b.setSample(ch, i, x);
        }
        instance->processBlock(b, midi);
        for (int i = 0; i < b.getNumSamples(); ++i) {
            check(std::isfinite(b.getSample(0, i)), "Native VST3 produced non-finite audio");
            if (detector.push(b.getSample(0, i)) && position > 48000 && detector.value().voiced()) measured.push_back(detector.value().hz);
        }
    }
    check(!measured.empty(), "Native VST3 produced no measurable audio");
    std::sort(measured.begin(), measured.end());
    double cents = 1200 * std::log2(measured[measured.size() / 2] / 440);
    check(std::abs(cents) < 12, "Native VST3 missed its automatic target");
    juce::MemoryBlock saved; instance->getStateInformation(saved);
    check(saved.getSize() > 0, "Native VST3 saved an empty state");
    setParameter(*instance, "Correction", 0.0f);
    instance->setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    bool restored = false;
    for (auto* p : instance->getParameters()) if (p->getName(128) == "Correction") restored = p->getValue() > 0.99f;
    check(restored, "Native VST3 did not restore its saved correction parameter");
    std::unique_ptr<juce::AudioProcessorEditor> editor(instance->createEditorIfNeeded());
    check(editor != nullptr, "Native VST3 editor did not construct");
    editor.reset(); instance->releaseResources(); instance.reset();
    std::cout << "PASS native VST3 load, render, parameter, state and editor checks; pitch error " << cents << " cents\n";
}
