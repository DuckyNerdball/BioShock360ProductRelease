// bioshock-recomp: Testing/cheat backend, stage 1.
// - Wraps UGameEngine::Exec (guest sub_822B9F60) to log commands and capture the engine `this`
//   and the output device the game passes.
// - Wraps the D3D swap (guest sub_8217EAF0); on the thread that runs Exec it drains a queue of
//   Unreal console commands and runs them through the engine's own Exec.
// - Backtick console:  ue god   |  ue summon ShockAIClasses.SpawnedMeleeThug
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/system/xthread.h>

#include <atomic>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>
#include <chrono>

#include "engine_vt_probe.inc"

extern "C" void __imp__sub_822B9F60(PPCContext& __restrict ctx, uint8_t* base);  // UGameEngine::Exec
extern "C" void __imp__sub_8217EAF0(PPCContext& __restrict ctx, uint8_t* base);  // D3D swap
extern "C" void __imp__sub_82205F20(PPCContext& __restrict ctx, uint8_t* base);  // UViewport/UPlayer::Exec (SHOT, SHOWALL, ...)
constexpr uint32_t kViewportExecVtbl = 0x82025C30;  // vtable whose slot 0 is sub_82205F20

namespace {
std::mutex g_mu;
std::deque<std::string> g_queue;
std::deque<std::string> g_results;
std::atomic<uint32_t> g_engine{0}, g_ar{0}, g_exec_tid{0};
std::atomic<uint32_t> g_vp_obj{0};
std::atomic<uint32_t> g_vp_exec_this{0};  // captured from the game's own viewport Exec calls  // viewport object base (from viewport-only vtable probes)

inline uint32_t LoadBE32(uint8_t* base, uint32_t a) {
  return uint32_t(base[a]) << 24 | uint32_t(base[a + 1]) << 16 | uint32_t(base[a + 2]) << 8 | base[a + 3];
}
// The Exec(FExec) sub-object of the viewport: the word inside the object equal to its vtable.
uint32_t FindViewportExecThis(uint8_t* base) {
  if (uint32_t t = g_vp_exec_this.load()) return t;
  uint32_t obj = g_vp_obj.load();
  if (!obj) return 0;
  for (uint32_t off = 0; off < 0x400; off += 4)
    if (LoadBE32(base, obj + off) == kViewportExecVtbl) return obj + off;
  return 0;
}

std::string ReadGuestW(uint8_t* base, uint32_t addr) {
  std::string s;
  if (!addr) return s;
  for (int i = 0; i < 256; ++i) {
    uint16_t c = uint16_t(base[addr + 2 * i] << 8 | base[addr + 2 * i + 1]);
    if (!c) break;
    s += c < 128 ? char(c) : '?';
  }
  return s;
}

}  // namespace
void BsRunGives(PPCContext& ctx, uint8_t* base);
// Fake FOutputDevice so command output (e.g. "get") can be read. Every vtable slot points at a
// guest function we already wrap (sub_824CA220, give.cpp); that wrapper forwards here when
// r3 is the fake device.
constexpr uint32_t kCapFn = 0x824CA220;
std::atomic<uint32_t> g_capdev{0};
std::string g_capture, g_cap_last;
uint32_t BsCapDev() { return g_capdev.load(); }
std::atomic<uint32_t> g_cap_calls{0};
void CapText(uint8_t* base, uint32_t v) {
  bool ok = (v >= 0x30000000 && v < 0x5F000000) || (v >= 0x70000000 && v < 0x80000000) ||
            (v >= 0x82000000 && v < 0x83000000);
  if (!ok) return;
  std::string line;
  for (int i = 0; i < 400; ++i) {
    uint16_t c = uint16_t(base[v + 2 * i] << 8 | base[v + 2 * i + 1]);
    if (!c) break;
    line += c < 128 ? char(c) : '?';
  }
  if (line.empty() || line == g_cap_last || g_capture.size() >= 4000) return;
  g_cap_last = line;
  if (!g_capture.empty()) g_capture += " | ";
  g_capture += line;
}
void BsCapture(PPCContext& ctx, uint8_t* base) {
  g_cap_calls.fetch_add(1);
  CapText(base, ctx.r4.u32);
  // Slot 4 is "should log?" (checked before Serialize); answer yes for every slot.
  ctx.r3.u64 = 1;
}
extern "C" void __imp__sub_82160480(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_8244A820(PPCContext& __restrict ctx, uint8_t* base);
// While one of our commands runs, also grab any text the game logs to ANY output device.
extern "C" REX_FUNC(sub_82160480) {
  if (g_capdev.load()) { g_cap_calls.fetch_add(1); CapText(base, ctx.r6.u32); }
  __imp__sub_82160480(ctx, base);
}
extern "C" REX_FUNC(sub_8244A820) {
  uint32_t buf = ctx.r3.u32;
  __imp__sub_8244A820(ctx, base);
  if (g_capdev.load()) { g_cap_calls.fetch_add(1); CapText(base, buf); }
}
namespace {
void RunQueued(PPCContext& ctx, uint8_t* base) {
  BsRunGives(ctx, base);
  uint32_t eng = g_engine.load(), ar = g_ar.load();
  if (!eng || !ar) return;
  std::string cmd;
  {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_queue.empty()) return;
    cmd = g_queue.front();
    g_queue.pop_front();
  }
  PPCContext saved = ctx;
  uint32_t sp = (ctx.r1.u32 - 0x4000) & ~0xFu;  // fresh frame well below the live stack
  uint32_t str = sp + 0x2000;                   // command string between new sp and old sp
  for (size_t i = 0; i < cmd.size() && i < 0x7FF; ++i) {
    base[str + 2 * i] = 0;
    base[str + 2 * i + 1] = uint8_t(cmd[i]);
  }
  base[str + 2 * cmd.size()] = 0;
  base[str + 2 * cmd.size() + 1] = 0;
  uint32_t vp_this = FindViewportExecThis(base);
  // Fake output device at sp+0x3000, vtable (64 slots, all kCapFn) at sp+0x3100.
  uint32_t dev = sp + 0x3000, vt = sp + 0x3100;
  std::memset(base + dev, 0, 0x100);
  auto put = [&](uint32_t a, uint32_t v) {
    base[a] = uint8_t(v >> 24); base[a + 1] = uint8_t(v >> 16); base[a + 2] = uint8_t(v >> 8); base[a + 3] = uint8_t(v);
  };
  put(dev, vt);
  for (uint32_t i = 0; i < 64; ++i) put(vt + 4 * i, kCapFn);
  g_capture.clear();
  g_cap_last.clear();
  g_cap_calls = 0;
  g_capdev = dev;
  ctx.r1.u64 = sp;
  ctx.r4.u64 = str;
  ctx.r5.u64 = dev;
  const char* via;
  if (vp_this) {
    ctx.r3.u64 = vp_this;
    __imp__sub_82205F20(ctx, base);
    via = "player";
  } else {
    ctx.r3.u64 = eng;
    __imp__sub_822B9F60(ctx, base);
    via = "engine";
  }
  uint32_t handled = ctx.r3.u32;
  g_capdev = 0;
  ctx = saved;
  REXLOG_INFO("bioshock-recomp: output '{}' ({} capture calls): {}", cmd, g_cap_calls.load(), g_capture);
  REXLOG_INFO("bioshock-recomp: ran '{}' via {} (vp obj {:08X} exec-this {:08X})", cmd, via,
              g_vp_obj.load(), vp_this);
  {
    std::lock_guard<std::mutex> lk(g_mu);
    g_results.push_back(fmt::format("{} -> {} ({})", cmd, handled ? "handled" : "not handled", vp_this ? "player" : "engine"));
    // Show captured output only for short replies (summon dumps dozens of object names).
    if (!g_capture.empty() && g_capture.size() < 160 && cmd.rfind("summon", 0) != 0) g_results.push_back("   = " + g_capture);
    if (g_results.size() > 12) g_results.pop_front();
  }
  REXLOG_INFO("bioshock-recomp: ue '{}' -> Exec returned {}", cmd, handled);
}
}  // namespace

