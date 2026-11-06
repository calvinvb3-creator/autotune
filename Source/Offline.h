#pragma once
#include "Notes.h"
#include "Shifter.h"
#include <functional>
#include <memory>
#include <stdexcept>

namespace pw {
struct AudioTake {
    double rate=48000,origin=0;
    int channels=1,frames=0;
    std::array<std::vector<float>,2> samples;
    void allocate(int count,int ch) { channels=ch; frames=count; for(int c=0;c<channels;++c) samples[c].resize(count); }
    float at(int ch,double frame) const {
        if(frame<0||frame>=frames) return 0;
        int a=static_cast<int>(frame),b=std::min(a+1,frames-1);
        return samples[std::min(ch,channels-1)][a]+static_cast<float>(frame-a)*(samples[std::min(ch,channels-1)][b]-samples[std::min(ch,channels-1)][a]);
    }
};
inline std::vector<float> pitchCurve(const AudioTake& take,int begin,int length,const std::function<bool()>& cancelled=[] {return false;}) {
    Detector d; d.prepare(take.rate);
    std::vector<float> curve(length,std::numeric_limits<float>::quiet_NaN());
    float last=std::numeric_limits<float>::quiet_NaN();
    for(int i=0;i<length;++i) {
        if(i%512==0&&cancelled())throw std::runtime_error("Render cancelled");
        double x=0; for(int c=0;c<take.channels;++c) x+=take.samples[c][begin+i]/take.channels;
        if(d.push(static_cast<float>(x))&&d.value().voiced()) {
            int t=std::clamp(i-static_cast<int>(d.centreDelay()),0,length-1);
            float now=static_cast<float>(midi(d.value().hz));
            int start=std::max(0,t-static_cast<int>(take.rate*0.012));
            for(int j=start;j<=t;++j) curve[j]=now; last=now;
        }
    }
    float first=last;
    for(float x:curve) if(std::isfinite(x)) { first=x; break; }
    if(!std::isfinite(first)) first=60;
    last=first; for(float& x:curve) { if(std::isfinite(x)) last=x; else x=last; }
    return curve;
}
inline std::vector<std::vector<float>> stretch(const std::vector<std::vector<float>>& input,int outputFrames,double rate,const std::function<bool()>& cancelled) {
    int length=static_cast<int>(input[0].size()),channels=static_cast<int>(input.size());
    if(length==outputFrames) return input;
    if(length<1||outputFrames<1) throw std::runtime_error("Empty note duration");
    auto* state=rubberband_new(static_cast<unsigned>(rate),channels,0x00010000|0x01000000|0x20000000|0x10000000,static_cast<double>(outputFrames)/length,1);
    if(!state) throw std::runtime_error("Time stretcher could not initialise");
    std::unique_ptr<RubberBandState_,decltype(&rubberband_delete)> guard(state,rubberband_delete);
    rubberband_set_max_process_size(state,512); rubberband_set_expected_input_duration(state,length);
    for(int i=0;i<length;i+=512) {
        if(cancelled()) throw std::runtime_error("Render cancelled");
        const float* pointers[]={input[0].data()+i,channels>1?input[1].data()+i:input[0].data()+i};
        int n=std::min(512,length-i); rubberband_study(state,pointers,n,i+n==length);
    }
    std::vector<std::vector<float>> out(channels); std::array<std::array<float,1024>,2> block{};
    auto drain=[&] {
        while(rubberband_available(state)>0) {
            float* ptr[]={block[0].data(),block[1].data()};
            int n=static_cast<int>(rubberband_retrieve(state,ptr,std::min(1024,rubberband_available(state))));
            if(!n) break;
            for(int c=0;c<channels;++c) out[c].insert(out[c].end(),block[c].begin(),block[c].begin()+n);
        }
    };
    for(int i=0;i<length;i+=512) {
        if(cancelled()) throw std::runtime_error("Render cancelled");
        const float* pointers[]={input[0].data()+i,channels>1?input[1].data()+i:input[0].data()+i};
        int n=std::min(512,length-i); rubberband_process(state,pointers,n,i+n==length); drain();
    }
    drain();
    if(std::abs(static_cast<int>(out[0].size())-outputFrames)>4) throw std::runtime_error("Time stretcher returned an unexpected duration");
    for(auto& c:out) c.resize(outputFrames,0); return out;
}
inline bool edited(const NoteView& n) {
    return n.muted||std::abs(n.target-n.original)>1e-7||std::abs(n.start-n.sourceBegin())>1e-7||
        std::abs(n.end-n.sourceFinish())>1e-7||n.gainDb!=0||n.formant!=0||n.modulation!=1||n.drift!=1;
}
inline std::unique_ptr<AudioTake> renderTake(const AudioTake& source,const std::vector<NoteView>& notes,const std::function<bool()>& cancelled=[] {return false;}) {
    if(source.frames<=0||source.channels<1||source.channels>2) throw std::runtime_error("No captured audio");
    auto out=std::make_unique<AudioTake>(); out->rate=source.rate; out->origin=source.origin;
    double end=source.origin+source.frames/source.rate;
    for(const auto& n:notes) end=std::max(end,n.end);
    if(end-source.origin>600) throw std::runtime_error("Edited take exceeds ten minutes");
    out->allocate(static_cast<int>(std::ceil((end-source.origin)*source.rate)),source.channels);
    for(int c=0;c<source.channels;++c) std::copy_n(source.samples[c].begin(),source.frames,out->samples[c].begin());
    // Remove all edited source intervals before placing any destination notes.
    for(const auto& n:notes) if(edited(n)) {
        int begin=std::clamp(static_cast<int>(std::round((n.sourceBegin()-source.origin)*source.rate)),0,source.frames);
        int finish=std::clamp(static_cast<int>(std::round((n.sourceFinish()-source.origin)*source.rate)),begin,source.frames);
        int fade=std::min((finish-begin)/2,static_cast<int>(source.rate*0.005));
        for(int i=begin;i<finish;++i) {
            double mask=fade?std::min({1.0,static_cast<double>(i-begin)/fade,static_cast<double>(finish-1-i)/fade}):1;
            for(int c=0;c<source.channels;++c) out->samples[c][i]*=static_cast<float>(1-mask);
        }
    }
    for(const auto& n:notes) {
        if(cancelled()) throw std::runtime_error("Render cancelled");
        if(!edited(n)||n.muted) continue;
        int begin=std::clamp(static_cast<int>(std::round((n.sourceBegin()-source.origin)*source.rate)),0,source.frames);
        int finish=std::clamp(static_cast<int>(std::round((n.sourceFinish()-source.origin)*source.rate)),begin,source.frames);
        int length=finish-begin,destination=static_cast<int>(std::round((n.start-source.origin)*source.rate));
        int outputLength=static_cast<int>(std::round((n.end-n.start)*source.rate));
        if(length<16||outputLength<16||destination<0) throw std::runtime_error("Invalid edited note interval");
        std::vector<std::vector<float>> corrected(source.channels,std::vector<float>(length));
        bool pitchEdit=std::abs(n.target-n.original)>1e-7||n.modulation!=1||n.drift!=1||n.formant!=0;
        if(pitchEdit) {
            auto curve=pitchCurve(source,begin,length,cancelled);
            double sum=0,sumT=0,sumTT=0,sumTP=0;
            for(int i=0;i<length;++i) { double t=static_cast<double>(i)/length-0.5; sum+=curve[i];sumT+=t;sumTT+=t*t;sumTP+=t*curve[i]; }
            double centre=sum/length,slope=(sumTP-sumT*centre)/std::max(1e-12,sumTT-sumT*sumT/length);
            Shifter shifter; shifter.prepare(source.rate,source.channels); shifter.setFormant(n.formant);
            for(int i=0;i<length+shifter.latency();++i) {
                if((i%512)==0&&cancelled()) throw std::runtime_error("Render cancelled");
                int index=std::clamp(i-shifter.controlDelay(),0,length-1); double t=static_cast<double>(index)/length-0.5;
                double drift=slope*t,mod=curve[index]-centre-drift;
                double semitones=n.target-n.original+(n.drift-1)*drift+(n.modulation-1)*mod;
                float input[2]{},wet[2]{};
                if(i<length) for(int c=0;c<source.channels;++c) input[c]=source.samples[c][begin+i];
                shifter.push(input,wet,std::exp2(std::clamp(semitones,-12.0,12.0)/12));
                int j=i-shifter.latency(); if(j>=0) for(int c=0;c<source.channels;++c) corrected[c][j]=wet[c];
            }
            if(shifter.errors()) throw std::runtime_error("Pitch render buffer error");
        } else for(int c=0;c<source.channels;++c) std::copy_n(source.samples[c].begin()+begin,length,corrected[c].begin());
        auto changed=stretch(corrected,outputLength,source.rate,cancelled);
        double gain=std::pow(10,std::clamp(n.gainDb,-60.0,12.0)/20);
        int fadeIn=std::min(outputLength/2,static_cast<int>(std::max(0.005,n.fadeIn)*source.rate));
        int fadeOut=std::min(outputLength/2,static_cast<int>(std::max(0.005,n.fadeOut)*source.rate));
        for(int i=0;i<outputLength && destination+i<out->frames;++i) {
            double envelope=std::min({1.0,static_cast<double>(i)/std::max(1,fadeIn),static_cast<double>(outputLength-1-i)/std::max(1,fadeOut)});
            for(int c=0;c<source.channels;++c) { float value=changed[c][i]*static_cast<float>(gain*envelope); if(!std::isfinite(value)) throw std::runtime_error("Non-finite rendered audio"); out->samples[c][destination+i]+=value; }
        }
    }
    return out;
}
}
