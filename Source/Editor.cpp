#include "Editor.h"

namespace {
const juce::Colour ink(0xffcbd3d9), muted(0xff7f8c97), background(0xff141a20), accent(0xffd39b5a);
juce::String noteName(double n) {
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int value = static_cast<int>(std::round(n));
    return juce::String(names[pw::pitchClass(value)]) + juce::String(value / 12 - 1);
}
}

juce::Rectangle<float> NoteCanvas::rectangle(pw::NoteView n) const {
    return {left + static_cast<float>((n.start - origin) * pixelsPerSecond), yFor(n.target) - row * 0.42f,
            std::max(5.0f, static_cast<float>((n.end - n.start) * pixelsPerSecond)), row * 0.84f};
}

void NoteCanvas::paint(juce::Graphics& g) {
    g.fillAll(background);
    for (int note = lowest; note <= highest; ++note) {
        float y = yFor(note);
        int pc = pw::pitchClass(note);
        bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
        g.setColour(black ? juce::Colour(0xff1a222a) : juce::Colour(0xff202931));
        g.fillRect(static_cast<float>(left), y - row * 0.5f, static_cast<float>(getWidth() - left), static_cast<float>(row));
        g.setColour(pc == 0 ? ink : muted); g.setFont(11.0f);
        g.drawText(noteName(note), 3, static_cast<int>(y) - 8, left - 9, 16, juce::Justification::centredRight);
    }
    g.setColour(juce::Colour(0xff35404a));
    double step = pixelsPerSecond < 30 ? 5 : 1;
    for (double second = std::ceil(origin / step) * step; second < origin + getWidth() / pixelsPerSecond; second += step) {
        int x = left + static_cast<int>((second - origin) * pixelsPerSecond);
        g.drawVerticalLine(x, top - 6.0f, static_cast<float>(getHeight()));
        g.setColour(muted); g.drawText(juce::String(second, 0) + "s", x + 4, 3, 48, 18, juce::Justification::left);
        g.setColour(juce::Colour(0xff35404a));
    }
    for (int i = 0; i < processor.notes.count.load(); ++i) {
        auto n = processor.notes.data[i].view(); auto r = rectangle(n);
        if (r.getRight() < left || r.getX() > getWidth()) continue;
        auto clipped = r.getIntersection(juce::Rectangle<float>(static_cast<float>(left), 0, static_cast<float>(getWidth() - left), static_cast<float>(getHeight())));
        g.setColour(i == selected ? juce::Colour(0xffffc27c) : accent); g.fillRoundedRectangle(clipped, 2);
        g.setColour(juce::Colour(0xff152029));
        if (r.getWidth() > 30) g.drawText(noteName(n.target), clipped.toNearestInt(), juce::Justification::centred);
        g.setColour(juce::Colour(0xff6e8394));
        float original = yFor(n.original);
        if (original > top && original < getHeight()) g.drawLine(clipped.getX(), original, clipped.getRight(), original, 1.0f);
    }
    float playX = left + static_cast<float>((processor.playheadSeconds.load() - origin) * pixelsPerSecond);
    if (playX >= left && playX < getWidth()) { g.setColour(juce::Colour(0xff6dc3bd)); g.drawVerticalLine(static_cast<int>(playX), 0, static_cast<float>(getHeight())); }
    if (!processor.notes.count.load()) {
        g.setColour(ink); g.setFont(16.0f);
        g.drawText("Capture a vocal take to edit its notes", left + 20, 120, getWidth() - left - 40, 30, juce::Justification::centred);
    }
}

