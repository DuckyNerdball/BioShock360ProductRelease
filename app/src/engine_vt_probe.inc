// bioshock-recomp: generated probe wrappers (engine + viewport vtables). Addresses only.
// X(addr, slot, engine_idx, viewport_idx); -1 = not in that table.
// 2026-11-03: trimmed to engine slot 57 (per-frame Tick: drains our command queue + P2 tick) and the
// viewport-only entries (capture the console viewport). The rest were discovery counters on hot shared
// virtuals (1.7M+ calls / 10 s) and cost a thread-id check + atomic add each.
#define BS_VT_LIST(X) \
  X(822BDB58, 24, 57, -1) \
  X(82205DF0, 49, -1, 1) \
  X(82205AE8, 50, -1, 2) \
  X(82447D98, 51, -1, 5) \
  X(82205EA8, 52, -1, 6) \
  X(822056D8, 53, -1, 16) \
  X(822050F0, 54, -1, 17) \
  X(82205490, 55, -1, 62) \
  X(82204AB8, 56, -1, 63) \
  X(82204B98, 57, -1, 64) \
  X(82204BE0, 58, -1, 65) \
  X(822053F8, 59, -1, 66) \
  X(826207C0, 60, -1, 67) \
