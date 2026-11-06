#include "Editor.h"
namespace {
const juce::Colour ink(0xffe0e4e6),muted(0xff8b979f),background(0xff161b20),accent(0xffd6a665);
juce::String noteName(double n) {
    static const char* names[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    int v=static_cast<int>(std::round(n));return juce::String(names[pw::pitchClass(v)])+juce::String(v/12-1);
}
}
juce::Rectangle<float> NoteCanvas::rectangle(pw::NoteView n) const {
    return {left+static_cast<float>((n.start-origin)*pixelsPerSecond),yFor(n.target)-row*0.42f,std::max(5.0f,static_cast<float>((n.end-n.start)*pixelsPerSecond)),row*0.84f};
}
void NoteCanvas::sync() {
    auto v=processor.notes.revision.load();if(revision!=v) {selection.clear();past.clear();future.clear();revision=v;}
}
void NoteCanvas::paint(juce::Graphics& g) {
    sync();g.fillAll(background);
    for(int n=lowest;n<=highest;++n) {
        int pc=pw::pitchClass(n);bool black=pc==1||pc==3||pc==6||pc==8||pc==10;
        g.setColour(black?juce::Colour(0xff1a2128):juce::Colour(0xff232c34));g.fillRect(static_cast<float>(left),yFor(n)-row*0.5f,static_cast<float>(getWidth()-left),static_cast<float>(row));
        g.setColour(pc==0?ink:muted);g.setFont(11.0f);g.drawText(noteName(n),3,static_cast<int>(yFor(n))-8,left-9,16,juce::Justification::centredRight);
    }
    double step=pixelsPerSecond<25?5:1;
    for(double t=std::ceil(origin/step)*step;t<origin+getWidth()/pixelsPerSecond;t+=step) {
        int x=left+static_cast<int>((t-origin)*pixelsPerSecond);g.setColour(juce::Colour(0xff36434d));g.drawVerticalLine(x,top-6.0f,static_cast<float>(getHeight()));
        g.setColour(muted);g.drawText(juce::String(t,0)+"s",x+4,1,45,18,juce::Justification::left);
    }
    for(int i=0;i<processor.notes.count.load();++i) {
        auto n=processor.notes.data[i].view();auto r=rectangle(n);
        if(r.getRight()<left||r.getX()>getWidth())continue;
        auto clipped=r.getIntersection(juce::Rectangle<float>(static_cast<float>(left),0,static_cast<float>(getWidth()-left),static_cast<float>(getHeight())));
        g.setColour(n.muted?muted:selection.count(i)?juce::Colour(0xffffd192):accent);g.fillRoundedRectangle(clipped,2);
        g.setColour(background);if(r.getWidth()>28)g.drawText(noteName(n.target),clipped.toNearestInt(),juce::Justification::centred);
        g.setColour(juce::Colour(0xff738b9c));float y=yFor(n.original);if(y>top&&y<getHeight())g.drawLine(clipped.getX(),y,clipped.getRight(),y,1);
    }
    float x=left+static_cast<float>((processor.playheadSeconds.load()-origin)*pixelsPerSecond);
    if(x>=left&&x<getWidth()){g.setColour(juce::Colour(0xff79c5bd));g.drawVerticalLine(static_cast<int>(x),0,static_cast<float>(getHeight()));}
    if(processor.notes.count==0){g.setColour(ink);g.setFont(16.0f);g.drawText("Capture one isolated vocal pass, then stop capture to edit.",left+20,120,getWidth()-left-40,40,juce::Justification::centred);}
}
void NoteCanvas::changed() {revision=processor.notes.revision.load();repaint();if(selectionChanged)selectionChanged();}
void NoteCanvas::apply(const std::vector<pw::NoteView>& values) {
    if(processor.capture.load())return;
    past.push_back(processor.notes.snapshot());if(past.size()>50)past.erase(past.begin());future.clear();
    processor.setNoteEdits(values);changed();
}
void NoteCanvas::mouseDown(const juce::MouseEvent& e) {
    sync();grabKeyboardFocus();if(processor.capture.load())return;
    int hit=-1;for(int i=processor.notes.count.load()-1;i>=0;--i)if(rectangle(processor.notes.data[i].view()).contains(e.position)){hit=i;break;}
    if(hit<0){selection.clear();changed();return;}
    if(e.mods.isShiftDown()&&selection.count(hit))selection.erase(hit);
    else {if(!e.mods.isShiftDown()&&!selection.count(hit))selection.clear();selection.insert(hit);}
    initial=e.position;before=processor.notes.snapshot();changed();
}
void NoteCanvas::mouseDrag(const juce::MouseEvent& e) {
    if(processor.capture.load()||selection.empty()||before.size()!=static_cast<size_t>(processor.notes.count.load()))return;
    auto values=before;
    for(int i:selection) {
        auto& n=values[i];
        if(tool==0) {double d=(initial.y-e.position.y)/row;if(!e.mods.isAltDown())d=std::round(d);n.target=std::clamp(n.target+d,n.original-12,n.original+12);}
        else if(tool==1) {
            double d=(e.position.x-initial.x)/pixelsPerSecond;
            double minimum=processor.audioTake()?processor.audioTake()->origin:0;
            d=std::max(d,minimum-n.start);n.start+=d;n.end+=d;
        } else {
            double minimum=(n.sourceFinish()-n.sourceBegin())*0.25,maximum=(n.sourceFinish()-n.sourceBegin())*4;
            n.end=n.start+std::clamp(n.end-n.start+(e.position.x-initial.x)/pixelsPerSecond,std::max(0.025,minimum),maximum);
        }
        processor.notes.data[i].set(n);
    }
    repaint();if(selectionChanged)selectionChanged();
}
void NoteCanvas::mouseUp(const juce::MouseEvent& e) {
    if(processor.capture.load()||before.empty()||selection.empty())return;
    if(e.getDistanceFromDragStart()>1) {
        auto after=processor.notes.snapshot();processor.notes.replace(before);revision=processor.notes.revision.load();apply(after);
    }
    before.clear();
}
void NoteCanvas::mouseDoubleClick(const juce::MouseEvent&) {correct();}
void NoteCanvas::mouseWheelMove(const juce::MouseEvent& e,const juce::MouseWheelDetails& w) {
    if(e.mods.isCommandDown()||e.mods.isCtrlDown())pixelsPerSecond=std::clamp(pixelsPerSecond*std::exp(w.deltaY),8.0,600.0);
    else origin=std::max(0.0,origin-(w.deltaX+w.deltaY)*120/pixelsPerSecond);repaint();
}
bool NoteCanvas::keyPressed(const juce::KeyPress& k) {
    if(k.getModifiers().isCommandDown()&&k.getTextCharacter()=='z'){if(k.getModifiers().isShiftDown())redo();else undo();return true;}
    if(k.getModifiers().isCommandDown()&&k.getTextCharacter()=='a'){for(int i=0;i<processor.notes.count;++i)selection.insert(i);changed();return true;}
    if(selection.empty()||processor.capture.load())return false;
    if(k.getKeyCode()==juce::KeyPress::upKey||k.getKeyCode()==juce::KeyPress::downKey){auto a=processor.notes.snapshot();for(int i:selection)a[i].target=std::clamp(a[i].target+(k.getKeyCode()==juce::KeyPress::upKey?1:-1)*(k.getModifiers().isAltDown()?0.01:1),a[i].original-12,a[i].original+12);apply(a);return true;}
    return false;
}
void NoteCanvas::undo(){sync();if(past.empty()||processor.capture.load())return;future.push_back(processor.notes.snapshot());auto a=past.back();past.pop_back();processor.setNoteEdits(a);selection.clear();changed();}
void NoteCanvas::redo(){sync();if(future.empty()||processor.capture.load())return;past.push_back(processor.notes.snapshot());auto a=future.back();future.pop_back();processor.setNoteEdits(a);selection.clear();changed();}
void NoteCanvas::split(){auto a=processor.notes.snapshot();int i=selected();if(i>=0&&pw::splitNote(a,i,(a[i].start+a[i].end)*0.5)){apply(a);selection={i};}}
void NoteCanvas::merge(){auto a=processor.notes.snapshot();int i=selected();if(pw::mergeNote(a,i)){apply(a);selection={i};}}
void NoteCanvas::correct(){auto a=processor.notes.snapshot();for(int i:selection)a[i].target=pw::nearest(a[i].original,static_cast<int>(processor.parameters.getRawParameterValue("root")->load()),static_cast<int>(processor.parameters.getRawParameterValue("scale")->load()));if(!selection.empty())apply(a);}
void NoteCanvas::resetNotes(){auto a=processor.notes.snapshot();for(int i:selection){auto n=a[i];n.start=n.sourceBegin();n.end=n.sourceFinish();n.target=n.original;n.gainDb=n.formant=0;n.modulation=n.drift=1;n.muted=false;n.fadeIn=n.fadeOut=0.005;a[i]=n;}if(!selection.empty())apply(a);}
void NoteCanvas::setProperty(int field,double v){auto a=processor.notes.snapshot();for(int i:selection){auto& n=a[i];if(field==0)n.gainDb=v;if(field==1)n.formant=v;if(field==2)n.modulation=v/100;if(field==3)n.drift=v/100;if(field==4)n.fadeIn=n.fadeOut=v/1000;if(field==5)n.muted=v>0;}if(!selection.empty())apply(a);}
void NoteCanvas::fit(){double begin=std::numeric_limits<double>::max(),end=0,pitch=0;int count=processor.notes.count;for(int i=0;i<count;++i){auto n=processor.notes.data[i].view();begin=std::min(begin,n.start);end=std::max(end,n.end);pitch+=n.target;}origin=count?std::max(0.0,begin-0.1):0;pixelsPerSecond=std::clamp((getWidth()-left-20)/std::max(1.0,end-origin+0.2),8.0,600.0);if(auto* v=findParentComponentOfClass<juce::Viewport>())v->setViewPosition(0,std::max(0,static_cast<int>(yFor(count?pitch/count:60))-v->getHeight()/2));repaint();}

PitchEditor::PitchEditor(PitchProcessor& p):AudioProcessorEditor(p),processor(p),canvas(p) {
    look.setColour(juce::ResizableWindow::backgroundColourId,background);look.setColour(juce::ComboBox::backgroundColourId,juce::Colour(0xff29343e));look.setColour(juce::ComboBox::textColourId,ink);
    look.setColour(juce::TextButton::buttonColourId,juce::Colour(0xff303c46));look.setColour(juce::TextButton::textColourOffId,ink);look.setColour(juce::Slider::thumbColourId,accent);look.setColour(juce::Slider::trackColourId,accent);setLookAndFeel(&look);
    mode.addItemList({"Automatic","Note editor"},1);root.addItemList({"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"},1);scale.addItemList({"Chromatic","Major","Natural minor"},1);
    tool.addItemList({"Pitch tool","Move in time","Change length"},1);tool.setSelectedId(1);tool.onChange=[this]{canvas.tool=tool.getSelectedId()-1;};
    modeAttachment=std::make_unique<ChoiceAttachment>(p.parameters,"mode",mode);rootAttachment=std::make_unique<ChoiceAttachment>(p.parameters,"root",root);scaleAttachment=std::make_unique<ChoiceAttachment>(p.parameters,"scale",scale);
    for(auto* slider:{&speed,&amount,&gain,&formant,&modulation,&drift,&fade}){slider->setSliderStyle(juce::Slider::LinearHorizontal);slider->setTextBoxStyle(juce::Slider::TextBoxRight,false,70,22);}
    speed.setTextValueSuffix(" ms");amount.setTextValueSuffix(" %");gain.setRange(-60,12,0.1);gain.setTextValueSuffix(" dB");formant.setRange(-12,12,0.1);formant.setTextValueSuffix(" st");modulation.setRange(0,200,1);modulation.setTextValueSuffix(" %");drift.setRange(0,200,1);drift.setTextValueSuffix(" %");fade.setRange(5,250,1);fade.setTextValueSuffix(" ms");
    speedAttachment=std::make_unique<SliderAttachment>(p.parameters,"speed",speed);amountAttachment=std::make_unique<SliderAttachment>(p.parameters,"amount",amount);bypassAttachment=std::make_unique<ButtonAttachment>(p.parameters,"bypass",bypass);
    std::array<std::pair<juce::Label*,const char*>,8> labels={{{&speedLabel,"RETUNE"},{&amountLabel,"AUTO CORRECTION"},{&inspectorTitle,"SELECT A NOTE"},{&gainLabel,"LEVEL"},{&formantLabel,"FORMANTS"},{&modLabel,"MODULATION"},{&driftLabel,"LINEAR DRIFT"},{&fadeLabel,"EDGE FADES"}}};
    for(auto& x:labels){x.first->setText(x.second,juce::dontSendNotification);x.first->setColour(juce::Label::textColourId,ink);}
    instruction.setColour(juce::Label::textColourId,ink);status.setColour(juce::Label::textColourId,muted);
    capture.onClick=[this]{if(processor.capture.load())processor.finishCapture();else if(processor.notes.count==0)processor.beginCapture();};clear.onClick=[this]{processor.clearNotes();canvas.repaint();};fit.onClick=[this]{canvas.fit();};undo.onClick=[this]{canvas.undo();};redo.onClick=[this]{canvas.redo();};split.onClick=[this]{canvas.split();};merge.onClick=[this]{canvas.merge();};correct.onClick=[this]{canvas.correct();};reset.onClick=[this]{canvas.resetNotes();};
    std::array<juce::Slider*,5> inspectors={&gain,&formant,&modulation,&drift,&fade};for(int i=0;i<5;++i){auto* s=inspectors[i];s->onDragEnd=[this,s,i]{canvas.setProperty(i,s->getValue());};s->onValueChange=[this,s,i]{if(!s->isMouseButtonDown())canvas.setProperty(i,s->getValue());};}
    mute.onClick=[this]{canvas.setProperty(5,mute.getToggleState()?1:0);};canvas.selectionChanged=[this]{updateInspector();};
    exportAudio.onClick=[this]{chooser=std::make_unique<juce::FileChooser>("Export edited take",juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("PitchWorkbench-take.wav"),"*.wav");juce::Component::SafePointer<PitchEditor> safe(this);chooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting,[safe](const juce::FileChooser& c){if(safe&&c.getResult()!=juce::File())safe->processor.exportTake(c.getResult());});};
    for(juce::Component* c:std::initializer_list<juce::Component*>{&mode,&root,&scale,&tool,&speed,&amount,&speedLabel,&amountLabel,&capture,&clear,&fit,&undo,&redo,&split,&merge,&correct,&reset,&exportAudio,&bypass,&instruction,&status,&viewport,&gain,&formant,&modulation,&drift,&fade,&gainLabel,&formantLabel,&modLabel,&driftLabel,&fadeLabel,&inspectorTitle,&mute})addAndMakeVisible(c);
    viewport.setViewedComponent(&canvas,false);viewport.setScrollBarsShown(true,false);setSize(1180,720);setResizable(true,false);setResizeLimits(1060,600,1800,1200);viewport.setViewPosition(0,260);updateInspector();startTimerHz(15);
}
PitchEditor::~PitchEditor(){stopTimer();setLookAndFeel(nullptr);}
void PitchEditor::paint(juce::Graphics& g){g.fillAll(background);g.setColour(ink);g.setFont(23);g.drawText("PITCH WORKBENCH",20,12,380,35,juce::Justification::left);g.setColour(muted);g.setFont(12);g.drawText("MONOPHONIC NOTE EDITOR",410,20,330,22,juce::Justification::left);}
void PitchEditor::resized(){
    mode.setBounds(20,65,150,28);root.setBounds(180,65,65,28);scale.setBounds(255,65,140,28);speedLabel.setBounds(415,47,170,18);speed.setBounds(415,69,210,26);amountLabel.setBounds(640,47,180,18);amount.setBounds(640,69,210,26);bypass.setBounds(getWidth()-110,15,90,28);
    int x=20;for(auto* b:{&capture,&clear,&fit,&undo,&redo,&split,&merge,&correct,&reset}){b->setBounds(x,114,b==&capture?112:72,28);x+=b==&capture?120:80;}tool.setBounds(20,157,175,28);status.setBounds(210,157,getWidth()-230,28);instruction.setBounds(20,193,getWidth()-40,25);
    int inspectorX=getWidth()-240;viewport.setBounds(20,230,getWidth()-280,getHeight()-250);canvas.setSize(viewport.getWidth()-16,875);inspectorTitle.setBounds(inspectorX,230,220,30);
    int y=278;std::array<std::pair<juce::Label*,juce::Slider*>,5> controls={{{&gainLabel,&gain},{&formantLabel,&formant},{&modLabel,&modulation},{&driftLabel,&drift},{&fadeLabel,&fade}}};for(auto& c:controls){c.first->setBounds(inspectorX,y,220,20);c.second->setBounds(inspectorX,y+22,220,27);y+=65;}mute.setBounds(inspectorX,y,210,26);exportAudio.setBounds(inspectorX,getHeight()-55,220,30);
}
void PitchEditor::updateInspector(){int i=canvas.selected();bool valid=i>=0&&i<processor.notes.count&&!processor.capture.load();for(auto* s:{&gain,&formant,&modulation,&drift,&fade})s->setEnabled(valid&&processor.hasAudioTake());mute.setEnabled(valid&&processor.hasAudioTake());if(!valid){inspectorTitle.setText("SELECT A NOTE",juce::dontSendNotification);return;}auto n=processor.notes.data[i].view();inspectorTitle.setText(noteName(n.target)+"  /  "+juce::String(n.end-n.start,2)+" s",juce::dontSendNotification);gain.setValue(n.gainDb,juce::dontSendNotification);formant.setValue(n.formant,juce::dontSendNotification);modulation.setValue(n.modulation*100,juce::dontSendNotification);drift.setValue(n.drift*100,juce::dontSendNotification);fade.setValue(std::max(n.fadeIn,n.fadeOut)*1000,juce::dontSendNotification);mute.setToggleState(n.muted,juce::dontSendNotification);}
void PitchEditor::timerCallback(){
    if(!processor.capture.load()&&processor.captureFault.load())processor.finishCapture();
    bool armed=processor.capture.load(),busy=processor.renderBusy.load();capture.setButtonText(armed?"Stop capture":"Capture");capture.setEnabled(armed||processor.notes.count==0);clear.setEnabled(!armed);exportAudio.setEnabled(processor.hasAudioTake()&&!busy);
    bool manual=processor.parameters.getRawParameterValue("mode")->load()>0.5;tool.setEnabled(manual&&processor.hasAudioTake());speed.setEnabled(!manual);amount.setEnabled(!manual);
    juce::String text=juce::String(processor.notes.count.load())+" notes";if(busy)text+="  |  Rendering edits…";else if(processor.hasAudioTake())text+="  |  Captured audio ready";if(processor.bufferErrors)text+="  |  Audio buffer error";auto error=processor.renderError();if(error.isNotEmpty())text="Render failed: "+error;status.setText(text,juce::dontSendNotification);
    if(processor.captureFault.load())instruction.setText("Capture stopped: timeline jumped or the five-minute limit was reached. Clear and capture one continuous pass.",juce::dontSendNotification);
    else if(armed)instruction.setText("Play one continuous pass in Logic. Stop capture before editing. Do not loop while capturing.",juce::dontSendNotification);
    else if(manual)instruction.setText("Select a tool, then drag a note. Alt-drag for cents. Shift-click selects more notes. Cmd-A selects all. Split divides at the midpoint.",juce::dontSendNotification);
    else instruction.setText("Automatic: choose key and scale. For detailed editing, capture a take and switch to Note editor.",juce::dontSendNotification);
    canvas.repaint();
}
