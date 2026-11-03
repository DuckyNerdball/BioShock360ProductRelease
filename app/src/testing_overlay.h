// bioshock-recomp: F9 "Testing" overlay. Buttons queue Unreal console commands that run on the
// game thread (see cheats.cpp). Command names other than god/summon are best guesses until the
// log shows which ones the 360 build handles; the Custom box lets you try others.
#pragma once

#include <rex/ui/imgui_dialog.h>
#include <imgui.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>

#include <string>
#include <vector>

void BsQueueCommand(std::string cmd);
std::vector<std::string> BsRecentResults();
bool BsEngineReady();
#include <deque>
void BsQueueGive(std::string cls);
void BsSetDoubleEncounters(bool on);  // give.cpp
void BsP2Request(bool detach, std::string view);
bool BsP2KeyboardActive();
void BsP2ToggleKeyboard();
void BsSetViewMode(int m);
void BsSplitWindow(bool on);   // give.cpp: second window, alternate frames
void BsSplitInvert(bool inv);
bool BsCoopSplit();
bool BsSplitInverted();
bool BsPadOnP2();               // co-op: controller currently drives the Big Daddy
void BsPadSwap();
void BsHudFlip();               // Big Daddy HUD to the other window (F5)               // co-op: move the controller to the other player (Back / F6)
void BsSplitDefault(bool on);
void BsJackBody(bool on);
void BsP2Blind(bool on);
void BsP2Squeeze(bool on);
void BsP2AsPlayer(bool on);
void BsP2Triggers(bool on);
void BsP2FreezeThink(bool on);
void BsP2FreezeAI(bool ctl, bool pawn);
void BsSetTrace(bool on);
void BsSetProbe(bool on);
int BsGetViewMode();             // two windows on: controller = Jack, keyboard/mouse = P2
std::string BsSplitStats();
void BsSetAttackRecorder(bool on);
void BsP2RealWeapons(bool on);
void BsP2Ally(bool on);
void BsP2Silence(bool on);
void BsSetVerbose(bool v);
void BsPanelOpen(bool open);
void BsFreezeWorld(bool on);
void BsClearGiveLog();
void BsP2Release();
uint32_t BsP2Pawn();
std::deque<std::string> BsGiveLog();
void BsScanNames(std::string needle);
std::vector<std::string> BsNameHits();
bool BsPadPoll(uint16_t* buttons, int16_t* lx, int16_t* ly);  // cheats.cpp (XInput)

// Created once at startup and kept alive; F9 or L3+R3 (click both sticks) shows/hides it.
// While it is shown the game gets no controller/keyboard input (see bioshock_app.h), and the
// panel is driven with the keyboard (arrows, Enter/Space, Esc, Tab) or the controller
// (D-pad/left stick move, A press, B back, X/Y edit, LB/RB switch window).
class TestingDialog : public rex::ui::ImGuiDialog {
 public:
  explicit TestingDialog(rex::ui::ImGuiDrawer* drawer) : rex::ui::ImGuiDialog(drawer) {}
  static bool Visible() { return visible_.load(); }
  static void Toggle() {
    visible_ = !visible_.load(); just_opened_ = visible_.load();
    BsPanelOpen(visible_.load());  // freezes the world while the panel is open (give.cpp)
  }

