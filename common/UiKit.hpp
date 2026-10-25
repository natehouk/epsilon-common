// epsilon-common — shared NanoVG UI pieces for DPF plugin UIs: colour theme, header buttons, hover tips,
// the HELP window and status flashes. Each plugin keeps its own look by passing its own Theme.
// Include after DistrhoUI.hpp. License: MIT.
#pragma once
#include "NanoVG.hpp"
#include "Platform.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace epsilon {

using DGL_NAMESPACE::Color;
using DGL_NAMESPACE::NanoVG;

struct Theme {
    Color bg, panel, panelHi, line, text, dim, faint, accent, tipBg, helpBody;
    static Theme dark(Color accent) {
        return { Color(0x10, 0x11, 0x14), Color(0x18, 0x1a, 0x1e), Color(0x22, 0x25, 0x2b), Color(0x2c, 0x30, 0x37),
                 Color(0xe6, 0xe8, 0xec), Color(0x9a, 0xa0, 0xab), Color(0x5c, 0x62, 0x6c), accent,
                 Color(0x1e, 0x20, 0x25), Color(0xc4, 0xc8, 0xd2) };
    }
};

inline Color withAlpha(Color c, float a) { c.alpha = a; return c; }

// a small labelled button in the header bar (UNDO, LEARN, HELP, ...)
inline void drawHeaderButton(NanoVG& vg, const Theme& t, float x, float y, float w, float h, const char* label, bool on, bool hot, bool disabled) {
    vg.beginPath(); vg.roundedRect(x, y, w, h, 5); vg.fillColor(on ? withAlpha(t.accent, 0.2f) : t.panel); vg.fill();
    vg.strokeColor(on ? t.accent : (hot && !disabled ? t.dim : t.line)); vg.strokeWidth(1); vg.stroke();
    vg.fontSize(9); vg.textLetterSpacing(1.6f); vg.textAlign(NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE);
    vg.fillColor(disabled ? t.faint : (on ? t.text : t.dim));
    vg.text(x + w / 2, y + h / 2 + 0.5f, label, nullptr);
}

// hover tip: a titled box near the pointer, kept inside the window
inline void drawTooltip(NanoVG& vg, const Theme& t, float px, float py, float winW, float winH,
                        const std::string& title, const std::string& value, const std::string& body) {
    const float w = 320, pad = 12;
    vg.fontSize(11); vg.textLetterSpacing(0.2f); vg.textLineHeight(1.3f);
    float bb[4] = { 0, 0, 0, 0 }; vg.textBoxBounds(0, 0, w - 2 * pad, body.c_str(), nullptr, bb);
    const float h = pad + 16 + 8 + (bb[3] - bb[1]) + pad;
    float x = px + 16, y = py + 20;
    if (x + w > winW - 8) x = px - w - 12;
    if (y + h > winH - 8) y = py - h - 12;
    x = std::min(std::max(x, 8.f), winW - w - 8); y = std::min(std::max(y, 8.f), winH - h - 8);
    vg.beginPath(); vg.rect(x - 10, y - 4, w + 20, h + 18);
    vg.fillPaint(vg.boxGradient(x, y + 3, w, h, 6, 14, Color(0, 0, 0, 0.55f), Color(0, 0, 0, 0.f))); vg.fill();
    vg.beginPath(); vg.roundedRect(x, y, w, h, 6); vg.fillColor(t.tipBg); vg.fill(); vg.strokeColor(t.line); vg.strokeWidth(1); vg.stroke();
    std::string up = title; for (char& ch : up) ch = (char)std::toupper((unsigned char)ch);
    vg.fontSize(10); vg.textLetterSpacing(1.6f); vg.textAlign(NanoVG::ALIGN_LEFT | NanoVG::ALIGN_TOP); vg.fillColor(t.text);
    vg.text(x + pad, y + pad + 1, up.c_str(), nullptr);
    if (!value.empty()) {
        vg.fontSize(10.5f); vg.textLetterSpacing(0.4f); vg.textAlign(NanoVG::ALIGN_RIGHT | NanoVG::ALIGN_TOP); vg.fillColor(t.accent);
        vg.text(x + w - pad, y + pad + 1, value.c_str(), nullptr);
    }
    vg.fontSize(11); vg.textLetterSpacing(0.2f); vg.textLineHeight(1.3f); vg.textAlign(NanoVG::ALIGN_LEFT | NanoVG::ALIGN_TOP); vg.fillColor(t.dim);
    vg.textBox(x + pad, y + pad + 24, w - 2 * pad, body.c_str(), nullptr);
    vg.textLineHeight(1.f);
}

