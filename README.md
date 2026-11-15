# bioshock-recomp

Static recompilation of **BioShock (Xbox 360)** to a native Windows x64 game, built with the
[ReXGlue SDK](https://github.com/rexglue/rexglue-sdk). Plays from the start into Rapture at 60 fps with
sharper-than-console visuals, plus an in-game testing/cheat panel.

<a href="https://recomplauncher.com"><picture><source media="(prefers-color-scheme: dark)" srcset="https://recomplauncher.com/assets/press/launcher/badge-light.svg"><img alt="Get it on RecompLauncher" src="https://recomplauncher.com/assets/press/launcher/badge-dark.svg" height="48"></picture></a>

**No game files, and nothing generated from them, are in this repository or will ever be distributed.**
You build the game yourself from your own legally dumped disc. Every user compiles their own copy.

## What you get that the PC versions don't have
- **Co-op.** A second player joins as a Big Daddy in their own window, with their own controller or keyboard and
  mouse. A friend on another PC can play by streaming that window (Parsec / Moonlight). No official version of BioShock
  has co-op. **Early alpha:** it works, and it has rough edges.
- **A population dial.** Turn the number of splicers in each area up or down, and choose whether areas refill after
  you clear them.
- **A cheat menu for the campaign (F9)** that pauses the world while it is open, in six tabs:
  - **Player:** god mode, noclip, teleport, field of view, health and EVE kits and upgrades, travel to any level.
  - **Items:** all weapons, all ammo, all plasmids and tonics, unlock every slot, a Gene Bank anywhere.
  - **Weapons:** live weapon mods (full-auto everything, buckshot pistol, heat-seeking bolts, grenade barrage and
    more) and a loot override.
  - **Enemies:** spawn any enemy, double encounters, the population dial, freeze the world.
  - **Co-op** and **Dev** (custom console commands, logs).

  Works with a controller: LB / RB switch tabs.
- **Picture controls the console never had:** 2x-3x resolution, 16:9 or 21:9 ultrawide, a field-of-view slider,
  sharpening, anti-aliasing and 60 fps.
- **Open and yours to change.** You build it from source, so you can read it, fix it and mod it. No account, no store
  client.

### Under the hood: this build vs the PC release
- **Physics at 60 fps.** On the PC release the physics are locked to 30 updates a second, so ragdolls and loose objects
  move at half the frame rate unless you install a community mod. Here the game logic itself runs at 60, so ragdolls,
  objects and animation all move at the full frame rate. The original 30 is one click away in the launcher.
- **The console game, not a port of it.** This is the Xbox 360 code recompiled to run natively: the console's
  controller-first interface, weapon and plasmid wheels and aim assist, as the game shipped there.
- **Changed from inside the engine.** Co-op, the population dial and the live weapon mods work by reaching into the
  running game's own objects and script functions, which is why they don't need modified game files.

What the PC versions still do better: higher-resolution textures, native mouse and keyboard menus, and far less setup.
This is the Xbox 360 game running natively, not a replacement for the PC release.

## Get it running (players)

You need: Windows 10/11 x64, ~60 GB free disk, admin rights, internet, and an `.iso` you dumped from
your own **BioShock (Xbox 360) disc 1** (TitleID 545407D8).

1. Download this repo (green **Code** button > Download ZIP, or `git clone`) to a short path such as `C:\Games\bioshock-recomp`.
2. Right-click **`Install.ps1`** > **Run with PowerShell**, and pick your `.iso` when asked.
   It installs the build tools (Visual Studio C++ + Clang, CMake, Ninja, Git, Python), extracts your disc,
   checks it is the supported build, then compiles the game. First run: 1-2 hours, mostly downloads and compiling.
3. Start the game from the **BioShock Recomp** desktop shortcut (or `BioShock.cmd`).

The launcher lets you pick a look preset, 60 or 30 fps, resolution and brightness, and rebuild after `git pull`.
Full guide: [docs/LAUNCHER.md](docs/LAUNCHER.md).

| Key | In game |
|---|---|
| F9 | Testing panel: god mode, noclip, teleport, level travel, give all weapons / ammo / plasmids, spawn enemies, freeze world, weapon mods |
| F10 | Mark a glitch in the log |
| F4 / F3 | Emulator settings / debug overlay |
| F9 > Co-op > Spawn P2 | Co-op: spawn a Big Daddy in a second window. Friend on another PC: stream the window with Parsec / Moonlight, see [docs/COOP.md](docs/COOP.md). "End co-op" on the same tab removes him |
| Controller 2: A / Start, D-pad down x5, triple F8 | Co-op shortcuts that spawn the Big Daddy. Off by default; switch them on in F9 > Co-op |
| F11 | Co-op: Big Daddy window fullscreen |
| Back / F6 | Co-op: move the controller between Jack and the Big Daddy |
| F7 | Co-op: views on the wrong windows? swap them (remembered next launch) |
| F5 | Co-op: Big Daddy health bar on the wrong window? move it (remembered) |
| Hold D-pad left / Q | Co-op: Big Daddy action wheel |
| Alt | Co-op: free the mouse from the Big Daddy window |
| F8 | Keyboard + mouse: control Jack / the Big Daddy |

Use a spare save for the F9 panel: level travel and loot changes can skip story state.

**Only one disc build is supported:** `default.xex` SHA-1 `1be6a0fadea96b3825e6213a4a4ae1a0e4c96c91`.
The installer stops if yours differs (other regions/versions need their own address work).

## RecompLauncher

`recomplauncher.json` describes the built game to [RecompLauncher](https://recomplauncher.com): which copy it needs,
how it starts, and five settings (resolution, 16:9 or 21:9, frame rate, sharpening, anti-aliasing).
This project publishes no built game, so RecompLauncher cannot download one. After you have built your own copy
with `Install.ps1`, run `tools\package_local.ps1` and add the folder it makes with RecompLauncher's
Settings > Developers > Developer tools > "Add a build". That folder contains code generated from your disc: keep it on your PC.

RecompLauncher asks for your copy as a **game folder**: the files extracted from your disc image, with `default.xex`
at the top. If you ran `Install.ps1` that folder already exists as `game_base1`. To make one from your `.iso` on its own:

```powershell
.\tools\extract_disc.ps1                                             # asks for the .iso, extracts to .\game_base1
.\tools\extract_disc.ps1 -Iso D:\BioShock.iso -Out D:\Games\BioShock360   # or choose both
```

It downloads the open-source `extract-xiso`, extracts the disc (about 2 GB) and checks it is the supported build.
By hand, the same thing is `extract-xiso -d <folder> <your.iso>`.

## Status

Boots, menus, new game, Lighthouse, Welcome to Rapture, Medical Pavilion, saving. See `docs/STATUS.md`.
Not yet verified start to finish.

## Developers

- Build from a dev shell: `. .\tools\vsenv.ps1; .\go` (build + launch). `.\go -NoBuild -Look Clean` passes options to `tools\play.ps1`.
- `tools\fix_loop.ps1`: run, catch "unregistered function", declare it, rebuild, repeat.
- `tools\report.ps1`: crash / GPU-error / hitch summary of the last run.
- SDK changes live in `patches/rexglue-sdk/` and are applied by `tools\setup.ps1` on top of the pinned tag.

## Legal

BioShock is (c) Take-Two Interactive / 2K. This project contains only original code, scripts, patches and
documentation. It is not affiliated with or endorsed by 2K or Take-Two. Don't share your built `bioshock.exe`,
`app/generated/`, or any extracted game files.