 protected:
  // Feed controller state to ImGui navigation; detect the L3+R3 toggle.
  void PollPad(ImGuiIO& io) {
    uint16_t b = 0; int16_t lx = 0, ly = 0;
    bool have = BsPadPoll(&b, &lx, &ly);
    if (!have) b = 0, lx = ly = 0;
    constexpr uint16_t kL3 = 0x0040, kR3 = 0x0080;
    bool combo = (b & kL3) && (b & kR3);
    if (combo && !combo_prev_) Toggle();
    combo_prev_ = combo;
    if (!visible_) { b = 0; lx = ly = 0; }
    if (have) io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    struct Map { uint16_t bit; ImGuiKey key; };
    static const Map kMap[] = {
        {0x0001, ImGuiKey_GamepadDpadUp}, {0x0002, ImGuiKey_GamepadDpadDown},
        {0x0004, ImGuiKey_GamepadDpadLeft}, {0x0008, ImGuiKey_GamepadDpadRight},
        {0x0010, ImGuiKey_GamepadStart}, {0x0020, ImGuiKey_GamepadBack},
        {0x0100, ImGuiKey_GamepadL1}, {0x0200, ImGuiKey_GamepadR1},
        {0x1000, ImGuiKey_GamepadFaceDown}, {0x2000, ImGuiKey_GamepadFaceRight},
        {0x4000, ImGuiKey_GamepadFaceLeft}, {0x8000, ImGuiKey_GamepadFaceUp}};
    for (const auto& m : kMap) {
      bool down = (b & m.bit) != 0;
      if (down != ((pad_prev_ & m.bit) != 0)) io.AddKeyEvent(m.key, down);
    }
    pad_prev_ = b;
    auto axis = [&](ImGuiKey k, float v) {
      float a = v > 0.3f ? std::min(1.0f, (v - 0.3f) / 0.7f) : 0.0f;
      io.AddKeyAnalogEvent(k, a > 0.0f, a);
    };
    float fx = lx / 32767.0f, fy = ly / 32767.0f;
    axis(ImGuiKey_GamepadLStickRight, fx); axis(ImGuiKey_GamepadLStickLeft, -fx);
    axis(ImGuiKey_GamepadLStickUp, fy);    axis(ImGuiKey_GamepadLStickDown, -fy);
  }

