# bioshock-recomp

Static recompilation of **BioShock (Xbox 360)** to a native Windows x64 game, built with the
[ReXGlue SDK](https://github.com/rexglue/rexglue-sdk). Plays from the start into Rapture at 60 fps with
sharper-than-console visuals, plus an in-game testing/cheat panel.

**No game files, and nothing generated from them, are in this repository or will ever be distributed.**
You build the game yourself from your own legally dumped disc. Every user compiles their own copy.

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
| Controller 2: A / Start | Co-op: spawn a Big Daddy in a second window; that controller drives him. Friend on another PC: stream the window with Parsec / Moonlight, see [docs/COOP.md](docs/COOP.md) |
| D-pad down x5 | Co-op: spawn a Big Daddy, keyboard + mouse drive him (WASD, E use, LMB punch, RMB drill, Space jump, C charge, R shove, T roar) |
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
