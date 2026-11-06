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
    const juce::ScopedLock stateGuard(stateLock);
    if(capture.load()) {capture=false;captureFault=true;}
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
    struct Reader {
        std::atomic<int>& count;
        explicit Reader(std::atomic<int>& c):count(c) { ++count; }
        ~Reader() { --count; }
    } playbackReader(renderReaders), captureWriter(captureWriters);
    auto* rendered = renderedAudio.load();
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
    if(!std::isfinite(start)||start<0) {start=0;transport=false;playing=false;}
    hostPlaying = playing; hasTransport = transport; playheadSeconds = start;
    bool recording = capture.load() && playing && transport;
    if(recording) if(auto* take=captureDestination.load()) {
        if(take->frames==0) take->origin=start;
        int offset=static_cast<int>(std::llround((start-take->origin)*rate));
        int limit=static_cast<int>(take->samples[0].size());
        if(std::abs(offset-take->frames)>2 || offset<0 || offset+buffer.getNumSamples()>limit) {
            captureFault=true; capture=false; recording=false;
        } else {
            for(int c=0;c<take->channels;++c) for(int i=0;i<buffer.getNumSamples();++i) {
                float value=buffer.getSample(std::min(c,channels-1),i);
                take->samples[c][offset+i]=std::isfinite(value)?value:0;
            }
            take->frames=offset+buffer.getNumSamples();
        }
    }
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
        float goal = (bypass || recording || (mode==0 && amount == 0)) ? 1.0f : 0.0f;
        bypassMix += std::clamp(goal - bypassMix, -1.0f / 128, 1.0f / 128);
        for (int ch = 0; ch < channels; ++ch) {
            float delayed = dry[ch][dryCursor]; dry[ch][dryCursor] = frame[ch];
            float processed=wet[ch];
            if(mode==1 && !recording && playing && transport && rendered) {
                double frame=(start+(i-dryDelay)/rate-rendered->origin)*rendered->rate;
                if(frame>=0 && frame<rendered->frames) processed=rendered->at(ch,frame);
            }
            buffer.setSample(ch, i, processed + bypassMix * (delayed - processed));
        }
        dryCursor = (dryCursor + 1) % dryDelay;
    }
    previousEnd = start + buffer.getNumSamples() / rate;
    bufferErrors = shifter.errors();
}

void PitchProcessor::clearNotes() {
    const juce::ScopedLock stateGuard(stateLock);
    const juce::ScopedLock lock(getCallbackLock());
    capture = false;
    while(captureWriters.load()>0) juce::Thread::sleep(1);
    captureDestination=nullptr; captureStorage.reset(); notes.clear();
    cancelRender(); discardRendered(); capturedAudio.reset();
}

void PitchProcessor::getStateInformation(juce::MemoryBlock& out) {
    const juce::ScopedLock stateGuard(stateLock);
    const juce::ScopedLock lock(getCallbackLock());
    auto xml = parameters.copyState().createXml();
    auto* list = xml->createNewChildElement("DetectedNotes");
    for (int i = 0; i < notes.count.load(); ++i) {
        auto n = notes.data[i].view(); auto* el = list->createNewChildElement("Note");
        el->setAttribute("start", n.start); el->setAttribute("end", n.end);
        el->setAttribute("original", n.original); el->setAttribute("target", n.target);
        el->setAttribute("sourceStart",n.sourceBegin()); el->setAttribute("sourceEnd",n.sourceFinish());
        el->setAttribute("gainDb",n.gainDb); el->setAttribute("formant",n.formant);
        el->setAttribute("modulation",n.modulation); el->setAttribute("drift",n.drift);
        el->setAttribute("fadeIn",n.fadeIn); el->setAttribute("fadeOut",n.fadeOut); el->setAttribute("muted",n.muted);
    }
    if(hasAudioTake()) {
        auto* audio=xml->createNewChildElement("SourceAudio"); auto source=capturedAudio;
        audio->setAttribute("rate",source->rate);audio->setAttribute("origin",source->origin);
        audio->setAttribute("frames",source->frames);audio->setAttribute("channels",source->channels);
        juce::MemoryOutputStream compressed;
        {
            juce::GZIPCompressorOutputStream gzip(&compressed,6,false);
            for(int c=0;c<source->channels;++c) gzip.write(source->samples[c].data(),static_cast<size_t>(source->frames)*sizeof(float));
        }
        audio->addTextElement(compressed.getMemoryBlock().toBase64Encoding());
    }
    copyXmlToBinary(*xml, out);
}

