#pragma once
#include "Core.h"
#include <atomic>
#include <vector>

namespace pw {
struct NoteView {
    double start = 0, end = 0, original = 60, target = 60;
    double sourceStart = -1, sourceEnd = -1, gainDb = 0, formant = 0;
    double modulation = 1, drift = 1, fadeIn = 0.005, fadeOut = 0.005;
    bool muted = false;
    double sourceBegin() const { return sourceStart < 0 ? start : sourceStart; }
    double sourceFinish() const { return sourceEnd < 0 ? end : sourceEnd; }
};
struct AtomicNote {
    std::atomic<double> start{0}, end{0}, original{60}, target{60};
    std::atomic<double> sourceStart{-1}, sourceEnd{-1}, gainDb{0}, formant{0};
    std::atomic<double> modulation{1}, drift{1}, fadeIn{0.005}, fadeOut{0.005};
    std::atomic<bool> muted{false};
    NoteView view() const { return {start.load(), end.load(), original.load(), target.load(),
        sourceStart.load(), sourceEnd.load(), gainDb.load(), formant.load(), modulation.load(),
        drift.load(), fadeIn.load(), fadeOut.load(), muted.load()}; }
    void set(NoteView n) { start=n.start; end=n.end; original=n.original; target=n.target;
        sourceStart=n.sourceStart; sourceEnd=n.sourceEnd; gainDb=n.gainDb; formant=n.formant;
        modulation=n.modulation; drift=n.drift; fadeIn=n.fadeIn; fadeOut=n.fadeOut; muted=n.muted; }
};
static_assert(std::atomic<double>::is_always_lock_free, "Audio parameters must be lock-free");
class Notes {
public:
    static constexpr int capacity = 4096;
    std::array<AtomicNote, capacity> data;
    std::atomic<int> count{0};
    std::atomic<uint64_t> revision{0};
    void clear() { count.store(0); active=-1; ++revision; }
    std::vector<NoteView> snapshot() const {
        std::vector<NoteView> result; int n=count.load(); result.reserve(n);
        for(int i=0;i<n;++i) result.push_back(data[i].view()); return result;
    }
    void replace(const std::vector<NoteView>& items) {
        count=0; active=-1;
        for(size_t i=0;i<std::min(items.size(),static_cast<size_t>(capacity));++i) data[i].set(items[i]);
        count=static_cast<int>(std::min(items.size(),static_cast<size_t>(capacity))); ++revision;
    }
    void capture(double time, Pitch pitch, double frameSeconds) {
        if(!pitch.voiced() || time<0 || !std::isfinite(time)) { active=-1; return; }
        double n=midi(pitch.hz);
        if(active>=0 && active<count.load()) {
            auto previous=data[active].view();
            if(time>=previous.start && time<=previous.end+0.045 && std::abs(n-previous.original)<0.8) {
                double centre=previous.original*0.95+n*0.05;
                data[active].original=centre; data[active].target=centre;
                data[active].end=time+frameSeconds*0.5; return;
            }
        }
        int index=count.load(); if(index>=capacity) { active=-1; return; }
        data[index].set({std::max(0.0,time-frameSeconds*0.5),time+frameSeconds*0.5,n,n}); active=index; count=index+1;
    }
    void stopSegment() { active=-1; }
    bool targetAt(double seconds,double& target) const {
        for(int i=count.load()-1;i>=0;--i) { auto n=data[i].view(); if(seconds>=n.start&&seconds<n.end) { target=n.target; return true; } } return false;
    }
    bool shiftAt(double seconds,double& shift) const {
        for(int i=count.load()-1;i>=0;--i) { auto n=data[i].view(); if(seconds>=n.start&&seconds<n.end) { shift=n.target-n.original; return true; } } return false;
    }
private: int active=-1;
};
inline bool splitNote(std::vector<NoteView>& notes,int index,double at) {
    if(index<0||index>=static_cast<int>(notes.size())||notes.size()>=Notes::capacity) return false;
    auto a=notes[index]; if(at<=a.start+0.025||at>=a.end-0.025) return false;
    double sourceAt=a.sourceBegin()+(at-a.start)/(a.end-a.start)*(a.sourceFinish()-a.sourceBegin());
    auto b=a; b.start=at; b.sourceStart=sourceAt; b.sourceEnd=a.sourceFinish();
    a.sourceStart=a.sourceBegin(); a.sourceEnd=sourceAt; a.end=at;
    notes[index]=a; notes.insert(notes.begin()+index+1,b); return true;
}
inline bool mergeNote(std::vector<NoteView>& notes,int index) {
    if(index<0||index+1>=static_cast<int>(notes.size())) return false;
    auto a=notes[index],b=notes[index+1];
    if(std::abs(a.sourceFinish()-b.sourceBegin())>0.06||std::abs(a.end-b.start)>0.06) return false;
    double duration=a.sourceFinish()-a.sourceBegin(),other=b.sourceFinish()-b.sourceBegin();
    a.sourceStart=a.sourceBegin(); a.sourceEnd=b.sourceFinish(); a.end=b.end;
    a.original=(a.original*duration+b.original*other)/(duration+other);
    notes[index]=a; notes.erase(notes.begin()+index+1); return true;
}
}
