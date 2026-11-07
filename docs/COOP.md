# Co-op: a friend plays the Big Daddy

BioShock has no multiplayer. This co-op runs **one game on one PC** (the host). The second player's
controller drives a Big Daddy in a second window. A friend on another PC plays by streaming that
window: their controller input comes back to the host as a second controller. No game state ever
crosses the network, so there is nothing to desync.

## Same couch (two controllers, one PC)
1. Plug in a second controller. It drives the Big Daddy automatically (controller 1 stays Jack's).
2. Press **A** or **Start** on it: a Big Daddy spawns next to Jack and his window opens.
3. Play. The Big Daddy window has his health bar. **F11** makes it fullscreen.

## Over the LAN / internet (friend on another PC)
Use a streaming tool; both of these give the guest a virtual controller on the host PC:
- **Parsec** (easiest): host runs Parsec, friend joins, friend's controller shows up as controller 2.
  Set Parsec to share only the window "BioShock - Player 2 (Big Daddy)" (Parsec > Settings > Host >
  "Share app window"), or press **F11** in that window and stream the whole screen on a second monitor.
- **Sunshine + Moonlight** (lower latency, LAN): add `bioshock.exe` as a Sunshine app, friend connects
  with Moonlight. Their controller arrives as controller 2.

Then it's the same as the couch: friend presses **A / Start** to spawn, plays in that window.

If the host's own controller ends up as controller 2 (plugged in after the guest's), open **F9** and
set "Big Daddy controller" to the right one, or press **Back / F6** to swap roles.

## Big Daddy controls (controller 2)
| Input | Action |
|---|---|
| Left stick / L3 | Walk / run |
| Right stick | Look |
| RT | Punch |
| LT / LB / RB | Drill |
| A | Jump |
| B | Ground slam |
| X | Use: pick up for Jack, loot, open / press |
| Y | Shove |
| D-pad up | Roar |
| D-pad right | Charge |
| Hold D-pad left | Action wheel: aim with the right stick, release to do it (punch, charge, slam, shove, jump, drill, use, roar) |

Keyboard + mouse drive him only when no second controller is connected (WASD, mouse, LMB punch,
RMB drill, Space jump, E use, R shove, T roar, C charge, F slam; hold Q + 1-8 = action wheel).

## Known limits
- Each window gets half the frame rate (the game renders Jack's frame, then the Big Daddy's).
- Streaming adds its own 20-50 ms; the Big Daddy is a melee tank, so it plays fine, but he feels heavier than Jack.
- Game menus, saves and the Vita-Chamber belong to Jack. The Big Daddy survives Jack's death (Vita-Chamber); after a save reload he comes back on his own.
- Jack's death popup ("You are being REVIVED at a Vita-Chamber") closes by itself (knob `autorevive=0` to keep it).
- Both windows show Jack's on-screen layer: his health/EVE gauges and prompts also draw on the Big Daddy view, and
  his death fade blacks out both windows for ~5 s. The game draws that layer once per frame for the one player;
  splitting it is unsolved.
- Audio diaries, pickups and loot the Big Daddy collects go to Jack.

## Live tuning (dev)
`.\tools\live.ps1` waits for `run\go.flag` (line 1: `build` or `nobuild`), builds and launches. While the game
runs, `run\knobs.txt` (`name=value` lines) is re-read twice a second: `bdcam` (camera distance, 330),
`bdcamcharge` (245), `bdhead` (95), `bdcamz` (25), `bdfollow` (0.35), `bdwalk` (3.0), `bdrun` (5.5), `bdfov` (Big Daddy FOV), `revivedelay` (1000 ms),
`bdmouse` (40), `bdpadlook` (300); `quit=1` closes the game. `run\shot.req` (first line = name) saves every game
window to `run\<name>_1.png`, `_2.png`. Build output: `logs\build_last.log`. `run\cmd.txt` runs dev commands once per change (peek/poke/bdclr/bdset/ev/jackui/dumpobj/con/block, see give.cpp).