extern "C" REX_FUNC(sub_822B9F60) {
  uint32_t tid = rex::system::XThread::GetCurrentThreadId();
  static std::unordered_set<std::string> s_seen;
  std::string cmd = ReadGuestW(base, ctx.r4.u32);
  if (!g_engine.load()) {
    REXLOG_INFO("bioshock-recomp: captured engine Exec this={:08X} ar={:08X} tid={}", ctx.r3.u32,
                ctx.r5.u32, tid);
  }
  g_engine = ctx.r3.u32;
  if (ctx.r5.u32 != g_capdev.load()) g_ar = ctx.r5.u32;
  g_exec_tid = tid;
  if (s_seen.size() < 200 && s_seen.insert(cmd).second) {
    REXLOG_INFO("bioshock-recomp: game Exec '{}' tid={} ar={:08X} (ours {:08X})", cmd, tid, ctx.r5.u32, g_capdev.load());
  }
  __imp__sub_822B9F60(ctx, base);
}

// Probe every engine-vtable method: count calls on the game thread with this == engine, and
// drain the command queue from the first such call (the engine's Tick runs every frame).
namespace {
std::atomic<uint32_t> g_vt_calls[128];
std::atomic<uint32_t> g_vt_this[128];
void ProbeHit(int slot, int eidx, int vidx, PPCContext& ctx, uint8_t* base) {
  if (rex::system::XThread::GetCurrentThreadId() != g_exec_tid.load()) return;
  g_vt_calls[slot].fetch_add(1, std::memory_order_relaxed);
  g_vt_this[slot].store(ctx.r3.u32, std::memory_order_relaxed);
  if (eidx < 0 && vidx >= 0) g_vp_obj.store(ctx.r3.u32, std::memory_order_relaxed);
  // Engine vtable slot 57 runs once per frame with this == engine object: treat as Tick.
  if (eidx == 57) RunQueued(ctx, base);
}
}  // namespace
#define BS_VT_WRAP(addr, slot, eidx, vidx)                                       \
  extern "C" void __imp__sub_##addr(PPCContext& __restrict ctx, uint8_t* base); \
  extern "C" REX_FUNC(sub_##addr) {                                             \
    ProbeHit(slot, eidx, vidx, ctx, base);                                      \
    __imp__sub_##addr(ctx, base);                                               \
  }
