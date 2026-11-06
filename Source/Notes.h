#pragma once
#include "Core.h"
#include <atomic>

namespace pw {
struct NoteView { double start = 0, end = 0, original = 60, target = 60; };
struct AtomicNote {
    std::atomic<double> start{0}, end{0}, original{60}, target{60};
    NoteView view() const { return {start.load(), end.load(), original.load(), target.load()}; }
    void set(NoteView n) { start = n.start; end = n.end; original = n.original; target = n.target; }
};
static_assert(std::atomic<double>::is_always_lock_free, "Audio parameters must be lock-free");
class Notes {
public:
    static constexpr int capacity = 4096;
    std::array<AtomicNote, capacity> data;
    std::atomic<int> count{0};
    std::atomic<uint64_t> revision{0};
    void clear() { count.store(0); active = -1; ++revision; }
    // Called from the processing thread, with recording already armed.
    void capture(double time, Pitch pitch, double frameSeconds) {
        if (!pitch.voiced() || time < 0 || !std::isfinite(time)) { active = -1; return; }
        double n = midi(pitch.hz);
        if (active >= 0 && active < count.load()) {
            auto previous = data[active].view();
            if (time >= previous.start && time <= previous.end + 0.045 &&
                std::abs(n - previous.original) < 0.8) {
                // Keep the measured centre stable, but permit modest pitch drift.
                double original = previous.original * 0.95 + n * 0.05;
                data[active].original = original;
                data[active].target = original;
                data[active].end = time + frameSeconds * 0.5;
                return;
            }
        }
        int index = count.load();
        if (index >= capacity) { active = -1; return; }
        data[index].set({std::max(0.0, time - frameSeconds * 0.5),
                         time + frameSeconds * 0.5, n, n});
        active = index; count.store(index + 1);
    }
    void stopSegment() { active = -1; }
    // A bounded linear search supports seeking and loops without mutable cursors.
    // Newer captures win if two notes overlap.
    bool targetAt(double seconds, double& target) const {
        for (int i = count.load() - 1; i >= 0; --i) {
            auto n = data[i].view();
            if (seconds >= n.start && seconds < n.end) { target = n.target; return true; }
        }
        return false;
    }
    bool shiftAt(double seconds, double& shift) const {
        for (int i = count.load() - 1; i >= 0; --i) {
            auto n = data[i].view();
            if (seconds >= n.start && seconds < n.end) { shift = n.target - n.original; return true; }
        }
        return false;
    }
private:
    int active = -1;
};
}