void PitchProcessor::setStateInformation(const void* bytes, int size) {
    const juce::ScopedLock stateGuard(stateLock);
    auto xml = getXmlFromBinary(bytes, size);
    if (!xml || !xml->hasTagName("PitchWorkbench")) return;
    const juce::ScopedLock lock(getCallbackLock());
    capture = false; while(captureWriters.load()>0) juce::Thread::sleep(1); captureDestination=nullptr; cancelRender(); discardRendered(); capturedAudio.reset(); notes.clear();
    if (auto* list = xml->getChildByName("DetectedNotes")) {
        for (auto* el : list->getChildIterator()) {
            if (!el->hasTagName("Note")) continue;
            pw::NoteView n{el->getDoubleAttribute("start"), el->getDoubleAttribute("end"),
                           el->getDoubleAttribute("original"), el->getDoubleAttribute("target")};
            if (!std::isfinite(n.start) || !std::isfinite(n.end) || !std::isfinite(n.original) || !std::isfinite(n.target) ||
                n.start < 0 || n.end <= n.start || n.original < 0 || n.original > 127 || n.target < 0 || n.target > 127) continue;
            n.sourceStart=el->getDoubleAttribute("sourceStart",n.start); n.sourceEnd=el->getDoubleAttribute("sourceEnd",n.end);
            n.gainDb=el->getDoubleAttribute("gainDb",0);n.formant=el->getDoubleAttribute("formant",0);
            n.modulation=el->getDoubleAttribute("modulation",1);n.drift=el->getDoubleAttribute("drift",1);
            n.fadeIn=el->getDoubleAttribute("fadeIn",0.005);n.fadeOut=el->getDoubleAttribute("fadeOut",0.005);n.muted=el->getBoolAttribute("muted",false);
            if(!std::isfinite(n.sourceStart)||!std::isfinite(n.sourceEnd)||n.sourceEnd<=n.sourceStart||
               !std::isfinite(n.gainDb)||!std::isfinite(n.formant)||!std::isfinite(n.modulation)||!std::isfinite(n.drift)||
               !std::isfinite(n.fadeIn)||!std::isfinite(n.fadeOut)) continue;
            n.gainDb=std::clamp(n.gainDb,-60.0,12.0); n.formant=std::clamp(n.formant,-12.0,12.0);
            n.modulation=std::clamp(n.modulation,0.0,2.0);n.drift=std::clamp(n.drift,0.0,2.0);
            n.fadeIn=std::clamp(n.fadeIn,0.0,1.0);n.fadeOut=std::clamp(n.fadeOut,0.0,1.0);
            n.target = std::clamp(n.target, n.original - 12, n.original + 12);
            int index = notes.count.load(); if (index >= pw::Notes::capacity) break;
            notes.data[index].set(n); notes.count = index + 1;
        }
    }
    if(auto* element=xml->getChildByName("SourceAudio")) {
        double sr=element->getDoubleAttribute("rate"),origin=element->getDoubleAttribute("origin");
        int frames=element->getIntAttribute("frames"),channels=element->getIntAttribute("channels");
        if(std::isfinite(sr)&&sr>=8000&&sr<=192000&&std::isfinite(origin)&&origin>=0&&frames>0&&frames<=sr*300&&channels>=1&&channels<=2) {
            juce::MemoryBlock packed;
            if(packed.fromBase64Encoding(element->getAllSubText())) {
                juce::MemoryInputStream input(packed,false);juce::GZIPDecompressorInputStream gzip(&input,false);
                auto source=std::make_shared<pw::AudioTake>();source->rate=sr;source->origin=origin;source->allocate(frames,channels);
                bool valid=true;
                for(int c=0;c<channels;++c) {
                    size_t bytes=static_cast<size_t>(frames)*sizeof(float),done=0;
                    auto* dest=reinterpret_cast<char*>(source->samples[c].data());
                    while(done<bytes) {int n=gzip.read(dest+done,static_cast<int>(std::min(bytes-done,static_cast<size_t>(65536))));if(n<=0)break;done+=n;}
                    if(done!=bytes) valid=false;
                    for(float v:source->samples[c]) if(!std::isfinite(v)) valid=false;
                }
                if(valid) capturedAudio=source;
            }
        }
    }
    xml->deleteAllChildElementsWithTagName("SourceAudio");
    xml->deleteAllChildElementsWithTagName("DetectedNotes");
    parameters.replaceState(juce::ValueTree::fromXml(*xml));
    requestRender();
}