BS_VT_LIST(BS_VT_WRAP)

extern "C" REX_FUNC(sub_82205F20) {
  static int s_n = 0;
  if (s_n < 50) {
    ++s_n;
    REXLOG_INFO("bioshock-recomp: game viewport Exec '{}' this={:08X}", ReadGuestW(base, ctx.r4.u32),
                ctx.r3.u32);
  }
  g_vp_exec_this = ctx.r3.u32;
  __imp__sub_82205F20(ctx, base);
}

extern "C" REX_FUNC(sub_8217EAF0) {
  uint32_t tid = rex::system::XThread::GetCurrentThreadId();
  static uint32_t s_logged_tid = 0;
  if (s_logged_tid != tid) {
    s_logged_tid = tid;
    REXLOG_INFO("bioshock-recomp: swap thread tid={} (exec tid={})", tid, g_exec_tid.load());
  }
  static auto s_last = std::chrono::steady_clock::now();
  auto now = std::chrono::steady_clock::now();
  if (now - s_last > std::chrono::seconds(10)) {
    s_last = now;
    std::string rep;
    for (int i = 0; i < 128; ++i)
      if (uint32_t n = g_vt_calls[i].exchange(0)) rep += fmt::format(" [{}]={}@{:08X}", i, n, g_vt_this[i].load());
    if (!rep.empty()) REXLOG_INFO("bioshock-recomp: vt calls/10s:{} | vp obj {:08X} exec-this {:08X}", rep, g_vp_obj.load(), FindViewportExecThis(base));
  }
  __imp__sub_8217EAF0(ctx, base);
}

