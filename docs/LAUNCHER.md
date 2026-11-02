# Installer and launcher

Two files in the repo root get a player from "I own the disc" to "playing":

| File | What it is |
|---|---|
| `Install.ps1` | One-time setup. Installs the build tools, extracts your disc image, checks it, builds the game, makes a desktop shortcut. |
| `BioShock.cmd` | Opens the launcher window (`Launcher.ps1`). The desktop shortcut points here. |

Nothing from the game is downloaded or included. You need your own dump of **BioShock (Xbox 360) disc 1**.

## 1. Install (once)

1. Put the repo somewhere short, e.g. `C:\Games\bioshock-recomp` (download the release zip and extract it, or `git clone`).
2. Right-click `Install.ps1` > **Run with PowerShell**. Approve the admin prompt.
3. Pick your `.iso` in the file dialog.
4. Wait. First run is 1-2 hours on a typical PC (Visual Studio download ~10 GB, then compiling).

What it does, step by step (every step is skipped if already done, so re-running is safe):

| Step | Detail |
|---|---|
| Build tools | winget installs Git, CMake, Ninja, Python 3.12, Visual Studio 2022 C++ + Clang, LLVM 20+ |
| Your game | Downloads extract-xiso (open source) and extracts the ISO into `game_base1\` |
| Version check | `default.xex` SHA-1 must be `1be6a0fadea96b3825e6213a4a4ae1a0e4c96c91`. Other versions stop here |
| Build | Builds the patched ReXGlue SDK, generates C++ from your XEX, compiles `bioshock.exe` |
| Shortcut | "BioShock Recomp" on the desktop |

Options: `.\Install.ps1 -Iso D:\BioShock.iso` (skip the file dialog), `-SkipPrereqs` (tools already installed), `-NoShortcut`.

Requirements: Windows 10/11 x64, ~60 GB free, internet, admin rights, a DirectX 12 GPU.

## 2. Launcher

Double-click the desktop shortcut (or `BioShock.cmd`).

| Control | Meaning |
|---|---|
| Look | Visual preset. **Pure** (default): no blur, 2x resolution, tuned brightness. **Crisp**: Pure + light bloom. **Sharp**: strongest anti-aliasing. **Clean** (3x) / **Ultra** (4x): supersampling, heavy GPU. **Cinematic**: original depth-of-field and bloom |
| Frame rate | 60 (default) or 30 (original) |
| Resolution | Override the look's render scale: 1 = 720p, 2 = 1440p, 3 = 2160p, 4 = 2880p |
| Brightness / Gamma | Blank = the look's value. Pure uses 1.55 / 1.05. Raise brightness first; gamma above ~1.1 causes banding |
| **Play** | Start the game with these settings |
| **Build + Play** | Rebuild after updating the repo (`git pull`), then play |
| **Last run report** | Crashes, GPU errors, stutters and fps from the last session |
| **Open logs folder** | Logs to attach when reporting a problem |
| **Crash-fix loop (dev)** | For developers: run, catch a missing function, declare it, rebuild, repeat |
| **In-game keys** | Key list |

Settings are remembered in `run\launcher.json`.

## 3. In game

| Key | Action |
|---|---|
| F9, or click both sticks (L3+R3) | Show/hide the Testing panel (A-/A+ changes its text size) |
| F10 | Mark a glitch in the log |
| F4 | Emulator settings |
| F3 | Debug overlay |

While the panel is open the game ignores your controller and keyboard. Navigate with the D-pad or left stick, **A** to press/tick, **B** to back out, **X/Y** to edit a text box; on keyboard use the arrow keys, Enter/Space and Esc. The mouse works as before.

F9 panel sections: Player (god mode, noclip, teleport), Travel to level, Give (all weapons / ammo / plasmids + tonics, any class), Weapon mods (full-auto, pistol buckshot, MG double shot, triple crossbow, heat-seeking bolts, grenade barrage / impact, rivet pistol, ammo cap 999, Untick all), Loot override, Spawn (with Freeze world), Custom command (`get <Class> <Property>`, `props <Class> [filter]`, `set ...`), Find game commands.

Use a spare save for cheats: level travel and loot changes can skip story state. Weapon mods last until you quit the game.

## 4. Updating

```powershell
git pull
```
Then **Build + Play** in the launcher. If the update changed the SDK patch, run `Install.ps1 -SkipPrereqs` once.

## 5. Problems

| Symptom | Fix |
|---|---|
| Installer stops at the version check | Your disc is a different build/region. Not supported yet |
| "Can't create symlinks" | Run the installer as admin, or turn on Developer Mode (Settings > System > For developers) |
| Game closes on start | Open **Last run report** and the newest file in the logs folder; file an issue with both |
| Too dark / too bright | Brightness box (try 1.3-1.9) |
| Stutter in a new area | First-visit shader compiles; it goes away on revisit |

Never share your `bioshock.exe`, `app\generated\`, `game_base1\` or logs that contain paths: they are derived from your copy of the game.