// hover-tip timing: call hover() from onMotion with the key under the pointer (-1 for none),
// press() on mouse down, and due() from uiIdle; show the tip when due() returns true.
struct TipTimer {
    int key = -1, suppressed = -2; double since = 0; bool shown = false;
    float x = 0, y = 0;
    bool hover(int k, float px, float py) {               // true if a shown tip must be hidden (repaint)
        x = px; y = py;
        if (k == key) return false;
        const bool was = shown; key = k; since = nowSec(); shown = false;
        if (k != suppressed) suppressed = -2;
        return was;
    }
    void press() { shown = false; suppressed = key; }
    bool due(bool enabled, bool busy, double delay = 0.6) {   // true when the tip should appear now (repaint)
        if (!enabled || shown || key < 0 || key == suppressed || busy || nowSec() - since <= delay) return false;
        shown = true; return true;
    }
};

// a short message in the status line, shown for a fixed time
struct Flash {
    std::string text; double until = 0;
    void show(const std::string& s, double secs = 1.2) { text = s; until = nowSec() + secs; }
    void clear() { until = 0; }
    bool active() const { return nowSec() < until; }
};

struct HelpPage { const char* title; const char* body; };

// the HELP window: page tabs on the left, the page on the right, a HOVER TIPS switch, FULL MANUAL (WEB) and CLOSE
class HelpWindow {
public:
    enum { kTipsSwitch = 100, kManual = 101, kClose = 102, kOutside = -2, kNone = -1 };
    float x = 150, y = 90, w = 760, h = 560, tabW = 180, tabH = 34;
    int page = 0, hover = kNone; bool open = false;

    HelpWindow(const char* inTitle, const HelpPage* inPages, int inCount, const char* inManualURL)
        : title(inTitle), pages(inPages), count(inCount), url(inManualURL) {}

    void centre(float winW, float winH) { x = std::round((winW - w) / 2); y = std::round((winH - h) / 2); }

    int hit(float px, float py) const {
        if (px < x || px > x + w || py < y || py > y + h) return kOutside;
        for (int i = 0; i < count; ++i) if (px >= x + 12 && px < x + 12 + tabW && py >= y + 56 + i * tabH && py < y + 56 + (i + 1) * tabH) return i;
        const float by = y + h - 44;
        if (py >= by && py < by + 28) {
            if (px >= x + 12 && px < x + 12 + tabW) return kTipsSwitch;
            if (px >= x + w - 330 && px < x + w - 140) return kManual;
            if (px >= x + w - 124 && px < x + w - 16) return kClose;
        }
        if (px >= x + w - 40 && py < y + 40) return kClose;
        return kNone;
    }
    bool motion(float px, float py) { const int hh = hit(px, py); if (hh == hover) return false; hover = hh; return true; }
    // tipsOn is the plugin's hover-tips switch
    void click(float px, float py, bool& tipsOn) {
        const int hh = hit(px, py);
        if (hh >= 0 && hh < count) page = hh;
        else if (hh == kTipsSwitch) tipsOn = !tipsOn;
        else if (hh == kManual) openURL(url);
        else if (hh == kClose || hh == kOutside) open = false;
    }

