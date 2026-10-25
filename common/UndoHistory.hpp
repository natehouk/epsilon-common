// epsilon-common — undo/redo and A/B for a plugin's settings. Framework-free: the UI hands in its current
// values and applies the snapshots it gets back (each one ends with the A/B slot it belongs to).
//
//   history.init(kParamCount, [](uint32_t p) { return !isOutput(p) && !isViewSetting(p); });
//   on mouse down:  history.begin(val);            on mouse up:  history.end(val);
//   from the host:  history.hostChanged(p, v);     (keeps the "before" state current between gestures)
//   undo:           if (history.undo(val, snap)) applySnapshot(snap);
//   A/B:            if (history.switchTo(1, val, snap)) applySnapshot(snap);
//
// License: MIT.
#pragma once
#include <cstdint>
#include <functional>
#include <cstddef>
#include <vector>

namespace epsilon {

class UndoHistory {
public:
    typedef std::vector<float> Snap;          // count values, then the A/B slot
    static constexpr size_t kMaxSteps = 200;

    void init(uint32_t count, std::function<bool(uint32_t)> inSetting) {
        n = count; isSetting = inSetting; before.assign(n, 0.f); undoStack.clear(); redoStack.clear(); ab[0].clear(); ab[1].clear(); slot_ = 0; gesture = false;
    }
    // the values the next edit starts from (call once after the UI's values are known)
    void reset(const float* cur) { before.assign(cur, cur + n); }

    // an edit is everything between begin and end: a drag, a click, a preset load
    void begin(const float* cur) { gesture = true; before.assign(cur, cur + n); }
    bool end(const float* cur) {          // true if it became an undo step
        gesture = false;
        const bool changed = differs(before.data(), cur);
        if (changed) { push(undoStack, snap(before.data(), slot_)); redoStack.clear(); }
        before.assign(cur, cur + n);
        return changed;
    }
    void hostChanged(uint32_t p, float v) { if (!gesture && p < n) before[p] = v; }
    bool inGesture() const { return gesture; }

    bool canUndo() const { return !undoStack.empty(); }
    bool canRedo() const { return !redoStack.empty(); }
    size_t undoSteps() const { return undoStack.size(); }
    size_t redoSteps() const { return redoStack.size(); }

    // out = the setting to apply. An undo across an A/B switch also moves back to that slot.
    bool undo(const float* cur, Snap& out) { return step(undoStack, redoStack, cur, out); }
    bool redo(const float* cur, Snap& out) { return step(redoStack, undoStack, cur, out); }

    // A/B: the first time, B starts as a copy of A. The switch itself is an undo step.
    int slot() const { return slot_; }
    bool switchTo(int to, const float* cur, Snap& out) {
        if (to == slot_ || to < 0 || to > 1) return false;
        ab[slot_] = snap(cur, slot_);
        if (ab[to].empty()) ab[to] = snap(cur, to);
        push(undoStack, snap(cur, slot_)); redoStack.clear();
        out = ab[to]; slot_ = to;
        before.assign(out.begin(), out.begin() + n);
        return true;
    }
    void copyToOther(const float* cur) { ab[1 - slot_] = snap(cur, 1 - slot_); }

private:
    Snap snap(const float* v, int s) const { Snap r(v, v + n); r.push_back((float)s); return r; }
    bool differs(const float* a, const float* b) const { for (uint32_t i = 0; i < n; ++i) if (isSetting(i) && a[i] != b[i]) return true; return false; }
    static void push(std::vector<Snap>& st, const Snap& s) { st.push_back(s); if (st.size() > kMaxSteps) st.erase(st.begin()); }
    bool step(std::vector<Snap>& from, std::vector<Snap>& to, const float* cur, Snap& out) {
        if (from.empty()) return false;
        push(to, snap(cur, slot_));
        out = from.back(); from.pop_back();
        const int s = (int)out[n];
        if (s != slot_) { ab[slot_] = snap(cur, slot_); slot_ = s; }
        before.assign(out.begin(), out.begin() + n);
        return true;
    }

    uint32_t n = 0;
    std::function<bool(uint32_t)> isSetting;
    Snap before; bool gesture = false;
    std::vector<Snap> undoStack, redoStack;
    Snap ab[2]; int slot_ = 0;
};

} // namespace epsilon
