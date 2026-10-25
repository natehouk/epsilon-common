// Compile check: UiKit.hpp inside a DPF NanoVG UI. Built with -fsyntax-only in CI.
#include "DistrhoUI.hpp"
#include "../../common/UiKit.hpp"

START_NAMESPACE_DISTRHO

static const epsilon::HelpPage kPages[] = { { "Overview", "Text." }, { "Controls", "More text." } };

class CheckUI : public UI {
public:
    CheckUI() : UI(600, 400), theme(epsilon::Theme::dark(Color(0x6a, 0xa8, 0xff))), help("CHECK HELP", kPages, 2, "https://example.com") { help.centre(600, 400); }
protected:
    void parameterChanged(uint32_t, float) override {}
    void onNanoDisplay() override {
        epsilon::drawHeaderButton(*this, theme, 10, 10, 60, 22, "UNDO", false, true, false);
        if (tips.shown) epsilon::drawTooltip(*this, theme, tips.x, tips.y, 600, 400, "Gain", "0 dB", "Body.");
        if (help.open) help.draw(*this, theme, 600, 400, tipsOn);
    }
    bool onMouse(const MouseEvent& ev) override { if (ev.press) tips.press(); if (help.open) help.click(ev.pos.getX(), ev.pos.getY(), tipsOn); return true; }
    bool onMotion(const MotionEvent& ev) override { if (tips.hover(1, ev.pos.getX(), ev.pos.getY())) repaint(); return help.motion(ev.pos.getX(), ev.pos.getY()); }
    void uiIdle() override { if (tips.due(tipsOn, false)) repaint(); if (flash.active()) repaint(); }
private:
    epsilon::Theme theme; epsilon::HelpWindow help; epsilon::TipTimer tips; epsilon::Flash flash; bool tipsOn = true;
};

UI* createUI() { return new CheckUI(); }

END_NAMESPACE_DISTRHO
