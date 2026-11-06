#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Core.h"
#include "Shifter.h"
#include "Notes.h"

class PitchProcessor final : public juce::AudioProcessor {
public:
    PitchProcessor();
    const juce::String getName() const override { return "Pitch Workbench"; }
    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    double getTailLengthSeconds() const override { return 0.5; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    juce::AudioProcessorValueTreeState parameters;
    pw::Notes notes;
    std::atomic<bool> capture{false}, hostPlaying{false}, hasTransport{false};
    std::atomic<double> playheadSeconds{0}, detectedMidi{-1}, confidence{0}, appliedCents{0};
    std::atomic<int> bufferErrors{0};
    void clearNotes();
private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    pw::Detector detector;
    pw::Correction correction;
    pw::Shifter shifter;
    double rate = 48000, ratio = 1, previousEnd = -1;
    int sampleCounter = 0;
    bool wasPlaying = false, wasCapture = false, forceBypass = false;
    std::array<std::vector<float>, 2> dry;
    int dryCursor = 0, dryDelay = 0;
    float bypassMix = 0;
    void resetStream();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PitchProcessor)
};