void NoteCanvas::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus(); selected = -1;
    if (processor.capture.load()) return;
    for (int i = processor.notes.count.load() - 1; i >= 0; --i) {
        auto n = processor.notes.data[i].view();
        if (rectangle(n).contains(e.position)) { selected = i; selectedRevision = processor.notes.revision.load(); initialY = e.position.y; initialTarget = n.target; break; }
    }
    repaint();
}
void NoteCanvas::mouseDrag(const juce::MouseEvent& e) {
    if (processor.capture.load() || selectedRevision != processor.notes.revision.load() || selected < 0 || selected >= processor.notes.count.load()) return;
    double target = initialTarget + (initialY - e.position.y) / row;
    if (!e.mods.isShiftDown()) target = std::round(target);
    double original = processor.notes.data[selected].original.load();
    processor.notes.data[selected].target = std::clamp(target, std::max(24.0, original - 12), std::min(96.0, original + 12)); repaint();
}
void NoteCanvas::remember(int index, double before, double after) {
    if (index < 0 || index >= processor.notes.count.load() || before == after) return;
    auto n = processor.notes.data[index].view();
    history.push_back({index, before, after, n.start, n.end, processor.notes.revision.load()});
    if (history.size() > 128) history.erase(history.begin());
}
void NoteCanvas::mouseUp(const juce::MouseEvent&) {
    if (selectedRevision == processor.notes.revision.load() && selected >= 0 && selected < processor.notes.count.load()) remember(selected, initialTarget, processor.notes.data[selected].target.load());
}
void NoteCanvas::mouseDoubleClick(const juce::MouseEvent&) {
    if (processor.capture.load() || selectedRevision != processor.notes.revision.load() || selected < 0 || selected >= processor.notes.count.load()) return;
    auto n = processor.notes.data[selected].view();
    remember(selected, n.target, n.original); processor.notes.data[selected].target = n.original; repaint();
}
void NoteCanvas::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
    if (e.mods.isCommandDown() || e.mods.isCtrlDown()) pixelsPerSecond = std::clamp(pixelsPerSecond * std::exp(w.deltaY), 8.0, 500.0);
    else origin = std::max(0.0, origin - (w.deltaX + w.deltaY) * 120.0 / pixelsPerSecond);
    repaint();
}
bool NoteCanvas::keyPressed(const juce::KeyPress& k) {
    if (k.getModifiers().isCommandDown() && k.getTextCharacter() == 'z') { undo(); return true; }
    if (selectedRevision != processor.notes.revision.load() || selected < 0 || selected >= processor.notes.count.load() || processor.capture.load()) return false;
    if (k.getKeyCode() == juce::KeyPress::upKey || k.getKeyCode() == juce::KeyPress::downKey) {
        double old = processor.notes.data[selected].target.load();
        double original = processor.notes.data[selected].original.load();
        double value = std::clamp(old + (k.getKeyCode() == juce::KeyPress::upKey ? 1 : -1), std::max(24.0, original - 12), std::min(96.0, original + 12));
        remember(selected, old, value); processor.notes.data[selected].target = value; repaint(); return true;
    }
    return false;
}
void NoteCanvas::undo() {
    if (history.empty() || processor.capture.load()) return;
    auto c = history.back(); history.pop_back();
    if (c.revision == processor.notes.revision.load() && c.index < processor.notes.count.load()) {
        auto n = processor.notes.data[c.index].view();
        if (n.start == c.start && n.end == c.end && n.target == c.after) processor.notes.data[c.index].target = c.before;
    }
    repaint();
}
void NoteCanvas::fit() {
    double start = std::numeric_limits<double>::max(), end = 5, pitchTotal = 0;
    int count = processor.notes.count.load();
    for (int i = 0; i < count; ++i) {
        auto n = processor.notes.data[i].view();
        start = std::min(start, n.start); end = std::max(end, n.end); pitchTotal += n.target;
    }
    origin = count ? std::max(0.0, start - 0.25) : 0;
    pixelsPerSecond = std::clamp((getWidth() - left - 20) / (end - origin + 0.25), 8.0, 500.0);
    if (auto* view = findParentComponentOfClass<juce::Viewport>()) {
        int y = static_cast<int>(yFor(count ? pitchTotal / count : 60)) - view->getHeight() / 2;
        view->setViewPosition(0, std::max(0, y));
    }
    repaint();
}

