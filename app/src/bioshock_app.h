// bioshock - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/rex_app.h>
#include <rex/runtime.h>
#include <rex/system/xmemory.h>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <rex/ui/keybinds.h>
#include <rex/input/input_system.h>
#include "testing_overlay.h"
#include "bd_hud.h"
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/immediate_drawer.h>
#include <functional>
#include <rex/graphics/split_view.h>
#include <rex/logging.h>
#include <rex/ui/graphics_provider.h>
#include <rex/ui/presenter.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>

extern std::function<void(bool)> g_split_window_fn;  // give.cpp
extern std::function<void(bool)> g_focus_window_fn;  // give.cpp
bool BsP2KbmDrives();                                 // give.cpp: keyboard+mouse drive the Big Daddy (no dedicated pad)
extern std::function<void(bool)> g_fullscreen2_fn;    // give.cpp
void BsToggleP2Fullscreen();                          // give.cpp
void BsInstallCrashTrace();                           // crash_trace.cpp
void BsHudFlip();                                     // give.cpp

class BioshockApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<BioshockApp>(new BioshockApp(ctx, "bioshock",
        PPCImageConfig));
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnPreSetup(rex::RuntimeConfig& config) override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // bioshock-recomp: dump the loaded (decrypted, decompressed) module image once to
  // logs/bioshock_image.bin for offline string/xref analysis. Local only: logs/ is gitignored.
  void OnPostLoadXexImage() override {
    auto out = std::filesystem::current_path() / "logs" / "bioshock_image.bin";
    if (std::filesystem::exists(out)) return;
    auto* mem = runtime()->memory();
    const uint8_t* p = mem->TranslateVirtual(PPCImageConfig.image_base);
    if (FILE* f = std::fopen(out.string().c_str(), "wb")) {
      std::fwrite(p, 1, PPCImageConfig.image_size, f);
      std::fclose(f);
    }
  }
  // void OnPostSetup() override {}
  // bioshock-recomp: F9 toggles the Testing overlay.
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    // Always alive (it also polls the controller for the L3+R3 toggle); F9 only shows/hides it.
    testing_ = std::make_unique<TestingDialog>(drawer);
    hud_main_ = std::make_unique<BigDaddyHud>(drawer, false);
    rex::ui::RegisterBind("bind_testing", "F9", "Toggle BioShock Testing overlay", [] { TestingDialog::Toggle(); });
    rex::ui::RegisterBind("bind_p2", "F8", "Keyboard+mouse: control P1 / P2", [] { BsP2ToggleKeyboard(); });
    g_split_window_fn = [this](bool on) { SetSplitWindow(on); };
    g_fullscreen2_fn = [this](bool on) {
      app_context().CallInUIThread([this, on] { if (window2_) window2_->SetFullscreen(on); });
    };
    rex::ui::RegisterBind("bind_p2_fullscreen", "F11", "Co-op: Big Daddy window fullscreen (for streaming to a friend)", [] { BsToggleP2Fullscreen(); });
    g_focus_window_fn = [this](bool p2) {
      app_context().CallInUIThread([this, p2] {
        if (p2 && window2_) window2_->Focus();
        else if (!p2 && window()) window()->Focus();
      });
    };
    // F5 (HUD flip) and F7 (view swap) are polled in give.cpp (BsPollCoopKeys): binds only fire in the main window,
    // and with the Big Daddy window focused they did nothing (Hayden, 2026-11-06).
    rex::ui::RegisterBind("bind_pad_swap", "F6", "Co-op: controller drives Jack / the Big Daddy (focuses his window)", [] { BsPadSwap(); });
  }
  // Split view: a second window with its own presenter. Frames the game tags as view 1 are routed
  // to it by IssueSwap (SDK patch, split_view.h). Created once and kept alive; closing it only
  // turns routing off, so the GPU thread never sees a dead presenter.
  struct SplitCloseListener : rex::ui::WindowListener {
    bool OnCloseRequested(rex::ui::UIEvent&) override {
      rex::graphics::split_view::state().enabled = false;
      BsSetViewMode(0);
      REXLOG_INFO("bioshock-recomp: split: second window close -> routing off (window stays until exit)");
      return false;
    }
  } split_close_;
  // Declaration order = reverse destruction order: the HUD and its ImGui drawer go first (they hang off
  // the window and presenter), then the window (detaches from the presenter), then drawer and presenter.
  std::unique_ptr<rex::ui::Presenter> presenter2_;
  std::unique_ptr<rex::ui::ImmediateDrawer> drawer2_;
  std::unique_ptr<rex::ui::Window> window2_;
  std::unique_ptr<rex::ui::ImGuiDrawer> imgui2_;
  std::unique_ptr<BigDaddyHud> hud2_;
  void OnShutdown() override {
    rex::graphics::split_view::state().enabled = false;
    rex::graphics::split_view::state().second_presenter = nullptr;
  }
  void SetSplitWindow(bool on) {
    app_context().CallInUIThread([this, on] {
      namespace sv = rex::graphics::split_view;
      if (!on) { sv::state().enabled = false; REXLOG_INFO("bioshock-recomp: split: off"); return; }
      if (!window2_) {
        auto* gs = runtime() ? runtime()->graphics_system() : nullptr;
        auto* prov = gs ? gs->provider() : nullptr;
        if (!prov) { REXLOG_ERROR("bioshock-recomp: split: no graphics provider"); return; }
        presenter2_ = prov->CreatePresenter();
        if (!presenter2_) { REXLOG_ERROR("bioshock-recomp: split: CreatePresenter failed"); return; }
        window2_ = rex::ui::Window::Create(app_context(), "BioShock - Player 2 (Big Daddy)", 1280, 720);
        if (!window2_) { REXLOG_ERROR("bioshock-recomp: split: window create failed"); return; }
        window2_->AddListener(&split_close_);
        if (!window2_->Open()) { REXLOG_ERROR("bioshock-recomp: split: window open failed"); return; }
        window2_->SetPresenter(presenter2_.get());
        sv::state().second_presenter = presenter2_.get();
        // Big Daddy HUD in window 2: its own immediate drawer + ImGui context on the second presenter.
        drawer2_ = prov->CreateImmediateDrawer();
        if (drawer2_) {
          drawer2_->SetPresenter(presenter2_.get());
          imgui2_ = std::make_unique<rex::ui::ImGuiDrawer>(window2_.get(), 64);
          imgui2_->SetPresenterAndImmediateDrawer(presenter2_.get(), drawer2_.get());
          hud2_ = std::make_unique<BigDaddyHud>(imgui2_.get(), true);
          REXLOG_INFO("bioshock-recomp: split: Big Daddy HUD on the second window");
        }
        REXLOG_INFO("bioshock-recomp: split: second window open");
      }
      sv::state().enabled = true;
      REXLOG_INFO("bioshock-recomp: split: on");
    });
  }
  // While the F9 panel is open, the game gets no pad/keyboard input (the panel uses it).
  // Replaces the SDK's callback set in ConstructRuntime (same mouse rule otherwise).
  void OnPreLaunchModule() override {
    BsInstallCrashTrace();
    auto* in = static_cast<rex::input::InputSystem*>(runtime()->input_system());
    auto* d = imgui_drawer();
    // Co-op (two windows + F8): controllers stay with Jack, keyboard/mouse go to the Big Daddy.
    if (in && d) in->SetActiveCallbacks(
        [d] { return !TestingDialog::Visible() && !(BsP2KeyboardActive() && !BsCoopSplit()) && !BsPadOnP2() && !g_bs_imgui_wants_mouse.load(); },
        [d] { return !TestingDialog::Visible() && !BsP2KbmDrives() && !g_bs_imgui_wants_mouse.load(); });
  }
  std::unique_ptr<TestingDialog> testing_;
  std::unique_ptr<BigDaddyHud> hud_main_;
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnConfigurePaths(rex::PathConfig& paths) override {}
};