  void OnDraw(ImGuiIO& io) override {
    PollPad(io);
    static bool s_cleaned = false;  // undo a rivet tracer saved by an older build
    if (!s_cleaned && BsEngineReady()) { s_cleaned = true; BsQueueCommand("set Pistol TracerEffects ()"); }
    if (!visible_) {
      io.ConfigFlags &= ~(ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad);
      return;
    }
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    if (just_opened_) { ImGui::SetNextWindowFocus(); just_opened_ = false; }
    ImGui::SetNextWindowSize(ImVec2(620, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Testing (F9)", nullptr, ImGuiWindowFlags_NoCollapse)) {
      ImGui::End();
      return;
    }
    // Text size: A- / A+ (remembered for the session).
    ImGui::SetWindowFontScale(font_scale_);
    if (ImGui::SmallButton("A-")) font_scale_ = std::max(0.8f, font_scale_ - 0.1f);
    ImGui::SameLine();
    if (ImGui::SmallButton("A+")) font_scale_ = std::min(2.5f, font_scale_ + 0.1f);
    ImGui::SameLine();
    ImGui::TextDisabled("text size %.0f%%", font_scale_ * 100.0f);
    if (!BsEngineReady()) {
      ImGui::TextColored(ImVec4(1, 0.6f, 0.2f, 1),
                         "Engine not captured yet - open/close the pause menu once.");
    }

    ImGui::SeparatorText("Player");
    if (ImGui::Checkbox("God mode (invulnerable)", &god_)) BsQueueCommand("god");

    ImGui::SameLine();
    if (ImGui::Checkbox("Noclip (fly through walls)", &noclip_)) BsQueueCommand(noclip_ ? "ghost" : "walk");

    ImGui::SameLine();
    if (ImGui::Button("Teleport to crosshair")) BsQueueCommand("teleport");

    ImGui::SeparatorText("Travel to level (use a spare save - may skip story state)");
    static const char* kMaps[] = {"0-Lighthouse", "1-Welcome",  "1-Medical",   "2-Fisheries",
                                  "2-SubBay",     "3-Arcadia",  "3-Market",    "4-Recreation",
                                  "5-Hephaestus", "5-Ryan",     "6-Resi",      "6-Slums",
                                  "7-Science",    "7-Gauntlet", "7-BossFight"};
    ImGui::SetNextItemWidth(260);
    ImGui::Combo("##map", &map_idx_, kMaps, IM_ARRAYSIZE(kMaps));
    ImGui::SameLine();
    if (ImGui::Button("Go")) BsQueueCommand(std::string("open ") + kMaps[map_idx_]);

    ImGui::SeparatorText("Give");
    if (ImGui::Button("All weapons (direct give)")) {
      static const char* kW[] = {"WrenchPickupItem", "PistolPickupItem", "MachineGunPickupItem",
                                 "ShotgunPickupItem", "GrenadeLauncherPickupItem",
                                 "ChemicalThrowerPickupItem", "CrossbowPickupItem",
                                 "ResearchCameraPickupItem"};
      for (const char* w : kW) BsQueueGive(std::string("ShockDesignerClasses.") + w);
    }
    ImGui::SameLine();
    if (ImGui::Button("All ammo")) {
      // Every ammo class the 360 build accepts (found by the memory scan, 2026-11-01).
      static const char* kA[] = {
          "Pistol_Bullet", "Pistol_AntiPersonnel", "Pistol_ArmorPiercing",
          "MachineGun_Bullet", "MachineGun_ArmorPiercingBullet", "MachineGun_FrozenBullet",
          "MachineGun_RubberBullet", "Shotgun_00Buck", "Shotgun_IonicBuck",
          "Shotgun_HighExplosiveBuck", "ChemicalThrower_Kerosene", "ChemicalThrower_LiquidNitrogen",
          "ChemicalThrower_IonicGel", "ChemicalThrower_Accelerant", "Crossbow_Bolt",
          "Crossbow_ArmorPiercingBolt", "Crossbow_DiseaseBolt", "Crossbow_SuperHeatedBolt",
          "Crossbow_TrapBolt", "GrenadeLauncher_FragGrenade", "GrenadeLauncher_LiquidNitrogen",
          "GrenadeLauncher_RPG", "GrenadeLauncher_StickyGrenade"};
      for (const char* x : kA) BsQueueGive(std::string("ShockGame.") + x + " x99");
    }
    ImGui::SameLine();
    if (ImGui::Button("All plasmids + tonics")) {
      static const char* kP[] = {
          "ElectricBolt", "ElectricBoltTwo", "ElectricBoltThree", "Incineration", "IncinerationTwo",
          "IncinerationThree", "Telekinesis", "TelekinesisTwo", "IcicleAssault", "IcicleAssaultTwo",
          "IcicleAssaultThree", "InsectSwarmPlasmid", "InsectSwarmPlasmidTwo", "InsectSwarmPlasmidThree",
          "AirBlast", "AirBlastTwo", "AggressorIrritant", "BerserkRage", "SecurityBeacon",
          "DecoyHuman", "SummonProtector", "SummonProtectorTwo", "Teleportation", "SpeedBoost",
          "Sanctuary", "SpringBoardTrap", "SpringBoardTrapTwo", "LifeDrain", "LifeDrainTwo",
          "LifeDrainThree", "Fireball",
          // tonics
          "ArmoredBody", "ArmoredBodyTwo", "AsbestosBody", "AsbestosBodyTwo", "BioAmmoEfficiency",
          "BioAmmoPoolBoost", "BioAmmoPoolBoostTwo", "BioAmmoPoolBoostThree", "BloodLust",
          "BoozeHound", "ChameleonBlood", "ChargedBursts", "ChargedBurstsTwo", "Chemosynthesis",
          "ChemosynthesisTwo", "ColdResistance", "CriticalHitBonus", "DevastatingCriticalHits",
          "DiseaseImmunity", "ElectricBody", "ElectricBodyTwo", "ElectricTouch", "EyeForDetail",
          "EyeForDetailTwo", "FastEquipper", "FastReloader", "FastTwitch", "FastTwitchTwo",
          "FreezingNimbus", "FreezingTouch", "GeneticHacker", "GeneticHackerTwo",
          "GeneticHackerThree", "HackingExpert", "HackingExpertTwo", "HealthPoolBoost",
          "HealthPoolBoostTwo", "HealthPoolBoostThree", "HealthyConsumer", "HealthyConsumerTwo",
          "HealthyConsumerThree", "MachineBully", "MedicineFriendly", "MeleeMaster", "MeleeMasterTwo",
          "NearSightedCameras", "OrganicPockets", "PhotosensitiveSecretions", "PlasmidSynergy",
          "ProlificCrafter", "Rechargeable", "RecyclingExpert", "SecuritySystemsExpert",
          "ShorterAlarm", "SlowFlow", "SneakAttack", "StationExpert", "SteadyHand",
          "SuperHeatedBolts", "ThoroughScavenger", "VendingExpert", "VulnerabilityKnowledge",
          "WrathfulCurse"};
      for (const char* p : kP) BsQueueGive(std::string("plasmid:") + p);
    }

    ImGui::SetNextItemWidth(300);
    bool gent = ImGui::InputText("##give", give_, sizeof(give_), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((ImGui::Button("Give class") || gent) && give_[0]) BsQueueGive(give_);
    {
      static bool verbose = false;
      if (ImGui::Checkbox("Verbose log", &verbose)) BsSetVerbose(verbose);
      ImGui::SameLine();
      if (ImGui::SmallButton("Clear log")) BsClearGiveLog();
      ImGui::BeginChild("##givelog", ImVec2(0, 110 * font_scale_), true);
      for (const auto& g : BsGiveLog()) ImGui::TextUnformatted(g.c_str());
      ImGui::SetScrollHereY(1.0f);
      ImGui::EndChild();
    }

    ImGui::SeparatorText("Health / EVE");
    {
      // Item class names from the 360 build's name table (MedHypoPickup / EVEHypoPickup /
      // BioAmmoHypo); several spellings are tried, whichever loads is given (the rest log "not found").
      static const char* kMed[] = {"ShockDesignerClasses.MedHypo"};  // confirmed 2026-11-01
      static const char* kEve[] = {"ShockDesignerClasses.BioAmmoHypo"};  // confirmed 2026-11-01
      auto give = [](const char* const* list, int n, int count) {
        for (int i = 0; i < n; ++i) BsQueueGive(std::string(list[i]) + " x" + std::to_string(count));
      };
      if (ImGui::Button("First aid kits +1")) give(kMed,1, 1);
      ImGui::SameLine();
      if (ImGui::Button("First aid kits max")) give(kMed,1, 99);
      ImGui::SameLine();
      if (ImGui::Button("EVE hypos +1")) give(kEve,1, 1);
      ImGui::SameLine();
      if (ImGui::Button("EVE hypos max")) give(kEve,1, 99);
      if (ImGui::Button("Max health + EVE upgrades (tonics)")) {
        for (const char* t : {"HealthPoolBoost", "HealthPoolBoostTwo", "HealthPoolBoostThree",
                              "BioAmmoPoolBoost", "BioAmmoPoolBoostTwo", "BioAmmoPoolBoostThree"})
          BsQueueGive(std::string("plasmid:") + t);
      }
      ImGui::SameLine();
      if (ImGui::Button("Refill health + EVE now")) BsQueueGive("refill");
    }

    ImGui::SeparatorText("Plasmids");
    // Spawns a Gene Bank (plasmid/tonic swap station) in front of you; walk up and use it.
    // Slot upgrades (the "+1 slot" Gatherer's Garden purchases): one item per slot, per track.
    // Class names from the name table (UGeneticSlotUpgrade); package tried both ways.
    if (ImGui::Button("Unlock all slots (plasmid + tonic tracks)")) {
      // Confirmed 2026-11-01: ShockGame.<Track>GeneticSlotUpgrade, one item = one slot.
      for (const char* t : {"ActiveGeneticSlotUpgrade", "EngineeringGeneticSlotUpgrade",
                            "PhysicalGeneticSlotUpgrade", "WeaponsGeneticSlotUpgrade"})
        for (int i = 0; i < 6; ++i) BsQueueGive(std::string("ShockGame.") + t);
    }
    ImGui::SameLine();
    if (ImGui::Button("Gene Bank here (swap plasmids/tonics)"))
      BsQueueGive("genebank");
    ImGui::SameLine();
    ImGui::TextDisabled("then walk up and use it");

    ImGui::SeparatorText("Weapon mods (live; untick to restore)");
    {
      // Ammo class defaults hold the firing behaviour (found with "props", 2026-11-01).
      using L = std::vector<const char*>;
      static const L kPis = {"Pistol_Bullet", "Pistol_AntiPersonnel", "Pistol_ArmorPiercing"};
      static const L kMG = {"MachineGun_Bullet", "MachineGun_ArmorPiercingBullet", "MachineGun_FrozenBullet", "MachineGun_RubberBullet"};
      static const L kSG = {"Shotgun_00Buck", "Shotgun_IonicBuck", "Shotgun_HighExplosiveBuck"};
      static const L kXB = {"Crossbow_Bolt", "Crossbow_ArmorPiercingBolt", "Crossbow_DiseaseBolt", "Crossbow_SuperHeatedBolt", "Crossbow_TrapBolt"};
      static const L kGL = {"GrenadeLauncher_FragGrenade", "GrenadeLauncher_LiquidNitrogen", "GrenadeLauncher_RPG", "GrenadeLauncher_StickyGrenade"};
      auto setAll = [](const L& cls, const char* prop, const std::string& val) {
        for (const char* c : cls) BsQueueCommand(std::string("set ") + c + " " + prop + " " + val);
      };
      if (ImGui::Checkbox("Full-auto everything", &mod_auto_))
        for (const L* g : {&kPis, &kSG, &kXB, &kGL}) setAll(*g, "UseFullAuto", mod_auto_ ? "True" : "False");
      ImGui::SameLine();
      if (ImGui::Checkbox("Pistol buckshot (6 rounds, spread)", &mod_pbuck_)) {
        setAll(kPis, "NumTracesToFire", mod_pbuck_ ? "6" : "1");
        setAll(kPis, "SpreadOfFire", mod_pbuck_ ? "(Pitch=450,Yaw=450,Roll=0)" : "(Pitch=0,Yaw=0,Roll=0)");
      }
      if (ImGui::Checkbox("Machine gun double shot", &mod_mg2_)) setAll(kMG, "NumTracesToFire", mod_mg2_ ? "2" : "1");
      ImGui::SameLine();
      if (ImGui::Checkbox("Crossbow triple + fast", &mod_xb3_)) {
        setAll(kXB, "NumProjectilesToFire", mod_xb3_ ? "3" : "1");
        setAll(kXB, "SpreadAngleOfFire", mod_xb3_ ? "4" : "0");
        setAll(kXB, "InitialVelocity", mod_xb3_ ? "12000" : "6000");
      }
      ImGui::SameLine();
      if (ImGui::Checkbox("Heat-seeking bolts", &mod_seek_)) setAll(kXB, "bShouldHeatSeek", mod_seek_ ? "True" : "False");
      if (ImGui::Checkbox("Grenade barrage (3x)", &mod_gl3_)) {
        setAll(kGL, "NumProjectilesToFire", mod_gl3_ ? "3" : "1");
        setAll(kGL, "SpreadAngleOfFire", mod_gl3_ ? "6" : "0");
      }
      ImGui::SameLine();
      if (ImGui::Checkbox("Grenades explode on impact", &mod_gimp_))
        setAll({"GrenadeLauncher_FragGrenade", "GrenadeLauncher_LiquidNitrogen"}, "ExplodeOnImpact", mod_gimp_ ? "True" : "False");
      // Rosie's rivet gun is a trace weapon like the pistol: same damage model, her stimuli set,
      // and the tracer FXClass.Tracer_RivetNEW on the weapon (props RosieRangedWeapon, 2026-11-01).
      if (ImGui::Checkbox("Rivet pistol (Rosie damage)", &mod_rivet_)) {
        BsQueueCommand(std::string("set Pistol_Bullet DamageStimuliSetName ") +
                       (mod_rivet_ ? "RosieRangedWeaponStimuliSet" : "StandardBulletStimuliSet"));
        // No tracer swap: Rosie's Tracer_RivetNEW is built for her slow rivets and smears long
        // smoke streaks across the screen on the hitscan pistol. Always clear any old one.
        BsQueueCommand("set Pistol TracerEffects ()");
      }
      if (ImGui::Button("Ammo cap 999 (reload save to undo)"))
        for (const L* g : {&kPis, &kMG, &kSG, &kXB, &kGL}) setAll(*g, "MaximumStackSize", "999");
      ImGui::SameLine();
      if (ImGui::Button("Untick all")) {
        bool* all[] = {&mod_auto_, &mod_pbuck_, &mod_mg2_, &mod_xb3_, &mod_seek_, &mod_gl3_, &mod_gimp_, &mod_rivet_};
        BsQueueCommand("set Pistol_Bullet DamageStimuliSetName StandardBulletStimuliSet");
        BsQueueCommand("set Pistol TracerEffects ()");
        for (bool* b : all) *b = false;
        for (const L* g : {&kPis, &kSG, &kXB, &kGL}) setAll(*g, "UseFullAuto", "False");
        setAll(kPis, "NumTracesToFire", "1"); setAll(kPis, "SpreadOfFire", "(Pitch=0,Yaw=0,Roll=0)");
        setAll(kMG, "NumTracesToFire", "1");
        setAll(kXB, "NumProjectilesToFire", "1"); setAll(kXB, "SpreadAngleOfFire", "0");
        setAll(kXB, "InitialVelocity", "6000"); setAll(kXB, "bShouldHeatSeek", "False");
        setAll(kGL, "NumProjectilesToFire", "1"); setAll(kGL, "SpreadAngleOfFire", "0");
        setAll({"GrenadeLauncher_FragGrenade", "GrenadeLauncher_LiquidNitrogen"}, "ExplodeOnImpact", "False");
      }
    }

    ImGui::SeparatorText("Loot override (spare save! reload level to undo)");
    static const char* kLoot[] = {"ShotgunPickupItem",        "MachineGunPickupItem",
                                  "GrenadeLauncherPickupItem", "ChemicalThrowerPickupItem",
                                  "CrossbowPickupItem",        "ResearchCameraPickupItem",
                                  "PistolPickupItem",          "WrenchPickupItem"};
    ImGui::SetNextItemWidth(260);
    ImGui::Combo("##loot", &loot_idx_, kLoot, IM_ARRAYSIZE(kLoot));
    ImGui::SameLine();
    if (ImGui::Button("All loot = this")) {
      BsQueueCommand(std::string("set LootItemSpecification ItemClass Class'ShockDesignerClasses.") +
                     kLoot[loot_idx_] + "'");
    }
    ImGui::SameLine();
    if (ImGui::Button("+ spawn a Thug to kill")) {
      BsQueueCommand(std::string("set LootItemSpecification ItemClass Class'ShockDesignerClasses.") +
                     kLoot[loot_idx_] + "'");
      BsQueueCommand("summon ShockAIClasses.SpawnedMeleeThug");
    }

    ImGui::SeparatorText("Spawn in front of me");
    static const char* kSpawn[] = {
        "SpawnedMeleeThug",          "SpawnedRangedAggressorPistol", "SpawnedRangedAggressorSMG",
        "SpawnedGrenadier",          "SpawnedAssassin",              "SpawnedCeilingCrawler",
        "SpawnedBouncer",            "SpawnedRosie",                 "SpawnedBouncerElite",
        "SpawnedRosieElite"};
    if (ImGui::Checkbox("Double encounters (every spawned enemy x2)", &double_enc_)) BsSetDoubleEncounters(double_enc_);
    if (ImGui::Checkbox("Freeze world (stays frozen after closing F9)", &freeze_world_)) BsFreezeWorld(freeze_world_);
    ImGui::SetNextItemWidth(260);
    ImGui::Combo("##spawn", &spawn_idx_, kSpawn, IM_ARRAYSIZE(kSpawn));
    ImGui::SameLine();
    if (ImGui::Button("Spawn")) {
      BsQueueCommand(std::string("summon ShockAIClasses.") + kSpawn[spawn_idx_]);
    }

    ImGui::SeparatorText("Player 2 (second controller)");
    {
      static int p2_idx = 0;
      static const char* kP2[] = {"SpawnedBouncer", "SpawnedRosie", "SpawnedMeleeThug", "SpawnedRangedThug"};
      ImGui::SetNextItemWidth(220);
      ImGui::Combo("##p2", &p2_idx, kP2, IM_ARRAYSIZE(kP2));
      ImGui::SameLine();
      if (ImGui::Button("Spawn P2")) { BsP2Request(false, kP2[p2_idx]); BsQueueCommand(std::string("summon ShockAIClasses.") + kP2[p2_idx]); }
      ImGui::SameLine();
      if (ImGui::Button("Release P2")) BsP2Release();
      ImGui::SameLine();
      if (ImGui::Button(BsP2KeyboardActive() ? "Keyboard -> P1 (F8)" : "Keyboard -> P2 (F8)")) BsP2ToggleKeyboard();
      static bool rec = false;
      if (ImGui::Checkbox("Record AI Big Daddy attacks (log anims + damage calls)", &rec)) BsSetAttackRecorder(rec);
      static bool realw = true;
      if (ImGui::Checkbox("P2 uses real Big Daddy weapons (off = simple damage)", &realw)) BsP2RealWeapons(realw);
      static bool ally = true;
      if (ImGui::Checkbox("P2 is your ally (hypnotized: enemies fight him, never hits Jack)", &ally)) BsP2Ally(ally);
      static bool trace = true, probe = true;
      if (ImGui::Checkbox("Trace P2 script calls (log summary every 5 s)", &trace)) BsSetTrace(trace);
      ImGui::SameLine();
      if (ImGui::Checkbox("Call-stack probe", &probe)) BsSetProbe(probe);
      static bool freeze_ctl = true, freeze_pawn = true;
      if (ImGui::Checkbox("Freeze P2's AI decisions while you drive", &freeze_ctl)) BsP2FreezeAI(freeze_ctl, freeze_pawn);
      ImGui::SameLine();
      if (ImGui::Checkbox("+ his body's state code (auto-off when he dies)", &freeze_pawn)) BsP2FreezeAI(freeze_ctl, freeze_pawn);
      static bool think = true;
      if (ImGui::Checkbox("Freeze P2's native AI think while you drive (perception, targeting, decisions)", &think)) BsP2FreezeThink(think);
      static bool trig = true;
      if (ImGui::Checkbox("P2 sets off level triggers (ghost scenes, scripted moments) - use a spare save", &trig)) BsP2Triggers(trig);
      static bool as_player = true;
      if (ImGui::Checkbox("Enemies treat P2 as a player while you drive (IsPlayer = yes)", &as_player)) BsP2AsPlayer(as_player);
      static bool squeeze = true;
      if (ImGui::Checkbox("Squeeze: P2 fits through tight spaces while you drive (Jack-sized collision)", &squeeze)) BsP2Squeeze(squeeze);
      static bool blind = true;
      if (ImGui::Checkbox("Blind P2's AI while you drive (clears its enemy/focus every tick)", &blind)) BsP2Blind(blind);
      static bool silence = false;
      if (ImGui::Checkbox("Silence P2's AI (CRASHES in fights - test only)", &silence)) BsP2Silence(silence);
      int view_mode = BsGetViewMode();
      ImGui::TextUnformatted("Camera:"); ImGui::SameLine();
      if (ImGui::RadioButton("P1", &view_mode, 0)) BsSetViewMode(0); ImGui::SameLine();
      if (ImGui::RadioButton("Alternate every frame (split test)", &view_mode, 1)) BsSetViewMode(1); ImGui::SameLine();
      if (ImGui::RadioButton("P2", &view_mode, 2)) BsSetViewMode(2);
      bool split = BsCoopSplit();
      if (ImGui::Checkbox("Two windows / co-op: controller = Jack (window 1), keyboard+mouse = Big Daddy (window 2)", &split)) BsSplitWindow(split);
      static bool split_default = true;
      if (ImGui::Checkbox("Open co-op split when P2 spawns (D-pad down x5 / triple F8)", &split_default)) BsSplitDefault(split_default);
      if (split) {
        bool inv = BsSplitInverted();
        if (ImGui::Checkbox("Swap windows (F7)", &inv)) BsSplitInvert(inv);
        ImGui::SameLine();
        if (ImGui::Button(BsPadOnP2() ? "Controller -> Jack (Back / F6)" : "Controller -> Big Daddy (Back / F6)")) BsPadSwap();
        ImGui::SameLine();
        if (ImGui::Button("HUD to other window (F5)")) BsHudFlip();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", BsSplitStats().c_str());
      }
      static bool body = false;
      if (ImGui::Checkbox("Give Jack a body in the Big Daddy's view (Splicer model, EXPERIMENTAL; reload save to undo)", &body)) BsJackBody(body);
      ImGui::TextDisabled(BsP2Pawn() ? "P2 keys: WASD move, mouse turn, LMB punch, RMB drill, F slam, C or Shift+F charge, R shove, T roar, Space jump, Shift run, E pick up, Alt free mouse | pad: RT punch, LT/LB/RB drill, B slam, D-right or L3+B charge, Y shove, D-up roar, A jump, X pick up, L3 run, Back = swap controller"
                                     : "no P2 yet: pick a body, Spawn P2");
    }

    ImGui::SeparatorText("Custom command");
    ImGui::SetNextItemWidth(300);
    bool enter = ImGui::InputText("##cmd", custom_, sizeof(custom_),
                                  ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((ImGui::Button("Run") || enter) && custom_[0]) BsQueueCommand(custom_);

    ImGui::SeparatorText("Find game commands (scan names)");
    ImGui::SetNextItemWidth(200);
    ImGui::InputText("##scan", scan_, sizeof(scan_));
    ImGui::SameLine();
    if (ImGui::Button("Scan") && scan_[0]) BsScanNames(scan_);
    ImGui::SameLine();
    ImGui::TextDisabled("click a hit to run it");
    if (ImGui::BeginChild("##hits", ImVec2(0, 120), true)) {
      for (const auto& h : BsNameHits())
        if (ImGui::Selectable(h.c_str())) {
          std::string c = h;
          if (c.size() > 3 && c.compare(c.size() - 3, 3, "(w)") == 0) c.resize(c.size() - 3);
          BsQueueCommand(c);
        }
    }
    ImGui::EndChild();

    ImGui::SeparatorText("Results");
    for (const auto& r : BsRecentResults()) ImGui::TextUnformatted(r.c_str());
    ImGui::End();
  }

 private:
  // static: the dialog object is recreated every time F9 opens; panel state must outlive it.
  static inline std::atomic<bool> visible_{false};
  static inline bool just_opened_ = false;
  uint16_t pad_prev_ = 0;
  bool combo_prev_ = false;
  static inline float font_scale_ = 1.4f;
  static inline bool god_ = false;
  static inline bool noclip_ = false;
  static inline int map_idx_ = 2;
  static inline int loot_idx_ = 0;
  static inline bool mod_auto_ = false, mod_pbuck_ = false, mod_mg2_ = false, mod_xb3_ = false, mod_seek_ = false,
                     mod_gl3_ = false, mod_gimp_ = false, mod_rivet_ = false;
  static inline bool freeze_world_ = false;
  static inline bool double_enc_ = false;
  static inline int spawn_idx_ = 0;
  static inline char custom_[128] = {};
  static inline char scan_[64] = {};
  static inline char give_[128] = "ShockDesignerClasses.ShotgunPickupItem";
};