PitchEditor::PitchEditor(PitchProcessor& p) : AudioProcessorEditor(p), processor(p), canvas(p) {
    look.setColour(juce::ResizableWindow::backgroundColourId, background);
    look.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff29333e));
    look.setColour(juce::ComboBox::textColourId, ink);
    look.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff303c47));
    look.setColour(juce::TextButton::textColourOffId, ink);
    look.setColour(juce::Slider::thumbColourId, accent);
    look.setColour(juce::Slider::trackColourId, accent);
    setLookAndFeel(&look);
    mode.addItemList({"Automatic", "Note editor"}, 1);
    root.addItemList({"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}, 1);
    scale.addItemList({"Chromatic", "Major", "Natural minor"}, 1);
    modeAttachment = std::make_unique<ChoiceAttachment>(p.parameters, "mode", mode);
    rootAttachment = std::make_unique<ChoiceAttachment>(p.parameters, "root", root);
    scaleAttachment = std::make_unique<ChoiceAttachment>(p.parameters, "scale", scale);
    for (auto* slider : {&speed, &amount}) {
        slider->setSliderStyle(juce::Slider::LinearHorizontal);
        slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 65, 24);
    }
    speed.setTextValueSuffix(" ms"); amount.setTextValueSuffix(" %");
    speedAttachment = std::make_unique<SliderAttachment>(p.parameters, "speed", speed);
    amountAttachment = std::make_unique<SliderAttachment>(p.parameters, "amount", amount);
    bypassAttachment = std::make_unique<ButtonAttachment>(p.parameters, "bypass", bypass);
    speedLabel.setText("RETUNE TIME", juce::dontSendNotification);
    amountLabel.setText("CORRECTION", juce::dontSendNotification);
    for (auto* label : {&speedLabel, &amountLabel, &instruction, &status}) label->setColour(juce::Label::textColourId, ink);
    capture.onClick = [this] {
        if (!processor.capture.load() && processor.notes.count.load() != 0) {
            noticeTicks = 100;
            instruction.setText("Clear the current take before capturing again. Existing edits are preserved.", juce::dontSendNotification); return;
        }
        processor.capture = !processor.capture.load();
    };
    clear.onClick = [this] { processor.clearNotes(); canvas.repaint(); };
    fit.onClick = [this] { canvas.fit(); };
    undo.onClick = [this] { canvas.undo(); };
    for (juce::Component* c : std::initializer_list<juce::Component*>{&mode, &root, &scale, &speed, &amount, &speedLabel, &amountLabel, &capture, &clear, &fit, &undo, &bypass, &instruction, &status, &viewport}) addAndMakeVisible(c);
    viewport.setViewedComponent(&canvas, false); viewport.setScrollBarsShown(true, false);
    setSize(1020, 650); setResizable(true, false); setResizeLimits(900, 540, 1600, 1100);
    viewport.setViewPosition(0, 260); startTimerHz(20);
}
PitchEditor::~PitchEditor() { stopTimer(); setLookAndFeel(nullptr); }
void PitchEditor::paint(juce::Graphics& g) {
    g.fillAll(background); g.setColour(ink); g.setFont(24.0f);
    g.drawText("PITCH WORKBENCH", 22, 14, 340, 34, juce::Justification::left);
    g.setColour(muted); g.setFont(12.0f);
    g.drawText("DEVELOPMENT BUILD  /  MONOPHONIC VOCALS", 365, 22, 420, 22, juce::Justification::left);
    g.setColour(juce::Colour(0xff35404a)); g.drawHorizontalLine(132, 20, static_cast<float>(getWidth() - 20));
}
void PitchEditor::resized() {
    mode.setBounds(22, 65, 155, 28); root.setBounds(188, 65, 65, 28); scale.setBounds(264, 65, 145, 28);
    speedLabel.setBounds(430, 47, 190, 20); speed.setBounds(430, 69, 220, 27);
    amountLabel.setBounds(668, 47, 180, 20); amount.setBounds(668, 69, 220, 27);
    bypass.setBounds(getWidth() - 112, 16, 92, 30);
    capture.setBounds(22, 148, 140, 30); clear.setBounds(174, 148, 100, 30);
    fit.setBounds(286, 148, 90, 30); undo.setBounds(388, 148, 105, 30);
    status.setBounds(505, 148, getWidth() - 525, 30);
    instruction.setBounds(22, 187, getWidth() - 44, 30);
    viewport.setBounds(22, 225, getWidth() - 44, getHeight() - 247);
    canvas.setSize(viewport.getWidth() - 16, 875);
}
void PitchEditor::timerCallback() {
    bool armed = processor.capture.load();
    capture.setButtonText(armed ? "Stop capture" : "Capture notes");
    capture.setColour(juce::TextButton::buttonColourId, armed ? juce::Colour(0xff864b38) : juce::Colour(0xff303c47));
    double n = processor.detectedMidi.load();
    juce::String text = n >= 0 ? noteName(n) + "  |  " + juce::String(processor.appliedCents.load(), 0) + " cents" : "No voiced pitch";
    text += "  |  " + juce::String(processor.notes.count.load()) + " notes";
    if (processor.bufferErrors.load()) text = "AUDIO BUFFER ERROR: " + juce::String(processor.bufferErrors.load());
    status.setText(text, juce::dontSendNotification);
    if (noticeTicks > 0) --noticeTicks;
    else if (armed && processor.notes.count.load() >= pw::Notes::capacity)
        instruction.setText("Capture limit reached. Stop capture to edit this take.", juce::dontSendNotification);
    else if (armed && !processor.hasTransport.load())
        instruction.setText("Capture needs the host timeline. Start playback in Logic to capture notes.", juce::dontSendNotification);
    else if (armed) instruction.setText("Play one pass in Logic, then stop capture. Capture monitors the uncorrected vocal. Avoid looping.", juce::dontSendNotification);
    else if (processor.parameters.getRawParameterValue("mode")->load() < 0.5f)
        instruction.setText("Automatic: choose key and scale. Lower retune time gives faster correction. Use an isolated vocal track.", juce::dontSendNotification);
    else instruction.setText("Note editor: drag notes vertically, Shift-drag for cents. Double-click restores measured pitch. Replay the same region.", juce::dontSendNotification);
    canvas.repaint();
}
