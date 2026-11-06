#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Core.h"
#include "Shifter.h"
#include "Notes.h"
#include "Offline.h"
#include <thread>

class PitchProcessor final : public juce::AudioProcessor {
public:
    PitchProcessor();
    ~PitchProcessor() override;
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
    bool beginCapture();
    void finishCapture();
    void requestRender();
    void setNoteEdits(const std::vector<pw::NoteView>&);
    bool hasAudioTake() const { const juce::ScopedLock lock(stateLock); return capturedAudio && capturedAudio->frames > 0; }
    std::shared_ptr<const pw::AudioTake> audioTake() const { const juce::ScopedLock lock(stateLock); return capturedAudio; }
    std::atomic<bool> renderBusy{false}, captureFault{false};
    juce::String renderError() const { const juce::ScopedLock lock(errorLock); return renderFailure; }
    void exportTake(const juce::File&);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    std::shared_ptr<pw::AudioTake> capturedAudio;
    std::unique_ptr<pw::AudioTake> captureStorage, renderedOwner;
    std::atomic<pw::AudioTake*> captureDestination{nullptr}, renderedAudio{nullptr};
    std::atomic<int> captureWriters{0}, renderReaders{0};
    std::atomic<uint64_t> renderGeneration{0};
    std::thread renderer;
    mutable juce::CriticalSection stateLock, errorLock;
    juce::String renderFailure;
    void cancelRender();
    void discardRendered();

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
