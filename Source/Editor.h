#pragma once
#include "Processor.h"
#include <juce_gui_extra/juce_gui_extra.h>

class NoteCanvas final : public juce::Component {
public:
    explicit NoteCanvas(PitchProcessor& p) : processor(p) { setWantsKeyboardFocus(true); }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void fit();
    void undo();
private:
    PitchProcessor& processor;
    double origin = 0, pixelsPerSecond = 65;
    int selected = -1;
    float initialY = 0;
    double initialTarget = 60;
    uint64_t selectedRevision = 0;
    struct Change { int index; double before, after, start, end; uint64_t revision; };
    std::vector<Change> history;
    static constexpr int left = 54, top = 28, row = 17, highest = 84, lowest = 36;
    float yFor(double note) const { return top + static_cast<float>((highest - note) * row); }
    juce::Rectangle<float> rectangle(pw::NoteView) const;
    void remember(int, double, double);
};

class PitchEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit PitchEditor(PitchProcessor&);
    ~PitchEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    PitchProcessor& processor;
    juce::LookAndFeel_V4 look;
    juce::ComboBox mode, root, scale;
    juce::Slider speed, amount;
    juce::Label speedLabel, amountLabel, instruction, status;
    juce::TextButton capture{"Capture notes"}, clear{"Clear take"}, fit{"Fit take"}, undo{"Undo move"};
    juce::ToggleButton bypass{"Bypass"};
    NoteCanvas canvas;
    juce::Viewport viewport;
    using ChoiceAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<ChoiceAttachment> modeAttachment, rootAttachment, scaleAttachment;
    std::unique_ptr<SliderAttachment> speedAttachment, amountAttachment;
    std::unique_ptr<ButtonAttachment> bypassAttachment;
    int noticeTicks = 0;
    void timerCallback() override;
};