    void draw(NanoVG& vg, const Theme& t, float winW, float winH, bool tipsOn) const {
        vg.beginPath(); vg.rect(0, 0, winW, winH); vg.fillColor(Color(0, 0, 0, 0.55f)); vg.fill();
        vg.beginPath(); vg.roundedRect(x, y, w, h, 10); vg.fillColor(t.panel); vg.fill(); vg.strokeColor(t.line); vg.strokeWidth(1); vg.stroke();
        vg.fontSize(13); vg.textLetterSpacing(4.f); vg.textAlign(NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE); vg.fillColor(t.text);
        vg.text(x + 22, y + 28, title, nullptr);
        const float cx = x + w - 24, cy = y + 26;
        vg.beginPath(); vg.moveTo(cx - 6, cy - 6); vg.lineTo(cx + 6, cy + 6); vg.moveTo(cx + 6, cy - 6); vg.lineTo(cx - 6, cy + 6);
        vg.strokeColor(hover == kClose ? t.text : t.dim); vg.strokeWidth(1.6f); vg.stroke();
        for (int i = 0; i < count; ++i) {
            const float ty = y + 56 + i * tabH;
            if (i == page || i == hover) { vg.beginPath(); vg.roundedRect(x + 12, ty + 2, tabW, tabH - 4, 5); vg.fillColor(i == page ? withAlpha(t.accent, 0.16f) : t.panelHi); vg.fill(); }
            if (i == page) { vg.beginPath(); vg.roundedRect(x + 12, ty + 8, 3, tabH - 16, 1.5f); vg.fillColor(t.accent); vg.fill(); }
            vg.fontSize(11.5f); vg.textLetterSpacing(0.6f); vg.textAlign(NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE); vg.fillColor(i == page ? t.text : t.dim);
            vg.text(x + 28, ty + tabH / 2 + 0.5f, pages[i].title, nullptr);
        }
        vg.beginPath(); vg.moveTo(x + tabW + 26, y + 56); vg.lineTo(x + tabW + 26, y + h - 56); vg.strokeColor(t.line); vg.strokeWidth(1); vg.stroke();
        const float bx = x + tabW + 46, bw = w - tabW - 70;
        vg.fontSize(15); vg.textLetterSpacing(0.8f); vg.textAlign(NanoVG::ALIGN_LEFT | NanoVG::ALIGN_TOP); vg.fillColor(t.accent);
        vg.text(bx, y + 60, pages[page].title, nullptr);
        vg.fontSize(12); vg.textLetterSpacing(0.2f); vg.textLineHeight(1.38f); vg.fillColor(t.helpBody);
        vg.textBox(bx, y + 92, bw, pages[page].body, nullptr);
        vg.textLineHeight(1.f);
        const float by = y + h - 44;
        vg.beginPath(); vg.moveTo(x + 12, by - 10); vg.lineTo(x + w - 12, by - 10); vg.strokeColor(t.line); vg.stroke();
        const float sw = 34, sh = 18, sx = x + 16, sy = by + 14 - sh / 2;
        vg.beginPath(); vg.roundedRect(sx, sy, sw, sh, sh / 2); vg.fillColor(tipsOn ? withAlpha(t.accent, 0.25f) : t.panelHi); vg.fill();
        vg.strokeColor(tipsOn ? t.accent : t.line); vg.stroke();
        vg.beginPath(); vg.circle(tipsOn ? sx + sw - sh / 2 : sx + sh / 2, by + 14, 6); vg.fillColor(tipsOn ? t.accent : t.dim); vg.fill();
        vg.fontSize(10); vg.textLetterSpacing(1.6f); vg.textAlign(NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE); vg.fillColor(hover == kTipsSwitch ? t.text : t.dim);
        vg.text(sx + sw + 10, by + 14.5f, "HOVER TIPS", nullptr);
        button(vg, t, x + w - 330, by, 190, "FULL MANUAL (WEB)", hover == kManual, false);
        button(vg, t, x + w - 124, by, 108, "CLOSE", hover == kClose, true);
    }

private:
    static void button(NanoVG& vg, const Theme& t, float x0, float y0, float w0, const char* label, bool hot, bool primary) {
        vg.beginPath(); vg.roundedRect(x0, y0, w0, 28, 5);
        vg.fillColor(primary ? withAlpha(t.accent, hot ? 0.32f : 0.16f) : (hot ? t.panelHi : t.panel)); vg.fill();
        vg.strokeColor(primary ? t.accent : t.line); vg.strokeWidth(1); vg.stroke();
        vg.fontSize(9.5f); vg.textLetterSpacing(1.8f); vg.textAlign(NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE); vg.fillColor(t.text);
        vg.text(x0 + w0 / 2, y0 + 14.5f, label, nullptr);
    }
    const char* title; const HelpPage* pages; int count; const char* url;
};

} // namespace epsilon