juce::AudioProcessorEditor* PitchProcessor::createEditor() { return new PitchEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PitchProcessor(); }

PitchProcessor::~PitchProcessor() {
    capture=false; cancelRender(); discardRendered();
}
void PitchProcessor::cancelRender() {
    ++renderGeneration; if(renderer.joinable()) renderer.join(); renderBusy=false;
}
void PitchProcessor::discardRendered() {
    renderedAudio=nullptr;
    while(renderReaders.load()>0) juce::Thread::sleep(1);
    renderedOwner.reset();
}
bool PitchProcessor::beginCapture() {
    const juce::ScopedLock stateGuard(stateLock);
    if(capture.load()) return false;
    cancelRender();
    while(captureWriters.load()>0) juce::Thread::sleep(1);
    auto take=std::make_unique<pw::AudioTake>(); take->rate=rate; take->channels=getTotalNumInputChannels();
    try { take->allocate(static_cast<int>(rate*300),std::clamp(take->channels,1,2)); }
    catch(const std::bad_alloc&) { const juce::ScopedLock lock(errorLock); renderFailure="Not enough memory to capture. Close other applications."; return false; }
    take->frames=0; captureDestination=nullptr; captureStorage=std::move(take);
    notes.clear(); capturedAudio.reset(); discardRendered(); captureFault=false;
    captureDestination=captureStorage.get(); capture=true; return true;
}
void PitchProcessor::finishCapture() {
    const juce::ScopedLock stateGuard(stateLock);
    capture=false;
    while(captureWriters.load()>0) juce::Thread::sleep(1);
    captureDestination=nullptr;
    if(captureStorage && captureStorage->frames>0) {
        for(int c=0;c<captureStorage->channels;++c) captureStorage->samples[c].resize(captureStorage->frames);
        capturedAudio=std::shared_ptr<pw::AudioTake>(captureStorage.release());
        for(int i=0;i<notes.count.load();++i) {
            auto n=notes.data[i].view();
            n.start=std::max(n.start,capturedAudio->origin); n.end=std::min(n.end,capturedAudio->origin+capturedAudio->frames/capturedAudio->rate);
            n.sourceStart=n.start; n.sourceEnd=n.end; notes.data[i].set(n);
        }
        requestRender();
    }
}
void PitchProcessor::setNoteEdits(const std::vector<pw::NoteView>& edits) {
    const juce::ScopedLock stateGuard(stateLock);
    if(capture.load()) return;
    notes.replace(edits); requestRender();
}
void PitchProcessor::requestRender() {
    const juce::ScopedLock stateGuard(stateLock);
    if(!hasAudioTake()||capture.load()) return;
    cancelRender();
    auto source=capturedAudio; auto plan=notes.snapshot(); uint64_t generation=renderGeneration.load();
    {const juce::ScopedLock lock(errorLock);renderFailure.clear();}
    renderBusy=true;
    renderer=std::thread([this,source,plan,generation] {
        try {
            auto result=pw::renderTake(*source,plan,[&] {return renderGeneration.load()!=generation;});
            if(renderGeneration.load()==generation) {
                auto old=std::move(renderedOwner); renderedOwner=std::move(result); renderedAudio=renderedOwner.get();
                while(renderReaders.load()>0) juce::Thread::sleep(1);
            }
        } catch(const std::exception& e) {
            if(renderGeneration.load()==generation) {const juce::ScopedLock lock(errorLock);renderFailure=e.what();}
        }
        if(renderGeneration.load()==generation) renderBusy=false;
    });
}
void PitchProcessor::exportTake(const juce::File& file) {
    const juce::ScopedLock stateGuard(stateLock);
    if(!hasAudioTake()||renderBusy.load()) return;
    auto* audio=renderedAudio.load(); if(!audio) return;
    juce::TemporaryFile temporary(file);
    auto stream=temporary.getFile().createOutputStream(); if(!stream) return;
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.release(),audio->rate,static_cast<unsigned>(audio->channels),24,{},0));
    if(!writer) return;
    std::array<const float*,2> ptr{}; for(int c=0;c<audio->channels;++c) ptr[c]=audio->samples[c].data();
    bool ok=writer->writeFromFloatArrays(ptr.data(),audio->channels,audio->frames); writer.reset();
    if(!ok||!temporary.overwriteTargetFileWithTemporary()) {const juce::ScopedLock lock(errorLock);renderFailure="Could not save the exported WAV file.";}
}