REXCVAR_DEFINE_COMMAND_ARGS(
    ue,
    [](std::string_view args) {
      while (!args.empty() && args.front() == ' ') args.remove_prefix(1);
      if (args.empty()) {
        REXLOG_INFO("ue: usage: ue <unreal console command>");
        return;
      }
      std::lock_guard<std::mutex> lk(g_mu);
      g_queue.emplace_back(args);
      REXLOG_INFO("ue: queued '{}' (engine {:08X}, exec tid {})", std::string(args), g_engine.load(),
                  g_exec_tid.load());
    },
    "BioShock", "Queue an Unreal console command for the game thread");

// Used by the F9 Testing overlay (testing_overlay.h).
void BsQueueGive(std::string cls);
void BsQueueCommand(std::string cmd) {
  // "get" never reaches StaticExec through the console on this build: do it natively (give.cpp).
  if (cmd.size() > 4 && (cmd[0] == 'g' || cmd[0] == 'G') && (cmd[1] == 'e' || cmd[1] == 'E') &&
      (cmd[2] == 't' || cmd[2] == 'T') && cmd[3] == ' ') {
    BsQueueGive("get:" + cmd.substr(4));
    return;
  }
  if (cmd.rfind("props ", 0) == 0) { BsQueueGive("props:" + cmd.substr(6)); return; }
  if (cmd.rfind("p2walk ", 0) == 0) { void BsSetP2Walk(std::string); BsSetP2Walk(cmd.substr(7)); return; }
  std::lock_guard<std::mutex> lk(g_mu);
  g_queue.push_back(std::move(cmd));
}
std::vector<std::string> BsRecentResults() {
  std::lock_guard<std::mutex> lk(g_mu);
  return {g_results.begin(), g_results.end()};
}
bool BsEngineReady() { return g_engine.load() != 0; }

// ---------------------------------------------------------------------------------------------
// bsnames <text>: scan readable guest memory for ASCII names containing <text> (case-insensitive)
// and log each distinct one. Used to discover the 360 build's exec/cheat function names.
#include <rex/runtime.h>
#include <rex/system/xmemory.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <set>

namespace {
std::vector<std::string> g_name_hits;
void ScanNames(std::string needle) {
  for (auto& c : needle) c = char(std::tolower(uint8_t(c)));
  auto* rt = rex::Runtime::instance();
  if (!rt || needle.empty()) return;
  uint8_t* lo = rt->memory()->TranslateVirtual(0x00010000);
  uint8_t* hi = rt->memory()->TranslateVirtual(0xFFFF0000);
  std::set<std::string> hits;
  for (uint8_t* p = lo; p < hi && hits.size() < 300;) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) break;
    uint8_t* end = std::min(hi, (uint8_t*)mbi.BaseAddress + mbi.RegionSize);
    bool readable = mbi.State == MEM_COMMIT &&
                    (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ |
                                    PAGE_EXECUTE_READWRITE)) && !(mbi.Protect & PAGE_GUARD);
    if (readable) {
      for (uint8_t* q = p; q + needle.size() < end; ++q) {
        size_t i = 0;
        while (i < needle.size() && std::tolower(q[i]) == uint8_t(needle[i])) ++i;
        if (i != needle.size()) continue;
        uint8_t* s = q;
        while (s > p && std::isalnum(s[-1]) | (s[-1] == '_')) --s;
        uint8_t* e = q + needle.size();
        while (e < end && (std::isalnum(*e) || *e == '_')) ++e;
        if (e - s >= 4 && e - s < 64 && (e >= end || *e == 0)) hits.emplace((char*)s, e - s);
        q = e;
      }
      // UTF-16BE pass: 0x00,'c' pairs
      for (uint8_t* q = p; q + 2 * needle.size() < end; q += 2) {
        size_t i = 0;
        while (i < needle.size() && q[2 * i] == 0 && std::tolower(q[2 * i + 1]) == uint8_t(needle[i])) ++i;
        if (i != needle.size()) continue;
        uint8_t* s = q;
        while (s - 2 >= p && s[-2] == 0 && (std::isalnum(s[-1]) || s[-1] == '_')) s -= 2;
        uint8_t* e = q + 2 * needle.size();
        while (e + 1 < end && e[0] == 0 && (std::isalnum(e[1]) || e[1] == '_')) e += 2;
        if ((e - s) / 2 >= 4 && (e - s) / 2 < 64) {
          std::string w;
          for (uint8_t* c = s; c < e; c += 2) w += char(c[1]);
          hits.insert(w + "(w)");
        }
        q = e - 2;
      }
    }
    p = end;
  }
  std::string all;
  for (auto& h : hits) all += h + " ";
  REXLOG_INFO("bsnames '{}': {} hits: {}", needle, hits.size(), all);
  std::lock_guard<std::mutex> lk(g_mu);
  g_name_hits.assign(hits.begin(), hits.end());
}
}  // namespace

