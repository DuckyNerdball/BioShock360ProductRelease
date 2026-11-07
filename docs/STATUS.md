# Status

## Disc set: BioShock Ultimate Rapture Edition (Xbox 360, 4 discs)
| # | Disc | Dumped | Folder | Notes |
|---|---|---|---|---|
| ? | BioShock 1 base game | yes | `game_base1/` | **Primary recomp target.** ISO SHA-1 F97BD76D22E31524D083400EC225D75E7C44E088. `default.xex` TitleID 545407D8, module default.exe, SHA-1 1BE6A0FADEA96B3825E6213A4A4AE1A0E4C96C91. 492 files / 4.98 GB. |
| ? | BioShock 1 add-on | yes | `game_dlc1/` | ISO SHA-1 06456D65D039032E71ED757BC486942FE9F747E2. Boot XEX = ExpansionInstaller (FFED2000). PIRS package 545407D8, content type 2, SHA-1 AEA02C6135C95499F2D2D6C5297F1BBA74674203; contains `Bioshock_Xenon.xex`, engine .u files, Challenge Rooms, museum. |
| ? | BioShock 2 base game | no | `game_base2/` | Out of scope for now |
| ? | BioShock 2 add-on | no | `game_dlc2/` | Out of scope for now |

## Target binary
- Region / title ID: 545407D8 (confirm from base disc)
- Title update: _(none / TU#)_ — note the add-on disc ships a `title_update.bin`
- Base `default.xex` SHA-1: _(run tools/inspect_disc.ps1)_
- ReXGlue SDK: v0.10.0 + patches in `patches/rexglue-sdk/`

## Milestones
- [x] Add-on disc dumped and inspected
- [ ] Base disc dumped, inspected, hashes recorded
- [ ] Xenia Canary baseline run: where does it break? (notes below)
- [ ] `rexglue codegen` completes (with `--force`)
- [ ] Codegen completes without `--force`
- [ ] Project compiles and links
- [ ] Boots: first log output / logos
- [ ] Main menu
- [ ] In-game (Welcome to Rapture)
- [ ] DLC content (Challenge Rooms / museum) loads via VFS
- [ ] Playable start to finish
- [ ] Enhancements: KB/M, widescreen, uncapped FPS

## Current blocker
Dump the BioShock 1 base game disc.

## Xenia baseline notes

## Findings log
<!-- YYYY-MM-DD — address — what it was — fix -->
- 2026-10-31 — n/a — 7.8 GB ISO size is fixed XGD2 size; only 2.05 GB is files (extract-xiso and xdvdfs agree); rest is video partition + random padding.
- 2026-11-01 - 0x8248AF70 - runtime: call to unregistered function - declared in [entrypoint.functions] (fix_loop)
- 2026-11-01 - 0x82802E08 - runtime: call to unregistered function - declared in [entrypoint.functions] (fix_loop)
- 2026-11-01 - 0x82204D40 - runtime: call to unregistered function - declared in [entrypoint.functions] (fix_loop)
- 2026-11-01 - 0x8281D3E8 - runtime: call to unregistered function - declared in [entrypoint.functions] (fix_loop)
- 2026-11-01 - 0x827A95F8 - runtime: call to unregistered function - declared in [entrypoint.functions] (fix_loop)
- 2026-11-01 - 0x821C3E28 - runtime: call to unregistered function - declared in [entrypoint.functions] (fix_loop)
- 2026-11-01 - 0x821CAF30 - runtime: call to unregistered function - declared in [entrypoint.functions] (fix_loop)
- 2026-11-01 - 0x8254FC90 - runtime: call to unregistered function - declared in [entrypoint.functions] (fix_loop)
- 2026-11-01 - 0x82473BA8 - runtime: call to unregistered function - declared in [entrypoint.functions] (fix_loop)
- 2026-11-01 - 0x82176760 - runtime: call to unregistered function - declared in [entrypoint.functions] (fix_loop)
- 2026-11-01 - 0x825FDA60 - runtime: call to unregistered function - declared in [entrypoint.functions] (fix_loop)
- 2026-11-01 - 0x826144F8 - runtime: call to unregistered function - declared in [entrypoint.functions] (fix_loop)
- 2026-11-01 - 0x827EF408 - runtime: call to unregistered function - declared in [entrypoint.functions] (fix_loop)
- 2026-11-01 - 0x82473B20 - runtime: call to unregistered function - declared in [entrypoint.functions] (fix_loop)
- 2026-11-01 - 0x8274BA20 - runtime: call to unregistered function - declared in [entrypoint.functions] (fix_loop)
- 2026-11-01 - 0x82473E80 - runtime: call to unregistered function - declared in [entrypoint.functions] (fix_loop)
- 2026-11-01 - 0x826151B8 - runtime: call to unregistered function - declared in [entrypoint.functions] (fix_loop)
- 2026-11-01 - 0x8274BA60 - runtime: call to unregistered function - declared in [entrypoint.functions] (fix_loop)
- 2026-11-01 - 0x822F3200 - runtime: call to unregistered function - declared in [entrypoint.functions] (fix_loop)
- 2026-11-01 - GPU: depth-resolve pitch fixed (patch 0001). Blur NOT caused by it; parked. Next: 360 reference check + RenderDoc capture.
- 2026-11-01 - 0x822E62B8 - runtime: call to unregistered function - declared in [entrypoint.functions] (fix_loop)
- 2026-11-01 - 0x82555828 - runtime: call to unregistered function - declared in [entrypoint.functions] (fix_loop)
- 2026-11-01 - 0x824FBB90 - runtime: call to unregistered function - declared in [entrypoint.functions] (fix_loop)
- 2026-11-01 - Blur: composite gets ~0 from full-res 7e3->10_11_11 resolve; workaround --resolve_1011_exp_bias=-2 (full-res only). Proper fix TBD (likely texture-fetch exp_adjust).
- 2026-11-01 - BLUR FIXED: second full-res 7e3->10_11_11 resolve (exp_bias -3) overwrote the sharp HDR copy with near-zero data before the final composite. Fix: --skip_hdr_1011_resolve_bias=-3 + --resolve_1011_exp_bias=0 + --fetch_1011_exp_adjust_delta=-3 (play.ps1 defaults; -Delta to tune brightness).
- 2026-11-01 - 0x827DF4D0 - runtime: call to unregistered function - declared in [entrypoint.functions] (fix_loop)
- 2026-11-01 - Visuals locked: Pure look (default). Composite PS 32B3EC0C91CA5BCC = gamma(sat((blur320*c1 + scene)*c2)), c1=0.4 c2=1; sharp scene fed into blur slot, exposure 1.3, gamma 1.2 (c254.z), Delta -4, 2x, CAS 0.7, FXAA. New cvars: bioshock_bloom_scale / bioshock_exposure_scale / bioshock_gamma.
- 2026-11-01 - 60 fps: guest vblank 120 Hz (--video_mode_refresh_rate=120) -> guest 55-74 fps, game logic speed normal. Default; play.ps1 -Fps30 for original. Hitch logger: remaining hitches 70-90 ms = 1-2 new PSO compiles (first visit only).
- 2026-11-01 - FIXED black characters/turrets at 2x: shadow PCF pass (PS 232F7598AED463AF) uses tfetch OffsetX/Y on the shadow map; SDK scaled those offsets with draw resolution, so samples missed. Fix: --draw_resolution_scaled_texture_offsets=false (look.ps1 default). Remaining flicker = game's flickering lamps (correct).
- 2026-11-01 - F9 Testing panel: god / noclip / teleport / level travel / freeze / spawn (viewport Exec 0x82205F20, queued on engine vtable slot 57 = Tick).
- 2026-11-01 - Direct give (no pickup needed): StaticLoadObject 0x82466660 + fake stack record (+0x48 class, +0x4C count) into UInventoryManager::AddStackToInventory 0x824BE0D8. Player captured from AShockPlayer exec natives (0x824CA238/0x824CA220/0x824C8310); GameInfo = player+0xE0 -> +0x7AC; InventoryManager = player field whose +0x38 == player; UClass metaclass via obj+0x2C chain.
- 2026-11-01 - Plasmids/tonics: PlasmidManager = player+0x928. Find class 0x82547268 (null-safe), owned 0x82546F18, upgrade 0x82549170, add 0x82548290(pm, FName*, -1). AddAvailablePlasmid 0x824DA720's pickup-message path derefs unchecked pointers (crash at 0x500 / 0x4C) -> upgrades added directly.
- 2026-11-01 - Console "get" never reaches UObject::StaticExec (0x82464AC0) on this build. Native get/props: FindObject 0x821C5028(-1, name, 0), FindField 0x822A0870, TFieldIterator next 0x821B07A0 (Children +0x44, Next +0x3C, Name FName +0x24), defaults = class+0x500 (count +0x504), UProperty::ExportText = vtable+280 into an FStringOutputDevice (vtable 0x82025BB4).
- 2026-11-01 - Weapon behaviour lives on ammo class defaults: trace ammo (Pistol_*/MachineGun_*/Shotgun_*: NumTracesToFire, SpreadOfFire, UseFullAuto, DamageModel=TraceDamageFactory), projectile ammo (Crossbow_*/GrenadeLauncher_*: ProjectileDamageFactory, ProjectileClass, InitialVelocity, NumProjectilesToFire, SpreadAngleOfFire, bShouldHeatSeek, ExplodeOnImpact). F9 "Weapon mods" toggles these via set.
- 2026-11-01 - Install.ps1 (one-shot newcomer install from own ISO, SHA-1 check) + Launcher.ps1/BioShock.cmd (WinForms launcher) + tools/vsenv.ps1.
- 2026-11-01 - Rosie's rivet gun = trace weapon (ShockAI.RosieRangedWeaponAmmo: TraceDamageFactory, RosieRangedWeaponStimuliSet, range 3200) + weapon TracerEffects FXClass.Tracer_RivetNEW. F9 'Rivet pistol' sets both on Pistol/Pistol_Bullet.
- 2026-11-01 - Fixed crash after loading a save then giving (0x00200150): cached InventoryManager/GameInfo from the previous level. Now reset when the player pointer changes and validated (inv+0x38 == player) before each give.
- 2026-11-01 - F9 text size (A-/A+, default 140%). docs/LAUNCHER.md (install + launcher guide). tools/release.ps1 (git-archive scripts-only GitHub release via gh).
- 2026-11-01 - F9 panel: controller + keyboard navigation (ImGui nav, host XInput poll), L3+R3 toggles it, game input blocked while open (InputSystem active callback replaced in OnPreLaunchModule). Panel object now lives for the whole session.
- 2026-11-01 - Gene Bank anywhere WORKS: placed actors found via level actor list (player field -> ULevel -> Actors TArray containing player), class match walks UField SuperField +0x38. Model = StaticMesh'MA_Plasmid_equip.plasmid_equip_machine' (DT_StaticMesh); copied onto PlasmidEquipStation defaults before summon; fallback loads it by name in levels without one.
- 2026-11-01 - Slot unlock works: ShockGame.{Active,Engineering,Physical,Weapons}GeneticSlotUpgrade via AddStackToInventory (one = one slot). Med/EVE hypo item class names still unknown.
- 2026-11-01 - Health items: ShockDesignerClasses.MedHypo (first aid kit) / ShockDesignerClasses.BioAmmoHypo (EVE hypo). Base classes ShockGame.*HypoBase give an unnamed junk item - never give those.
- 2026-11-01 - Double encounters (F9 toggle): hook AActor::Spawn 0x822F8B80 (execSpawn 0x822F8C70 -> XLevel(+228) vtable+316 SpawnActor). Actor Location +448, Rotation +460. Pawn subclasses (excl. Player/LittleSister/Vehicle) are spawned a second time 90-130u away.
- 2026-11-01 - Black/grey streaks = Rosie Tracer_RivetNEW on pistol (confirmed by user). Rivet pistol is now damage-only; panel clears Pistol TracerEffects at startup (set persists to config).
- 2026-11-01 - Screen streaks / smeared bodies near corpses: RenderDoc (guest frame) shows a skinned character draw with ONE garbage vertex (W ~5e29, last vertex of the mesh) -> triangles stretched across screen. SDK patch: DXBC VS epilogue sets W=NaN when |W|>=1e8 or non-finite (cvar bioshock_kill_runaway_vertices, default on). Root cause (bad bone matrix vs out-of-range vfetch) still open.
- 2026-11-01 - Likely root cause of corpse streaks: bad vertex is the LAST vertex of the mesh (index 949 of 950) -> out-of-bounds vfetch; SDK did no bounds check (upstream FIXME). Patch adds vfetch bounds check (cvar vfetch_bounds_check, default on): OOB reads return 0.
- 2026-11-01 - P2 puppet: movement via ULevel::MoveActor (XLevel +228, vtable+288; args as in AActor::execMove 0x822F7BA0), attack = AShockPawn::RemoveHealth (vtable+916, f1) on AI pawns within 250u in front.
- 2026-11-01 - P2 Big Daddy plays its own anims (BO_PushBackSweep swing, BO_DrillSpin drill) via anim component actor+992 vtable+8 (channel, &FName, 1, flag) as in execPlayAnimationOnChannel 0x822F6558. Health float at pawn+1356.
- 2026-11-01 - RemoveHealth (vtable+916) lowers health without death. Use TakeSimpleDamage core 0x8250EFB0 (pawn, r4 dmgtype byte, f1 dmg, f2 1.0, r7 instigator, r8 &FName None).
- 2026-11-01 - P2 damage: TakeSimpleDamage core with damage type 17 + instigator = P1 kills splicers. Kill notifies all pawns' sensors -> needs P2's controller present (restore around the call; null -> crash write +0x47C).
- 2026-11-01 - Recorded AI Big Daddy: melee BO_AttackMelee_F/H/I/K + BO_AttackShoulderSlamLONG (channel 2, flag 1), BO_DrillSpin (channel 3, flag 8 loop). Weapons BouncerMeleeHandWeapon/ShoulderWeapon/ThreeSixtyWeapon; AWeapon::InitiateDamage(FName) = vtable+740.
- 2026-11-01 - Crash 0x4D8: P2 hit by an AI Big Daddy while its controller was detached (damage reaction reads controller). P2 now keeps its AI controller; our input overrides position/rotation each tick. Real BD weapon attacks: 'BigRightHook'/'BigLeftHook' via AWeapon::InitiateDamage (vtable+740). P2 hits on other Big Daddies do 0 (type 17 resisted).
- 2026-11-01 - BD walk anims: BO_walk{FWD,BWD,LEFT,RIGHT}_{idle,agg}; P2 plays them on channel 0 flag 8 (loop) by stick direction.
- 2026-11-01 - Big Daddy ally state: DispositionToPlayer +0x1070 enum (0 none,1 hostile,2 friendly), bProtectingPlayer, EndProtectingPlayerTime +0x1078. P2 'ally' sets these every tick.
- 2026-11-01 - Splicer->P2 aggro: setting AIController.Enemy (+0x4A0) is ignored; 1-dmg 'provoke' with P2 instigator crashed (invalid indirect call right after pause menu) -> removed. Needs the splicer target-tracker path.
- 2026-11-01 - Splicer aggro on P2 via AEcologyFighter::AddForcedEnemy 0x825DB088(splicer, P2). F9 log: diagnostics hidden unless 'Verbose log'.
- 2026-11-03 - 0x823A9670 - runtime: call to unregistered function (co-op run) - declared in [entrypoint.functions]
- 2026-11-03 - Co-op: UObject::ProcessEvent = 0x8247FFB0 (this, UFunction*, parms); CallEvent(obj, "Name", {args}) calls script by name. Doors: TriggerRadius.MeetsTriggerCriteria -> DispatchEnter -> Pawn.CanOpenDoors -> Door.AddUniquePawnToPawnsInsideDoor; scoped yes-overrides while the Big Daddy's own MoveActor runs. Pickups: X.OnUsed(user) -> GetLoot -> AddStackToInventory; diaries same path -> AddLogEntry -> PlayLogEntry. Split view: per-frame label queue pushed by the game thread, popped by VdSwap, always (enabling late shifted labels by one). Never skip a script call in the CallFunction hook (VM freeze): run it and undo the result. Level travel pauses P2 ticks (freed actor arrays).
- 2026-11-03 - Co-op remote play: a dedicated pad (auto = first XInput index >= 1, or F9 pick) always drives the Big Daddy; A/Start on it spawns him; keyboard+mouse return to Jack while that pad is connected (BsP2KbmDrives). F11 = window 2 fullscreen. Networking = stream window 2 (Parsec / Sunshine+Moonlight), guest pad arrives as controller 2. Real netcode ruled out (no multiplayer in the game; would need full actor replication). docs/COOP.md.
