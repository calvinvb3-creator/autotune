#pragma once
#include "Processor.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <set>

class NoteCanvas final : public juce::Component {
public:
    explicit NoteCanvas(PitchProcessor& p):processor(p) {setWantsKeyboardFocus(true);}
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&,const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void fit(); void undo(); void redo();
    void split(); void merge(); void correct(); void resetNotes();
    void setProperty(int field,double value);
    void apply(const std::vector<pw::NoteView>&);
    int selected() const {return selection.empty()?-1:*selection.begin();}
    int tool=0;
    std::function<void()> selectionChanged;
private:
    PitchProcessor& processor;
    double origin=0,pixelsPerSecond=80;
    std::set<int> selection;
    juce::Point<float> initial;
    std::vector<pw::NoteView> before;
    std::vector<std::vector<pw::NoteView>> past,future;
    uint64_t revision=0;
    static constexpr int left=54,top=28,row=17,highest=84,lowest=36;
    float yFor(double note) const {return top+static_cast<float>((highest-note)*row);}
    juce::Rectangle<float> rectangle(pw::NoteView n) const;
    void sync(); void changed();
};
class PitchEditor final : public juce::AudioProcessorEditor,private juce::Timer {
public:
    explicit PitchEditor(PitchProcessor&);
    ~PitchEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    PitchProcessor& processor;
    juce::LookAndFeel_V4 look;
    juce::ComboBox mode,root,scale,tool;
    juce::Slider speed,amount,gain,formant,modulation,drift,fade;
    juce::Label speedLabel,amountLabel,instruction,status,inspectorTitle,gainLabel,formantLabel,modLabel,driftLabel,fadeLabel;
    juce::TextButton capture{"Capture"},clear{"Clear"},fit{"Fit"},undo{"Undo"},redo{"Redo"},split{"Split"},merge{"Merge"},correct{"Correct"},reset{"Reset"},exportAudio{"Export WAV"};
    juce::ToggleButton bypass{"Bypass"},mute{"Mute note"};
    NoteCanvas canvas;
    juce::Viewport viewport;
    std::unique_ptr<juce::FileChooser> chooser;
    using ChoiceAttachment=juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAttachment=juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment=juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<ChoiceAttachment> modeAttachment,rootAttachment,scaleAttachment;
    std::unique_ptr<SliderAttachment> speedAttachment,amountAttachment;
    std::unique_ptr<ButtonAttachment> bypassAttachment;
    void timerCallback() override; void updateInspector();
};
