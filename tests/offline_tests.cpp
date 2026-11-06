#include "Offline.h"
#include <iostream>
#include <cstdlib>
#include <fstream>
void need(bool b,const char* m){if(!b){std::cerr<<"FAIL "<<m<<'\n';std::exit(1);}}
double rms(const pw::AudioTake& a,double start,double end){double s=0;int begin=static_cast<int>(start*a.rate),finish=std::min(a.frames,static_cast<int>(end*a.rate));for(int i=begin;i<finish;++i)s+=a.samples[0][i]*a.samples[0][i];return std::sqrt(s/std::max(1,finish-begin));}
double pitch(const pw::AudioTake& a,double start,double end){pw::Detector d;d.prepare(a.rate);std::vector<double> measured;for(int i=static_cast<int>(start*a.rate);i<std::min(a.frames,static_cast<int>(end*a.rate));++i)if(d.push(a.samples[0][i])&&d.value().voiced())measured.push_back(d.value().hz);need(!measured.empty(),"pitched output exists");std::sort(measured.begin(),measured.end());return measured[measured.size()/2];}
void wav(const pw::AudioTake& a,const std::string& path){std::ofstream f(path,std::ios::binary);auto u16=[&](unsigned n){f.put(static_cast<char>(n));f.put(static_cast<char>(n>>8));};auto u32=[&](unsigned n){u16(n);u16(n>>16);};f.write("RIFF",4);u32(36+a.frames*2);f.write("WAVEfmt ",8);u32(16);u16(1);u16(1);u32(static_cast<unsigned>(a.rate));u32(static_cast<unsigned>(a.rate)*2);u16(2);u16(16);f.write("data",4);u32(a.frames*2);for(int i=0;i<a.frames;++i)u16(static_cast<uint16_t>(static_cast<int16_t>(std::clamp(a.samples[0][i],-1.0f,1.0f)*32767)));}
int main(int argc,char**argv){
    std::string output=argc>1?argv[1]:".";
    for(double rate:{44100.0,48000.0,96000.0})for(int ch:{1,2}){
        pw::AudioTake source;source.rate=rate;source.allocate(static_cast<int>(rate*4),ch);
        for(int i=static_cast<int>(rate*0.5);i<static_cast<int>(rate*2.5);++i){double phase=2*pw::pi*220*i/rate;for(int c=0;c<ch;++c)source.samples[c][i]=static_cast<float>(0.25*std::sin(phase)+0.04*std::sin(phase*2));}
        pw::NoteView n{0.5,2.5,57,57};n.sourceStart=0.5;n.sourceEnd=2.5;
        auto original=pw::renderTake(source,{n});need(original->samples[0]==source.samples[0],"unedited audio is bit-identical");
        n.target=60;n.start=1;n.end=3.8;
        auto edited=pw::renderTake(source,{n});
        need(std::abs(1200*std::log2(pitch(*edited,1.7,3.2)/pw::frequency(60)))<15,"pitch shift plus time stretch stays in tune");
        need(rms(*edited,0.65,0.85)<1e-6,"moving removes original note");need(rms(*edited,3.2,3.6)>0.06,"duration actually extends audio");
        for(int c=0;c<ch;++c)for(float v:edited->samples[c])need(std::isfinite(v),"finite offline output");
        if(ch==2)for(int i=0;i<edited->frames;++i)need(std::abs(edited->samples[0][i]-edited->samples[1][i])<1e-5,"offline stereo stays aligned");
        n.start=0.5;n.end=2.5;n.target=57;n.gainDb=-6;
        auto quieter=pw::renderTake(source,{n});double db=20*std::log10(rms(*quieter,1,2)/rms(source,1,2));need(std::abs(db+6)<0.1,"per-note gain changes actual amplitude");
        n.muted=true;auto muted=pw::renderTake(source,{n});need(rms(*muted,1,2)<1e-8,"per-note mute changes actual audio");
        std::vector<pw::NoteView> split{n};need(pw::splitNote(split,0,1.5),"note splits");need(split.size()==2&&std::abs(split[0].sourceFinish()-split[1].sourceBegin())<1e-9,"split keeps source continuity");need(pw::mergeNote(split,0)&&split.size()==1,"split notes merge");
        if(rate==48000&&ch==1){wav(source,output+"/offline-source.wav");wav(*edited,output+"/offline-moved-stretched-C4.wav");wav(*quieter,output+"/offline-gain-minus6.wav");}
        std::cout<<"PASS offline pitch, timing, duration, gain, mute, split/merge, stereo: "<<rate<<" Hz "<<ch<<" channels\n";
    }
    pw::AudioTake vibrato;vibrato.rate=48000;vibrato.allocate(144000,1);double phase=0;
    for(int i=0;i<vibrato.frames;++i){double t=i/vibrato.rate;double note=57+0.25*(t-1.5)+0.25*std::sin(2*pw::pi*5*t);phase+=2*pw::pi*pw::frequency(note)/vibrato.rate;vibrato.samples[0][i]=static_cast<float>(0.25*std::sin(phase));}
    pw::NoteView n{0,3,57,57};n.modulation=0;n.drift=0;auto flattened=pw::renderTake(vibrato,{n});
    pw::Detector detector;detector.prepare(48000);std::vector<double> measured;
    for(int i=48000;i<120000;++i)if(detector.push(flattened->samples[0][i])&&detector.value().voiced())measured.push_back(pw::midi(detector.value().hz));
    need(measured.size()>20,"flattened audio measurable");double mean=0;for(double p:measured)mean+=p;mean/=measured.size();double variance=0;for(double p:measured)variance+=(p-mean)*(p-mean);variance/=measured.size();
    std::cout<<"Flattened pitch standard deviation: "<<100*std::sqrt(variance)<<" cents\n";need(std::sqrt(variance)<0.12,"drift/modulation controls flatten actual pitch");
    wav(vibrato,output+"/offline-vibrato-source.wav");wav(*flattened,output+"/offline-flattened.wav");
    bool cancelled=false;try{pw::renderTake(vibrato,{n},[]{return true;});}catch(...){cancelled=true;}need(cancelled,"background cancellation is bounded");
    std::cout<<"PASS offline modulation/drift and render cancellation\n";
}
