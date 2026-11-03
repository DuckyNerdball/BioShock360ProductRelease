// bioshock-recomp: Big Daddy HUD for the player driving him. One instance per window: the second
// window's own ImGui drawer (created with the split view) and the main window's drawer. Each draws only
// while its window shows the Big Daddy's view. Data comes from give.cpp (atomics written on the game
// thread; ImGui runs on the UI thread).
#pragma once

#include <rex/ui/imgui_dialog.h>
#include <imgui.h>
#include <algorithm>
#include <chrono>
#include <cfloat>
#include <cstdio>
#include <atomic>

// Written on the UI thread by the main window's HUD each ImGui frame. The input thread must read this
// instead of calling drawer->GetIO(): GetIO() switches the process-global ImGui context, and with two
// windows (two contexts) that yanked the context out from under window 2 mid-frame -> crash in ImGui::End.
inline std::atomic<bool> g_bs_imgui_wants_mouse{false};

bool BsP2HudData(float* hp, float* mx);  // give.cpp
bool BsCoopSplit();
bool BsSplitInverted();
bool BsP2KeyboardActive();
bool BsPadOnP2();
bool BsBdInMain();  // give.cpp: Back/F6 put the Big Daddy view in the main window

class BigDaddyHud : public rex::ui::ImGuiDialog {
 public:
  BigDaddyHud(rex::ui::ImGuiDrawer* drawer, bool second_window) : rex::ui::ImGuiDialog(drawer), second_(second_window) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (!second_) g_bs_imgui_wants_mouse = io.WantCaptureMouse;
    float hp = 0.0f, mx = 1.0f;
    if (!BsP2HudData(&hp, &mx)) { last_ = -1.0f; return; }
    // Which window shows him: follows every view swap (Back/F6/F7); F5 corrects it once per session.
    const bool coop = BsCoopSplit(), in_main = BsBdInMain();
    const bool shows_bd = second_ ? (coop && !in_main) : ((coop && in_main) || (!coop && BsP2KeyboardActive()));
    if (!shows_bd) return;

    const auto now = std::chrono::steady_clock::now();
    if (last_ >= 0.0f && hp < last_ - 1.0f) flash_until_ = now + std::chrono::milliseconds(450);
    last_ = hp;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    if (W < 50.0f || H < 50.0f) return;
    ImFont* font = ImGui::GetFont();
    const float ts = std::max(14.0f, H * 0.030f);

    // Health bar, bottom-left: brass frame, amber fill (red when low).
    const float bw = W * 0.28f, bh = std::max(8.0f, H * 0.022f), x0 = W * 0.04f, y0 = H * 0.90f;
    const float frac = std::clamp(mx > 0.0f ? hp / mx : 0.0f, 0.0f, 1.0f);
    dl->AddRectFilled(ImVec2(x0 - 4, y0 - 4), ImVec2(x0 + bw + 4, y0 + bh + 4), IM_COL32(25, 18, 10, 210), 5.0f);
    dl->AddRect(ImVec2(x0 - 4, y0 - 4), ImVec2(x0 + bw + 4, y0 + bh + 4), IM_COL32(170, 130, 60, 255), 5.0f, 0, 2.0f);
    const ImU32 fill = frac < 0.3f ? IM_COL32(210, 50, 35, 255) : IM_COL32(230, 165, 40, 255);
    if (frac > 0.0f) dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + bw * frac, y0 + bh), fill, 3.0f);
    char buf[96];
    std::snprintf(buf, sizeof(buf), "BIG DADDY   %d / %d", int(hp + 0.5f), int(mx + 0.5f));
    dl->AddText(font, ts, ImVec2(x0 + 1, y0 - ts - 5), IM_COL32(0, 0, 0, 200), buf);
    dl->AddText(font, ts, ImVec2(x0, y0 - ts - 6), IM_COL32(235, 215, 170, 255), buf);
    const char* who = BsPadOnP2() ? "controller" : "keyboard + mouse";
    std::snprintf(buf, sizeof(buf), "%s  |  Back / F6: swap control  |  views on the wrong windows? F7", who);
    dl->AddText(font, ts * 0.6f, ImVec2(x0, y0 + bh + 8), IM_COL32(200, 185, 150, 200), buf);

    // Hit flash: red border that fades out.
    if (now < flash_until_) {
      const float a = std::chrono::duration<float>(flash_until_ - now).count() / 0.45f;
      const float t = H * 0.025f;
      const ImU32 c = IM_COL32(200, 20, 20, int(150 * a));
      dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, t), c);
      dl->AddRectFilled(ImVec2(0, H - t), ImVec2(W, H), c);
      dl->AddRectFilled(ImVec2(0, 0), ImVec2(t, H), c);
      dl->AddRectFilled(ImVec2(W - t, 0), ImVec2(W, H), c);
    }

    // Down.
    if (hp <= 0.0f) {
      dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(60, 0, 0, 110));
      const char* t1 = "BIG DADDY DOWN";
      const char* t2 = "D-pad down x5 for a new Big Daddy";
      ImVec2 s1 = font->CalcTextSizeA(ts * 2.0f, FLT_MAX, 0.0f, t1), s2 = font->CalcTextSizeA(ts, FLT_MAX, 0.0f, t2);
      dl->AddText(font, ts * 2.0f, ImVec2((W - s1.x) * 0.5f, H * 0.42f), IM_COL32(240, 60, 40, 255), t1);
      dl->AddText(font, ts, ImVec2((W - s2.x) * 0.5f, H * 0.42f + s1.y + 10), IM_COL32(230, 210, 170, 230), t2);
    }
  }

 private:
  bool second_;
  float last_ = -1.0f;
  std::chrono::steady_clock::time_point flash_until_{};
};