REXCVAR_DEFINE_COMMAND_ARGS(
    bsnames, [](std::string_view a) { ScanNames(std::string(a)); }, "BioShock",
    "Scan guest memory for names containing text");

void BsScanNames(std::string needle) { ScanNames(std::move(needle)); }
std::vector<std::string> BsNameHits() {
  std::lock_guard<std::mutex> lk(g_mu);
  return g_name_hits;
}


// ---------------------------------------------------------------------------------------------
// Controller polling for the F9 panel (host XInput, independent of the game's input path, so it
// still works while game input is blocked). Returns false if no pad is connected.
#include <xinput.h>
#pragma comment(lib, "xinput.lib")
bool BsPadPoll(uint16_t* buttons, int16_t* lx, int16_t* ly) {
  for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
    XINPUT_STATE st{};
    if (XInputGetState(i, &st) == ERROR_SUCCESS) {
      *buttons = st.Gamepad.wButtons;
      *lx = st.Gamepad.sThumbLX;
      *ly = st.Gamepad.sThumbLY;
      return true;
    }
  }
  return false;
}

uint8_t g_pad_lt[4] = {};
uint8_t BsPadLT(int i) { return g_pad_lt[i & 3]; }
bool BsPadPollIndex(int i, uint16_t* buttons, int16_t* lx, int16_t* ly, int16_t* rx, int16_t* ry, uint8_t* rt) {
  XINPUT_STATE st{};
  if (XInputGetState(DWORD(i), &st) != ERROR_SUCCESS) return false;
  g_pad_lt[i & 3] = st.Gamepad.bLeftTrigger;
  *buttons = st.Gamepad.wButtons; *lx = st.Gamepad.sThumbLX; *ly = st.Gamepad.sThumbLY;
  *rx = st.Gamepad.sThumbRX; *ry = st.Gamepad.sThumbRY; *rt = st.Gamepad.bRightTrigger;
  return true;
}

// Keyboard/mouse for the P2 puppet (polled directly, works while the game's input is blocked).
bool BsKeyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
// Horizontal mouse movement since the last call; re-centres the cursor while our window is focused.
int BsMouseDX() {
  HWND fg = GetForegroundWindow();
  DWORD pid = 0;
  GetWindowThreadProcessId(fg, &pid);
  if (!fg || pid != GetCurrentProcessId()) return 0;
  RECT r; GetClientRect(fg, &r);
  POINT c{(r.right - r.left) / 2, (r.bottom - r.top) / 2};
  ClientToScreen(fg, &c);
  POINT p; GetCursorPos(&p);
  SetCursorPos(c.x, c.y);
  extern int g_mouse_dy_bs; g_mouse_dy_bs = p.y - c.y;
  return p.x - c.x;
}
int g_mouse_dy_bs = 0;
int BsMouseDY() { return g_mouse_dy_bs; }
