// bioshock-recomp: native "give item" for the F9 Testing overlay.
// Addresses found by tracing the native-function table (intU...exec... names) in the 360 image:
//   sub_82466660  StaticLoadObject(UClass* Cls, UObject* Outer, const TCHAR* Name, const TCHAR* File, DWORD Flags, void* Sandbox)
//   sub_824C6D68  AShockGameInfo::GetItemFromClass(UClass* ItemClass) -> item           (from execGetItemFromClass)
//   sub_824BE0D8  UInventoryManager::AddStackToInventory(item) -> bool                    (from execAddStackToInventory)
// GameInfo and the player's InventoryManager are captured the first time the game itself calls
// those functions (pick up any item once after loading).
#include <rex/logging.h>
#include <rex/graphics/split_view.h>
#include <functional>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/system/xthread.h>

#include <atomic>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

extern "C" void __imp__sub_82466660(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_824C6D68(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_824BE0D8(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_824697D8(PPCContext& __restrict ctx, uint8_t* base);  // FName::FName(out, const TCHAR*, find=1, 1)
extern "C" void __imp__sub_824DA720(PPCContext& __restrict ctx, uint8_t* base);  // AShockPlayer::AddAvailablePlasmid(FName*)
extern "C" void __imp__sub_825473F8(PPCContext& __restrict ctx, uint8_t* base);  // PlasmidManager lookup(FName*) -> plasmid CDO (no null check!)
extern "C" void __imp__sub_82547268(PPCContext& __restrict ctx, uint8_t* base);  // PlasmidManager find/load class(FName*) -> UClass* or 0
extern "C" void __imp__sub_82546F18(PPCContext& __restrict ctx, uint8_t* base);  // PlasmidManager already owned(FName*) -> bool
extern "C" void __imp__sub_82549170(PPCContext& __restrict ctx, uint8_t* base);  // PlasmidManager prerequisites met(FName*) -> bool
extern "C" void __imp__sub_82548290(PPCContext& __restrict ctx, uint8_t* base);  // PlasmidManager add(FName*, slot=-1)
// AShockPlayer exec natives the HUD calls constantly: used only to capture the player pointer.
extern "C" void __imp__sub_82462F60(PPCContext& __restrict ctx, uint8_t* base);  // StaticFindObject(Class|0, Outer, Name, Exact)
extern "C" void __imp__sub_821C5028(PPCContext& __restrict ctx, uint8_t* base);  // FindObject(ANY_PACKAGE=-1, Name, 0) -> UClass*
extern "C" void __imp__sub_821CE930(PPCContext& __restrict ctx, uint8_t* base);  // AActor::SetCollisionSize(f1 radius, f2 height)
extern "C" void __imp__sub_822A0870(PPCContext& __restrict ctx, uint8_t* base);  // FindField(UClass*, PropName) -> UProperty*
extern "C" void __imp__sub_821640E8(PPCContext& __restrict ctx, uint8_t* base);  // FString::FString(const TCHAR*)
extern "C" void __imp__sub_821B07A0(PPCContext& __restrict ctx, uint8_t* base);  // TFieldIterator<UProperty>::IterateToNext
extern "C" void __imp__sub_8246ECC8(PPCContext& __restrict ctx, uint8_t* base);  // FName -> FString (out, FName*)
extern "C" void __imp__sub_824CF4A8(PPCContext& __restrict ctx, uint8_t* base);  // AShockPlayer::AddBioAmmo(float f1)
extern "C" void __imp__sub_824CA238(PPCContext& __restrict ctx, uint8_t* base);  // execGetBioAmmo
extern "C" void __imp__sub_824CA220(PPCContext& __restrict ctx, uint8_t* base);  // execGetMaxBioAmmo
extern "C" void __imp__sub_824C8310(PPCContext& __restrict ctx, uint8_t* base);  // execUpdateUIAmmoTotals

void BsQueueCommand(std::string cmd);  // cheats.cpp
void BsSetVerbose(bool v);
int g_prop_off_field = -1;  // UProperty::Offset field position, calibrated by props

namespace {
std::atomic<uint32_t> g_gameinfo{0}, g_invmgr{0}, g_any_class{0}, g_uclass{0}, g_player{0};
std::mutex g_gmu;
std::deque<std::string> g_give_queue;
std::deque<std::string> g_give_log;
// "spawnany:A|B|C": look each class up natively; summon the first that exists (full Package.Class
// comes from exporting its Class property). Names still waiting for a lookup:
std::vector<std::string> g_spawn_wait;
bool g_vis_found = false;      // a placed Gene Bank's look was copied
std::string g_vis_from;        // which instance we copy from (first one with a model)
bool g_spawn_done = false;

inline uint32_t BE32(uint8_t* b, uint32_t a) {
  return uint32_t(b[a]) << 24 | uint32_t(b[a + 1]) << 16 | uint32_t(b[a + 2]) << 8 | b[a + 3];
}
// ---- Call-stack probe (recon for the script tracer). The recompiled code keeps guest return
// addresses: every call sets ctx.lr and each prologue saves it at (caller sp - 8), with the back
// chain at 0(sp). So from any hook we can walk the guest stack. Each site logs its first few hits
// as "probe <site>: <ret0> <ret1> ...", mapped offline to functions (find ProcessEvent / CallFunction).
std::atomic<bool> g_probe{true};
std::string GuestBacktrace(uint8_t* base, const PPCContext& ctx, int max_frames = 14) {
  auto code = [](uint32_t a) { return a >= 0x82000000 && a < 0x83000000; };
  std::string out = fmt::format("{:08X}", uint32_t(ctx.lr));
  uint32_t sp = ctx.r1.u32;
  for (int k = 0; k < max_frames; ++k) {
    uint32_t prev = BE32(base, sp);
    if (prev <= sp || prev - sp > 0x40000) break;
    uint32_t ret = BE32(base, prev - 8);
    if (!code(ret)) break;
    out += fmt::format(" {:08X}", ret);
    sp = prev;
  }
  return out;
}
void Probe(const char* site, uint8_t* base, const PPCContext& ctx, const std::string& extra = "", int limit = 4) {
  if (!g_probe.load()) return;
  static std::mutex mu; static std::unordered_map<std::string, int> counts;
  {
    std::lock_guard<std::mutex> lk(mu);
    if (++counts[site] > limit) return;
  }
  REXLOG_INFO("bioshock-recomp: probe {}: this {:08X}{} | stack {}", site, ctx.r3.u32, extra, GuestBacktrace(base, ctx));
}

std::atomic<bool> g_verbose{false};
bool IsDiag(const std::string& s) {
  static const char* kDiag[] = {"cam:", "P2 move:", "P2: weapon", "P2: drill", "P2: hit ", "P2: swing",
                                "P2: damage type", "UClass meta", "captured ", "derived ", "load ->", ": start (uclass",
                                "Gene Bank look", "is an EcologyFighter", "is not an EcologyFighter"};
  for (const char* k : kDiag) if (s.find(k) != std::string::npos) return true;
  return false;
}
void Log(std::string s) {
  REXLOG_INFO("bioshock-recomp: give: {}", s);
  if (!g_verbose.load() && IsDiag(s)) return;
  std::lock_guard<std::mutex> lk(g_gmu);
  g_give_log.push_back(std::move(s));
  if (g_give_log.size() > 10) g_give_log.pop_front();
}
// UClass::StaticClass(): a UClass object whose Class field points to itself. Find the Class
// field offset by looking for the self-reference, starting from any known UClass*.
bool SafeAddr(uint32_t a) { return (a >= 0x30000000 && a < 0x5F000000) || (a >= 0x82000000 && a < 0x83000000); }
// UObject::Class is at +0x2C: obj -> its class -> metaclass, which points to itself.
uint32_t MetaFrom(uint8_t* base, uint32_t obj) {
  if (!SafeAddr(obj)) return 0;
  uint32_t c = BE32(base, obj + 0x2C);
  if (!SafeAddr(c)) return 0;
  uint32_t m = BE32(base, c + 0x2C);
  if (!SafeAddr(m)) return 0;
  return BE32(base, m + 0x2C) == m ? m : 0;
}
uint32_t FindUClass(uint8_t* base) {
  if (uint32_t u = g_uclass.load()) return u;
  for (uint32_t o : {g_player.load(), g_invmgr.load(), g_gameinfo.load()}) {
    if (uint32_t m = MetaFrom(base, o)) {
      g_uclass = m;
      REXLOG_INFO("bioshock-recomp: give: UClass meta {:08X} (from object {:08X})", m, o);
      return m;
    }
  }
  uint32_t c = g_any_class.load();
  if (!c) return 0;
  for (uint32_t off = 4; off < 0x40; off += 4) {
    uint32_t meta = BE32(base, c + off);
    bool heap = meta >= 0x40000000 && meta < 0x5F000000;
    bool img = meta >= 0x82000000 && meta < 0x83000000;
    if (!heap && !img) continue;
    if (BE32(base, meta + off) == meta) {
      g_uclass = meta;
      REXLOG_INFO("bioshock-recomp: give: UClass meta {:08X} (Class field +{:X})", meta, off);
      return meta;
    }
  }
  return 0;
}
}  // namespace
void BsSetProbe(bool on) { g_probe = on; }  // outside the anonymous namespace: called from the panel

namespace {
bool InHeap(uint32_t a) { return a >= 0x40000000 && a < 0x5F000000; }
void CapturePlayer(uint32_t p) {
  if (!InHeap(p) || g_player.load() == p) return;
  g_player = p;
  // New player = level/save load: everything cached from the old level is freed memory now.
  g_invmgr = 0;
  g_gameinfo = 0;
  REXLOG_INFO("bioshock-recomp: give: captured ShockPlayer {:08X}", p);
}
// Fill in GameInfo / InventoryManager from the player so nothing has to be picked up first.
void DeriveFromPlayer(uint8_t* base) {
  uint32_t p = g_player.load();
  if (!p) return;
  if (!g_gameinfo.load()) {
    uint32_t level = BE32(base, p + 0xE0);
    uint32_t gi = InHeap(level) ? BE32(base, level + 0x7AC) : 0;
    if (InHeap(gi)) { g_gameinfo = gi; REXLOG_INFO("bioshock-recomp: give: derived GameInfo {:08X}", gi); }
  }
  if (uint32_t inv = g_invmgr.load(); inv && (!InHeap(inv) || BE32(base, inv + 0x38) != p)) g_invmgr = 0;  // stale
  if (!g_invmgr.load()) {
    for (uint32_t off = 0x100; off < 0x1400; off += 4) {
      uint32_t cand = BE32(base, p + off);
      if (InHeap(cand) && BE32(base, cand + 0x38) == p && BE32(base, cand) >= 0x82000000 &&
          BE32(base, cand) < 0x83000000) {
        g_invmgr = cand;
        REXLOG_INFO("bioshock-recomp: give: derived InventoryManager {:08X} (player+{:X})", cand, off);
        break;
      }
    }
  }
}
}  // namespace
extern "C" REX_FUNC(sub_824CA238) { CapturePlayer(ctx.r3.u32); __imp__sub_824CA238(ctx, base); }
uint32_t BsCapDev();
void BsCapture(PPCContext& ctx, uint8_t* base);
extern "C" REX_FUNC(sub_824CA220) {
  if (uint32_t d = BsCapDev(); d && ctx.r3.u32 == d) { BsCapture(ctx, base); return; }
  CapturePlayer(ctx.r3.u32); __imp__sub_824CA220(ctx, base);
}
extern "C" REX_FUNC(sub_824C8310) { CapturePlayer(ctx.r3.u32); __imp__sub_824C8310(ctx, base); }

extern "C" REX_FUNC(sub_824C6D68) {
  if (!g_gameinfo.load()) REXLOG_INFO("bioshock-recomp: give: captured GameInfo {:08X}", ctx.r3.u32);
  g_gameinfo = ctx.r3.u32;
  if (ctx.r4.u32) g_any_class = ctx.r4.u32;
  __imp__sub_824C6D68(ctx, base);
}

extern "C" REX_FUNC(sub_824BE0D8) {
  if (uint32_t p = g_player.load(); p && BE32(base, ctx.r3.u32 + 0x38) == p) {
    uint32_t stk = ctx.r4.u32;
    Probe("inv.AddStack", base, ctx, fmt::format(" item class {:08X} x{}", BE32(base, stk + 0x48), BE32(base, stk + 0x4C)), 6);
  }
  // Only the player's own inventory (NPCs and containers call this too).
  if (uint32_t p = g_player.load(); p && BE32(base, ctx.r3.u32 + 0x38) == p && g_invmgr.load() != ctx.r3.u32) {
    REXLOG_INFO("bioshock-recomp: give: captured InventoryManager {:08X}", ctx.r3.u32);
    g_invmgr = ctx.r3.u32;
  }
  __imp__sub_824BE0D8(ctx, base);
}

// Called on the game thread from cheats.cpp RunQueued (frame tick). Runs one queued give.
void BsTickP2(PPCContext& ctx, uint8_t* base);
void BsPollPadSpawnCombo();
void BsRunGives(PPCContext& ctx, uint8_t* base) {
  BsPollPadSpawnCombo();
  BsTickP2(ctx, base);
  std::string cls;
  {
    std::lock_guard<std::mutex> lk(g_gmu);
    if (g_give_queue.empty()) return;
    cls = g_give_queue.front();
    g_give_queue.pop_front();
  }
  // Native "get <Class> <Property>": the same steps as UObject::StaticExec's GET (sub_82464AC0),
  // which the console path never reaches on this build.
  if (cls.rfind("spawnany:", 0) == 0) {
    std::string list = cls.substr(9);
    g_spawn_wait.clear(); g_spawn_done = false;
    std::lock_guard<std::mutex> lk(g_gmu);
    for (size_t a = 0; a <= list.size();) {
      size_t b = list.find('|', a); if (b == std::string::npos) b = list.size();
      std::string n = list.substr(a, b - a);
      if (!n.empty()) { g_spawn_wait.push_back(n); g_give_queue.push_back("get:" + n + " Class"); }
      a = b + 1;
    }
    return;
  }
  if (cls == "refill") {
    // Same natives the scripts use: AShockPawn::AddHealth = vtable+912 (f1 amount, r5 flag),
    // AShockPlayer::AddBioAmmo 0x824CF4A8 (f1). Max values: pawn+1876 / player+2756 (floats).
    uint32_t p = g_player.load();
    if (!InHeap(p)) { Log("refill: player not captured yet"); return; }
    auto rf = [&](uint32_t a) { uint32_t v = BE32(base, a); float f; std::memcpy(&f, &v, 4); return f; };
    float mh = rf(p + 1876), me = rf(p + 2756);
    PPCContext saved = ctx;
    uint32_t sp = (ctx.r1.u32 - 0x4000) & ~0xFu;
    if (mh > 0 && mh < 100000) {
      ctx.r1.u64 = sp; ctx.r3.u64 = p; ctx.r5.u64 = 0; ctx.f1.f64 = mh;
      if (PPCFunc* f = rex::runtime::ResolveIndirectFunction(BE32(base, BE32(base, p) + 912))) f(ctx, base);
    }
    if (me > 0 && me < 100000) {
      ctx = saved; ctx.r1.u64 = sp; ctx.r3.u64 = p; ctx.f1.f64 = me;
      __imp__sub_824CF4A8(ctx, base);
    }
    ctx = saved;
    Log(fmt::format("refill: +{} health, +{} EVE", mh, me));
    return;
  }
  if (cls.rfind("genebank", 0) == 0) {
    // Copy the look of a Gene Bank the level placed onto the class defaults, then spawn one.
    // Spawned ones otherwise have no model (StaticMesh = None). Placed actors are found by
    // walking the level's actor list (player+0xE0 = ULevel; its Actors TArray contains the player).
    g_vis_found = false;
    g_vis_from.clear();
    // ULevel (AActor::XLevel) is one of the player's pointer fields; its Actors TArray holds the
    // player. Search: player field -> object -> TArray{data,num,max} containing the player.
    static uint32_t s_xlevel_off = 0, s_arr_off = 0;
    uint32_t player = g_player.load(), actors = 0, num = 0;
    auto tryPair = [&](uint32_t pf, uint32_t af) {
      uint32_t lv = BE32(base, player + pf);
      if (!InHeap(lv)) return false;
      uint32_t d = BE32(base, lv + af), n = BE32(base, lv + af + 4), mx = BE32(base, lv + af + 8);
      if (!InHeap(d) || n < 10 || n > 100000 || mx < n || mx > 400000) return false;
      for (uint32_t i = 0; i < n; ++i)
        if (BE32(base, d + 4 * i) == player) { actors = d; num = n; s_xlevel_off = pf; s_arr_off = af; return true; }
      return false;
    };
    if (player && !(s_xlevel_off && tryPair(s_xlevel_off, s_arr_off)))
      for (uint32_t pf = 0x30; pf < 0x400 && !actors; pf += 4)
        for (uint32_t af = 0x20; af < 0x400 && !actors; af += 4) tryPair(pf, af);
    if (actors) REXLOG_INFO("bioshock-recomp: give: actor list = player+{:X} -> +{:X}", s_xlevel_off, s_arr_off);
    // The class to match.
    PPCContext saved = ctx;
    uint32_t sp = (ctx.r1.u32 - 0x4000) & ~0xFu, s1 = sp + 0x2000;
    const char* cn = "PlasmidEquipStation";
    for (size_t i = 0; cn[i]; ++i) { base[s1 + 2 * i] = 0; base[s1 + 2 * i + 1] = uint8_t(cn[i]); }
    base[s1 + 2 * std::strlen(cn)] = base[s1 + 2 * std::strlen(cn) + 1] = 0;
    ctx.r1.u64 = sp; ctx.r3.u64 = 0xFFFFFFFFu; ctx.r4.u64 = s1; ctx.r5.u64 = 0;
    __imp__sub_821C5028(ctx, base);
    uint32_t sc = ctx.r3.u32;
    ctx = saved;
    std::vector<uint32_t> banks;
    for (uint32_t i = 0; actors && i < num; ++i) {
      uint32_t a = BE32(base, actors + 4 * i);
      if (!InHeap(a)) continue;
      // class or any superclass == PlasmidEquipStation (UStruct SuperField at +0x40 is a guess; also exact match)
      uint32_t c = BE32(base, a + 0x2C);
      // UField: SuperField +0x38, Next +0x3C (UStruct Children +0x44), so walk +0x38.
      for (int k = 0; k < 12 && c; ++k) { if (c == sc) { banks.push_back(a); break; } c = InHeap(c) || (c >= 0x82000000 && c < 0x83000000) ? BE32(base, c + 0x38) : 0; }
    }
    Log(fmt::format("Gene Bank: level actors {} at {:08X}, placed Gene Banks found: {}", num, actors, banks.size()));
    std::lock_guard<std::mutex> lk(g_gmu);
    static const char* kVis[] = {"StaticMesh", "Mesh", "DrawType", "Skins", "DrawScale", "DrawScale3D", "PrePivot"};
    for (uint32_t b : banks)
      for (const char* pr : kVis) g_give_queue.push_back(fmt::format("vget:@{:08X} {}", b, pr));
    g_give_queue.push_back("visdone");
    g_give_queue.push_back("spawnany:PlasmidEquipStation");
    return;
  }
  if (cls.rfind("pokeb:", 0) == 0) {  // pokeb:ADDR VALUE  (byte write, for enum probing)
    uint32_t a = 0, v = 0;
    if (std::sscanf(cls.c_str() + 6, "%x %u", &a, &v) == 2 && (InHeap(a))) base[a] = uint8_t(v);
    return;
  }
  if (cls == "visdone") {
    if (!g_vis_found) {
      // No Gene Bank in this level: load the model by name (package MA_Plasmid_equip, found 2026-11-01).
      Log("no placed Gene Bank in this level: loading MA_Plasmid_equip.plasmid_equip_machine");
      BsQueueCommand("set PlasmidEquipStation DrawType DT_StaticMesh");
      BsQueueCommand("set PlasmidEquipStation StaticMesh StaticMesh'MA_Plasmid_equip.plasmid_equip_machine'");
    }
    return;
  }
  bool quiet = cls.rfind("vget:", 0) == 0;
  if (quiet) cls = "get:" + cls.substr(5);
  if (cls.rfind("get:", 0) == 0 || cls.rfind("props:", 0) == 0) {
    bool list = cls[0] == 'p';
    std::string rest = cls.substr(list ? 6 : 4), cname = rest, pname;
    if (auto spc = rest.find(' '); spc != std::string::npos) { cname = rest.substr(0, spc); pname = rest.substr(spc + 1); }
    if (!list && pname.empty()) { Log("usage: get <Class> <Property>   |   props <Class> [filter]"); return; }
    if (auto d = cname.rfind('.'); d != std::string::npos) cname = cname.substr(d + 1);
    PPCContext saved = ctx;
    uint32_t sp = (ctx.r1.u32 - 0x4000) & ~0xFu, s1 = sp + 0x2000, s2 = sp + 0x2800, dev = sp + 0x1000,
             it = sp + 0x1100, nm = sp + 0x1120;
    auto putw = [&](uint32_t a, const std::string& t) {
      for (size_t i = 0; i < t.size(); ++i) { base[a + 2 * i] = 0; base[a + 2 * i + 1] = uint8_t(t[i]); }
      base[a + 2 * t.size()] = base[a + 2 * t.size() + 1] = 0;
    };
    auto put = [&](uint32_t a, uint32_t v) {
      base[a] = uint8_t(v >> 24); base[a + 1] = uint8_t(v >> 16); base[a + 2] = uint8_t(v >> 8); base[a + 3] = uint8_t(v);
    };
    auto readfs = [&](uint32_t fs) {  // FString {TCHAR* data, int num}
      std::string o;
      uint32_t data = BE32(base, fs), num = BE32(base, fs + 4);
      for (uint32_t i = 0; data && i < num && i < 600; ++i) {
        uint16_t c = uint16_t(base[data + 2 * i] << 8 | base[data + 2 * i + 1]);
        if (!c) break;
        o += c < 128 ? char(c) : '?';
      }
      return o;
    };
    auto exportText = [&](uint32_t prop, uint32_t defs) {
      std::memset(base + dev, 0, 0x20);
      put(dev, 0x82025BB4);  // FStringOutputDevice vtable; FString at +4
      ctx.r1.u64 = sp; ctx.r3.u64 = dev + 4; ctx.r4.u64 = 0x8211C970;  // empty TCHAR string
      __imp__sub_821640E8(ctx, base);
      uint32_t fn = BE32(base, BE32(base, prop) + 280);  // UProperty::ExportText
      ctx.r1.u64 = sp; ctx.r3.u64 = prop; ctx.r4.u64 = 0; ctx.r5.u64 = dev; ctx.r6.u64 = defs;
      ctx.r7.u64 = defs; ctx.r8.u64 = 1; ctx.r9.u64 = 0;
      if (PPCFunc* f = rex::runtime::ResolveIndirectFunction(fn)) f(ctx, base);
      std::string o = readfs(dev + 4);
      return o.empty() ? std::string("(empty)") : o;  // FString buffer leaked (debug only)
    };
    auto fieldName = [&](uint32_t field) {
      std::memset(base + nm, 0, 0x10);
      ctx.r1.u64 = sp; ctx.r3.u64 = nm; ctx.r4.u64 = field + 0x24;
      __imp__sub_8246ECC8(ctx, base);
      return readfs(nm);
    };
    putw(s1, cname);
    ctx.r1.u64 = sp; ctx.r3.u64 = 0xFFFFFFFFu; ctx.r4.u64 = s1; ctx.r5.u64 = 0;
    if (cname[0] == '@') ctx.r3.u64 = std::strtoul(cname.c_str() + 1, nullptr, 16);  // object by address
    else __imp__sub_821C5028(ctx, base);  // FindObject<UClass>: classes only
    if (!ctx.r3.u32 && cname[0] != '@') {
      // Not a class: any object by name (live actors like PlasmidEquipStation0).
      ctx.r1.u64 = sp; ctx.r3.u64 = 0; ctx.r4.u64 = 0xFFFFFFFFu; ctx.r5.u64 = s1; ctx.r6.u64 = 0;
      __imp__sub_82462F60(ctx, base);
    }
    uint32_t klass = ctx.r3.u32, defs = klass ? BE32(base, klass + 0x500) : 0;
    bool hasdefs = klass && BE32(base, klass + 0x504) && InHeap(defs);
    // Not a class but a live object (e.g. a placed actor "PlasmidEquipStation0"): read the object
    // itself, using its class for the property list.
    if (uint32_t meta = FindUClass(base); klass && meta && BE32(base, klass + 0x2C) != meta) {
      uint32_t oc = BE32(base, klass + 0x2C);
      if (InHeap(oc) || (oc >= 0x82000000 && oc < 0x83000000)) { defs = klass; klass = oc; hasdefs = InHeap(defs); }
    }
    if (!klass) {
      ctx = saved;
      if (quiet) return;
      Log(fmt::format("{} {}: class not found", list ? "props" : "get", cname));
      if (auto it = std::find(g_spawn_wait.begin(), g_spawn_wait.end(), cname); it != g_spawn_wait.end()) {
        g_spawn_wait.erase(it);
        if (g_spawn_wait.empty() && !g_spawn_done) Log("none of the candidate classes are loaded here (try a level that has one)");
      }
      return;
    }
    if (list) {
      // Walk every property (incl. inherited) the way FindField does: it = {struct, field}.
      put(it, klass); put(it + 4, BE32(base, klass + 0x44));
      ctx.r1.u64 = sp; ctx.r3.u64 = it; __imp__sub_821B07A0(ctx, base);
      std::string f = pname;
      std::transform(f.begin(), f.end(), f.begin(), ::tolower);
      int n = 0, shown = 0;
      for (uint32_t field = BE32(base, it + 4); field && n < 2000; ++n) {
        std::string name = fieldName(field), low = name;
        std::transform(low.begin(), low.end(), low.begin(), ::tolower);
        if (f.empty() || low.find(f) != std::string::npos) {
          std::string v = hasdefs ? exportText(field, defs) : "?";
          // Data offset of this property inside the object: the UProperty word that holds it.
          // Calibrated once against AActor.Location, known to live at +0x1C0 (448).
          int& s_off_field = g_prop_off_field;
          if (s_off_field < 0) {
            uint32_t pn2 = sp + 0x2C00;
            putw(pn2, "Location");
            ctx.r1.u64 = sp; ctx.r3.u64 = klass; ctx.r4.u64 = pn2;
            __imp__sub_822A0870(ctx, base);
            if (uint32_t lp = ctx.r3.u32)
              for (int o = 0x40; o < 0x90 && s_off_field < 0; o += 4)
                if (BE32(base, lp + o) == 448) { s_off_field = o; REXLOG_INFO("bioshock-recomp: UProperty::Offset at +{:X}", o); }
          }
          if (s_off_field >= 0) v = fmt::format("[+0x{:X}] ", BE32(base, field + s_off_field)) + v;
          if (v.size() > 160) v = v.substr(0, 160) + "...";
          REXLOG_INFO("bioshock-recomp: prop {}.{} = {}", cname, name, v);
          if (shown++ < 8) Log(fmt::format("  {}.{} = {}", cname, name, v));
        }
        put(it + 4, BE32(base, field + 0x3C));
        ctx.r1.u64 = sp; ctx.r3.u64 = it; __imp__sub_821B07A0(ctx, base);
        field = BE32(base, it + 4);
      }
      ctx = saved;
      Log(fmt::format("props {}: {} properties, {} matched (full list in the log: 'prop {}.')", cname, n, shown, cname));
      return;
    }
    putw(s2, pname);
    ctx.r1.u64 = sp; ctx.r3.u64 = klass; ctx.r4.u64 = s2;
    __imp__sub_822A0870(ctx, base);
    uint32_t prop = ctx.r3.u32;
    std::string out = !prop ? "property not found (try: props " + cname + ")" : !hasdefs ? "class has no defaults" : exportText(prop, defs);
    ctx = saved;
    if (quiet) {
      // Only live instances (not the class), only real values, only from one instance.
      bool live = defs != BE32(base, klass + 0x500);
      bool empty = out.empty() || out == "None" || out == "()" || out == "(empty)" || out.find("not found") != std::string::npos ||
                   out.find("no defaults") != std::string::npos ||
                   out.find("SimpleAnim") != std::string::npos;  // class placeholder mesh
      if (live && !empty && (g_vis_from.empty() || g_vis_from == cname)) {
        if (pname == "DrawType" || pname == "StaticMesh" || pname == "Mesh") {
          if (g_vis_from.empty() && pname != "DrawType") { g_vis_from = cname; }
        }
        if (!g_vis_from.empty() && g_vis_from == cname) {
          g_vis_found = true;
          Log(fmt::format("Gene Bank look from {}: {} = {}", cname, pname, out));
          BsQueueCommand("set PlasmidEquipStation " + pname + " " + out);
        }
      }
      return;
    }
    Log(fmt::format("get {} {} = {}", cname, pname, out));
    if (auto it = std::find(g_spawn_wait.begin(), g_spawn_wait.end(), cname); it != g_spawn_wait.end()) {
      g_spawn_wait.erase(it);
      if (!g_spawn_done && out.rfind("Class'", 0) == 0 && out.size() > 7) {
        g_spawn_done = true;
        std::string path = out.substr(6, out.size() - 7);  // Class'Pkg.Name' -> Pkg.Name
        Log("spawning " + path + " in front of you");
        BsQueueCommand("summon " + path);
      } else if (g_spawn_wait.empty() && !g_spawn_done) {
        Log("none of the candidate classes are loaded here (try a level that has one)");
      }
    }
    return;
  }
  uint32_t count = 1;
  if (auto x = cls.find(" x"); x != std::string::npos) {
    count = uint32_t(std::max(1, std::atoi(cls.c_str() + x + 2)));
    cls.resize(x);
  }
  DeriveFromPlayer(base);
  uint32_t gi = g_gameinfo.load(), inv = g_invmgr.load(), uc = FindUClass(base);
  if (cls.rfind("plasmid:", 0) != 0 && (!gi || !inv || !uc)) {
    Log(fmt::format("{}: not ready (gameinfo {:08X} inv {:08X} uclass {:08X}) - move around for a second and retry", cls, gi, inv, uc));
    return;
  }
  if (cls.rfind("plasmid:", 0) == 0) {
    std::string name = cls.substr(8);
    uint32_t player = g_player.load() ? g_player.load() : (inv ? BE32(base, inv + 0x38) : 0);
    uint32_t pm = player ? BE32(base, player + 0x928) : 0;
    if (!player || !pm) { Log(fmt::format("plasmid {}: no player/plasmid manager ({:08X})", name, player)); return; }
    PPCContext saved = ctx;
    uint32_t sp = (ctx.r1.u32 - 0x4000) & ~0xFu, str = sp + 0x2000, fn = sp + 0x1000;
    for (size_t i = 0; i < name.size(); ++i) { base[str + 2 * i] = 0; base[str + 2 * i + 1] = uint8_t(name[i]); }
    base[str + 2 * name.size()] = base[str + 2 * name.size() + 1] = 0;
    std::memset(base + fn, 0, 16);
    ctx.r1.u64 = sp; ctx.r3.u64 = fn; ctx.r4.u64 = str; ctx.r5.u64 = 1; ctx.r6.u64 = 1;
    __imp__sub_824697D8(ctx, base);
    uint32_t idx = BE32(base, fn);
    auto call = [&](void (*f)(PPCContext&, uint8_t*), uint64_t r3, uint64_t r4, uint64_t r5 = 0) {
      ctx.r1.u64 = sp; ctx.r3.u64 = r3; ctx.r4.u64 = r4; ctx.r5.u64 = r5;
      f(ctx, base);
      return ctx.r3.u32;
    };
    // The game's lookup (825473F8) reads class+0x500 with no null check, so resolve the class first.
    std::string res;
    uint32_t klass = idx ? call(__imp__sub_82547268, pm, fn) : 0;
    uint32_t cdo = klass ? BE32(base, klass + 0x500) : 0;
    if (!InHeap(klass) && !(klass >= 0x82000000 && klass < 0x83000000)) res = "skipped (no such plasmid/tonic class)";
    else if (!InHeap(cdo)) res = "skipped (class has no default object)";
    else if ([&] {
               // Cut/unfinished items have no FriendlyName ("...needs to be configured in
               // Inventory.ini") and show as garbage rows in the Gene Bank: skip them.
               uint32_t pn = sp + 0x2800, dev = sp + 0x1100;
               const char* f = "FriendlyName";
               for (size_t i = 0; f[i]; ++i) { base[pn + 2 * i] = 0; base[pn + 2 * i + 1] = uint8_t(f[i]); }
               base[pn + 24] = base[pn + 25] = 0;
               uint32_t prop = call(__imp__sub_822A0870, klass, pn);
               if (!prop) return false;
               std::memset(base + dev, 0, 0x20);
               base[dev] = 0x82; base[dev + 1] = 0x02; base[dev + 2] = 0x5B; base[dev + 3] = 0xB4;  // FStringOutputDevice vtbl
               call(__imp__sub_821640E8, dev + 4, 0x8211C970);
               uint32_t ex = BE32(base, BE32(base, prop) + 280);
               ctx.r1.u64 = sp; ctx.r3.u64 = prop; ctx.r4.u64 = 0; ctx.r5.u64 = dev; ctx.r6.u64 = cdo;
               ctx.r7.u64 = cdo; ctx.r8.u64 = 1; ctx.r9.u64 = 0;
               if (PPCFunc* fp = rex::runtime::ResolveIndirectFunction(ex)) fp(ctx, base);
               std::string t;
               uint32_t d = BE32(base, dev + 4), n = BE32(base, dev + 8);
               for (uint32_t i = 0; d && i < n && i < 200; ++i) { uint8_t c = base[d + 2 * i + 1]; if (!c) break; t += char(c); }
               return t.find("needs to be") != std::string::npos;
             }()) res = "skipped (cut/unfinished item, no name)";
    else if (call(__imp__sub_82546F18, pm, fn)) res = "already owned";
    else if (call(__imp__sub_82549170, pm, fn)) {  // upgrade: removes owned lower levels, true if so
      // Add directly. AddAvailablePlasmid's "you got X" message path dereferences unchecked
      // pointers (crashed at 0x500 / 0x4C), so skip the message.
      call(__imp__sub_82548290, pm, fn, uint64_t(int64_t(-1)));
      res = call(__imp__sub_82546F18, pm, fn) ? "ADDED" : "add called, not owned after";
    } else {
      // Not an upgrade: the game's normal new-plasmid path (only needs a valid class).
      call(__imp__sub_824DA720, player, fn);
      res = call(__imp__sub_82546F18, pm, fn) ? "ADDED (via player)" : "not added";
    }
    ctx = saved;
    Log(fmt::format("plasmid {}: {} (name#{} class {:08X})", name, res, idx, klass));
    return;
  }
  Log(fmt::format("{}: start (uclass {:08X})", cls, uc));
  PPCContext saved = ctx;
  uint32_t sp = (ctx.r1.u32 - 0x4000) & ~0xFu;
  uint32_t str = sp + 0x2000;
  for (size_t i = 0; i < cls.size() && i < 0x7FF; ++i) {
    base[str + 2 * i] = 0;
    base[str + 2 * i + 1] = uint8_t(cls[i]);
  }
  base[str + 2 * cls.size()] = base[str + 2 * cls.size() + 1] = 0;

  ctx.r1.u64 = sp;
  ctx.r3.u64 = uc; ctx.r4.u64 = 0; ctx.r5.u64 = str; ctx.r6.u64 = 0; ctx.r7.u64 = 0x2002; ctx.r8.u64 = 0;
  __imp__sub_82466660(ctx, base);
  uint32_t klass = ctx.r3.u32;
  REXLOG_INFO("bioshock-recomp: give: load -> {:08X}", klass);
  uint32_t item = 0, ok = 0;
  if (klass) {
    // AddStackToInventory takes a stack record: +0x48 item class, +0x4C count (it creates the
    // item itself via GetItemFromClass). Build one in the scratch frame.
    uint32_t stk = sp + 0x1000;
    std::memset(base + stk, 0, 0x200);
    auto put = [&](uint32_t a, uint32_t v) {
      base[a] = uint8_t(v >> 24); base[a + 1] = uint8_t(v >> 16); base[a + 2] = uint8_t(v >> 8); base[a + 3] = uint8_t(v);
    };
    put(stk + 0x48, klass);
    put(stk + 0x4C, count);
    item = stk;
    // Ammo has per-type caps; if the full count does not fit, halve until it does.
    for (uint32_t n = count; n >= 1; n /= 2) {
      std::memset(base + stk, 0, 0x200);
      put(stk + 0x48, klass);
      put(stk + 0x4C, n);
      ctx.r1.u64 = sp; ctx.r3.u64 = inv; ctx.r4.u64 = stk;
      __imp__sub_824BE0D8(ctx, base);
      ok = ctx.r3.u32;
      if (ok) { count = n; break; }
    }
  }
  ctx = saved;
  Log(fmt::format("{}: class {:08X} item {:08X} -> {}", cls, klass, item,
                  !klass ? "class not found" : ok ? fmt::format("GIVEN x{}", count) : "inventory refused (full or not an item)"));
}

void BsQueueGive(std::string cls) {
  std::lock_guard<std::mutex> lk(g_gmu);
  g_give_queue.push_back(std::move(cls));
}
std::deque<std::string> BsGiveLog() {
  std::lock_guard<std::mutex> lk(g_gmu);
  return g_give_log;
}

// ---------------------------------------------------------------------------------------------
// "Double encounters": every AI pawn the game spawns through AActor::Spawn (0x822F8B80; script
// Spawn -> execSpawn 0x822F8C70) gets a second copy next to it. Args: r3 spawner, r4 class,
// r5 owner, r6 tag, r7 Location* (null = spawner+448), r8 Rotation* (null = +460), r9/r10 extra.
extern "C" void __imp__sub_822F8B80(PPCContext& __restrict ctx, uint8_t* base);
// AShockPawn::TakeSimple/ScriptedDamage core (from exec 0x8250F0F0): (pawn, r4 byte DamageType,
// f1 Damage, f2 scale=1.0, r7 Instigator, r8 FName* = None). Full pipeline: reactions + death.
extern "C" void __imp__sub_8250EFB0(PPCContext& __restrict ctx, uint8_t* base);
// AEcologyFighter::AddForcedEnemy(this, AActor*) (from exec 0x825DB150): makes an AI treat an actor as an enemy.
extern "C" void __imp__sub_825DB088(PPCContext& __restrict ctx, uint8_t* base);
#include <unordered_map>
#include <cmath>
#include <chrono>
namespace {
std::atomic<bool> g_double{false};
bool g_in_double = false;
std::unordered_map<uint32_t, bool> g_ai_class;  // class -> should double
inline bool HeapOrImg(uint32_t a) { return (a >= 0x40000000 && a < 0x5F000000) || (a >= 0x82000000 && a < 0x83000000); }
std::string FNameStr(PPCContext& ctx, uint8_t* base, uint32_t obj, uint32_t sp) {
  uint32_t nm = sp + 0x1200;
  std::memset(base + nm, 0, 0x10);
  PPCContext saved = ctx;
  ctx.r1.u64 = sp; ctx.r3.u64 = nm; ctx.r4.u64 = obj + 0x24;
  __imp__sub_8246ECC8(ctx, base);
  ctx = saved;
  std::string o;
  uint32_t d = BE32(base, nm), n = BE32(base, nm + 4);
  for (uint32_t i = 0; d && i < n && i < 80; ++i) { uint8_t c = base[d + 2 * i + 1]; if (!c) break; o += char(c); }
  return o;
}
bool ShouldDouble(PPCContext& ctx, uint8_t* base, uint32_t cls, uint32_t sp) {
  if (auto it = g_ai_class.find(cls); it != g_ai_class.end()) return it->second;
  bool pawn = false, skip = false;
  std::string first;
  for (uint32_t c = cls, k = 0; c && HeapOrImg(c) && k < 16; c = BE32(base, c + 0x38), ++k) {
    std::string n = FNameStr(ctx, base, c, sp);
    if (k == 0) first = n;
    if (n == "Pawn") pawn = true;
    if (n.find("Player") != std::string::npos || n.find("LittleSister") != std::string::npos ||
        n.find("Vehicle") != std::string::npos) skip = true;
  }
  bool r = pawn && !skip;
  g_ai_class[cls] = r;
  REXLOG_INFO("bioshock-recomp: double: class {} ({:08X}) -> {}", first, cls, r ? "doubled" : "not doubled");
  return r;
}
}  // namespace
void BsSetDoubleEncounters(bool on) { g_double = on; }

// ---- Player 2 puppet (second controller drives a spawned pawn, e.g. a Big Daddy) ----------
// Offsets from `props SpawnedBouncer` (2026-11-01): Location +0x1C0, Rotation +0x1CC (int
// pitch/yaw/roll, 65536 = 360 deg), Velocity +0x1D8, Acceleration +0x1E4, Controller +0x420,
// AccelRate +0x4E4, DesiredRotation +0x310.
std::string g_p2_view = "SpawnedBouncer";
std::atomic<uint32_t> g_p2{0}, g_p2_class{0};
std::atomic<bool> g_p2_want{false}, g_p2_detach{true}, g_p2_kbd{false};
std::atomic<int> g_view_mode{0};
int16_t g_pad_pitch = 0;
std::atomic<bool> g_p2_real_weapons{true};
std::string g_slam_name = "BigRightHook";  // recorded: ShoulderWeapon fires BigRightHook
std::string g_walk_anim;  // set with "p2walk <AnimName>" in Custom command
void BsSetP2Walk(std::string n) { g_walk_anim = std::move(n); Log("P2: walk anim = '" + g_walk_anim + "'"); }  // replaced by the recorder when an AI slams
void BsP2RealWeapons(bool on) { g_p2_real_weapons = on; }  // 0 normal, 1 alternate P1/P2 every frame, 2 P2 only
void BsSetViewMode(int m) { g_view_mode = m; }
int BsGetViewMode() { return g_view_mode.load(); }
// Split view camera for the Big Daddy: an invisible Engine.Mutator we place behind him. With a
// non-pawn ViewTarget and behind-view off, the engine takes the camera's Location/Rotation as-is,
// so Jack's controller rotation (his aim) is never touched.
std::atomic<uint32_t> g_cam_actor{0}, g_cam_class{0};
std::atomic<bool> g_cam_want{false};
std::atomic<bool> g_split_default{true};   // spawning P2 (D-pad x5 / triple F8) opens co-op split
std::atomic<bool> g_jack_body{false};
std::atomic<bool> g_p2_blind{true};
std::atomic<bool> g_p2_squeeze{true};      // while you drive: collision radius shrunk to about Jack's        // while you drive: clear P2's AI Enemy/Focus every tick
std::atomic<uint32_t> g_p2_yaw_of{0};      // pawn whose facing we own (0 = re-read from the pawn)
std::atomic<uint32_t> g_ai_swings{0};      // AI-started weapon calls on P2 while you drive      // experimental: give Jack a Splicer mesh in the P2 view
// Split view (two windows): the app installs the window opener; the frame tag lives in the SDK.
std::function<void(bool)> g_split_window_fn;
void BsSplitWindow(bool on) {
  if (on) g_view_mode = 1;  // alternate frames
  else if (g_view_mode.load() == 1) g_view_mode = 0;
  if (!on) rex::graphics::split_view::state().enabled = false;
  if (g_split_window_fn) g_split_window_fn(on);
}
bool BsCoopSplit() { return rex::graphics::split_view::state().enabled.load(); }
void BsSplitInvert(bool inv) { rex::graphics::split_view::state().invert = inv; }
bool BsSplitInverted() { return rex::graphics::split_view::state().invert.load(); }
// Co-op: which player the controller (pad 0) drives. Back / F6 flips it, and the views swap so the
// player you are holding the controller for is always in the main window.
std::atomic<bool> g_pad_p2{false};
bool BsPadOnP2() { return g_pad_p2.load() && BsCoopSplit(); }
void BsPadSwap() {
  if (!BsCoopSplit() || !g_p2.load()) { Log("Controller swap: only in co-op split (spawn a Big Daddy first)"); return; }
  bool to_p2 = !g_pad_p2.load();
  g_pad_p2 = to_p2;
  if (to_p2) g_p2_kbd = true;  // the Big Daddy has to be in driven mode for any input to reach him
  auto& sv = rex::graphics::split_view::state();
  sv.invert = !sv.invert.load();  // relative flip, so a manual F7 correction is kept
  Log(to_p2 ? "Controller -> Big Daddy (his view is now the main window). Back / F6 = back to Jack"
            : "Controller -> Jack (main window). Back / F6 = Big Daddy");
}
void BsSplitDefault(bool on) { g_split_default = on; }
void BsJackBody(bool on) { g_jack_body = on; }
void BsP2Blind(bool on) { g_p2_blind = on; }
void BsP2Squeeze(bool on) { g_p2_squeeze = on; }
std::string BsSplitStats() {
  namespace sv = rex::graphics::split_view;
  return fmt::format("frames: main {}  P2 window {}", sv::state().frames_routed[0].load(), sv::state().frames_routed[1].load());
}
bool BsKeyDown(int vk);
int BsMouseDX();
int BsMouseDY();
uint8_t BsPadLT(int i);
void BsQueueCommand(std::string cmd);
// F8: hand keyboard+mouse (and the camera) to P2, or back to P1.
bool BsP2KeyboardActive() { return g_p2_kbd.load() && g_p2.load(); }
std::atomic<bool> g_p2_autoswitch{false};
bool BsPadPollIndex(int i, uint16_t* buttons, int16_t* lx, int16_t* ly, int16_t* rx, int16_t* ry, uint8_t* rt);
// Spawn a Big Daddy and put you in control of him (triple F8, or D-pad down x5 on the controller).
void BsP2QuickSpawn(const char* why) {
  if (g_p2_kbd.load()) BsQueueCommand("viewself"), BsQueueCommand("behindview 0");
  g_p2_kbd = false;
  g_p2_detach = false; g_p2_view = "SpawnedBouncer"; g_p2_want = true; g_p2_autoswitch = true;
  BsQueueCommand("summon ShockAIClasses.SpawnedBouncer");
  Log(std::string("P2: ") + why + " - spawning a Big Daddy for you");
}
// D-pad down pressed 5 times within 1.5 s (controller 1) = BsP2QuickSpawn. Polled every frame.
void BsPollPadSpawnCombo() {
  static bool s_prev = false; static std::chrono::steady_clock::time_point s_t[5]{};
  uint16_t b = 0; int16_t a0, a1, a2, a3; uint8_t t;
  bool down = BsPadPollIndex(0, &b, &a0, &a1, &a2, &a3, &t) && (b & 0x0002);
  if (down && !s_prev) {
    auto now = std::chrono::steady_clock::now();
    for (int i = 0; i < 4; ++i) s_t[i] = s_t[i + 1];
    s_t[4] = now;
    if (now - s_t[0] < std::chrono::milliseconds(1500)) { for (auto& x : s_t) x = {}; BsP2QuickSpawn("D-pad down x5"); }
  }
  s_prev = down;
}
// F9 panel open: world frozen ("playersonly" = only players tick) and P2 ignores input, so menu
// navigation with the pad does not punch/jump. Restored when the panel closes.
std::atomic<bool> g_panel_open{false};
bool BsPanelIsOpen() { return g_panel_open.load(); }
bool g_our_fire = false;  // set while P2's own (player-driven) weapon call runs
bool g_panel_froze = false;
bool g_user_frozen = false;  // "Freeze world" checkbox
void BsPanelOpen(bool open) {
  g_panel_open = open;
  if (g_user_frozen) return;  // already frozen by the checkbox: leave it alone
  if (open && !g_panel_froze) { g_panel_froze = true; BsQueueCommand("playersonly"); }
  else if (!open && g_panel_froze) { g_panel_froze = false; BsQueueCommand("playersonly"); }
}
// "Freeze world" checkbox: playersonly is a toggle, so coordinate with the panel's own freeze.
void BsFreezeWorld(bool on) {
  if (on == g_user_frozen) return;
  g_user_frozen = on;
  if (g_panel_froze) { g_panel_froze = false; if (!on) BsQueueCommand("playersonly"); }  // hand over ownership
  else BsQueueCommand("playersonly");
}
std::atomic<bool> g_p2_ally{true};
std::atomic<bool> g_p2_silence{false};  // crashed: AI code reads Controller.Pawn->+0x430
void BsP2Silence(bool on) { g_p2_silence = on; }
void BsP2Ally(bool on) { g_p2_ally = on; }
void BsP2ToggleKeyboard() {
  // Triple-tap F8 (within 0.8 s): spawn a Big Daddy as P2 and switch to him once he is ready.
  static std::chrono::steady_clock::time_point s_taps[3]{};
  auto now = std::chrono::steady_clock::now();
  s_taps[0] = s_taps[1]; s_taps[1] = s_taps[2]; s_taps[2] = now;
  if (now - s_taps[0] < std::chrono::milliseconds(800)) {
    s_taps[0] = s_taps[1] = s_taps[2] = {};
    BsP2QuickSpawn("triple F8");
    return;
  }
  if (!g_p2.load()) { Log("P2: spawn a P2 first (F9 > Player 2), or triple-tap F8"); return; }
  g_p2_kbd = !g_p2_kbd.load();
  if (g_p2_kbd) { BsQueueCommand("viewclass " + g_p2_view); BsQueueCommand("behindview 1"); }
  else { BsQueueCommand("viewself"); BsQueueCommand("behindview 0"); }
  Log(g_p2_kbd ? "P2: keyboard+mouse now drive P2 (F8 to switch back)" : "P2: keyboard+mouse back to P1");
}
bool BsPadPollIndex(int i, uint16_t* buttons, int16_t* lx, int16_t* ly, int16_t* rx, int16_t* ry, uint8_t* rt);
void BsP2Request(bool detach, std::string view) { g_p2_detach = detach; g_p2_want = true; g_p2_view = std::move(view); }
void BsP2Release() { if (g_p2_kbd.load()) BsP2ToggleKeyboard(); g_p2 = 0; }
uint32_t BsP2Pawn() { return g_p2.load(); }
// Level actor list: a player pointer field -> ULevel whose Actors TArray contains the player.
static bool FindActorList(uint8_t* base, uint32_t player, uint32_t* data, uint32_t* num) {
  static uint32_t s_pf = 0, s_af = 0;
  auto tryPair = [&](uint32_t pf, uint32_t af) {
    uint32_t lv = BE32(base, player + pf);
    if (!InHeap(lv)) return false;
    uint32_t d = BE32(base, lv + af), n = BE32(base, lv + af + 4), mx = BE32(base, lv + af + 8);
    if (!InHeap(d) || n < 10 || n > 100000 || mx < n || mx > 400000) return false;
    for (uint32_t i = 0; i < n; ++i)
      if (BE32(base, d + 4 * i) == player) { *data = d; *num = n; s_pf = pf; s_af = af; return true; }
    return false;
  };
  if (s_pf && tryPair(s_pf, s_af)) return true;
  for (uint32_t pf = 0x30; pf < 0x400; pf += 4)
    for (uint32_t af = 0x20; af < 0x400; af += 4)
      if (tryPair(pf, af)) return true;
  return false;
}

bool g_our_anim = false;  // set while P2's own (player-driven) animation call runs
// Play a named animation on an actor: what AActor::execPlayAnimationOnChannel (0x822F6558) does:
// anim = actor+992 (0x821CAFA0), anim->vtable[8/4](channel, &FName, 1, flag).
static uint32_t PlayAnim(PPCContext& ctx, uint8_t* base, uint32_t actor, const char* name, uint32_t channel, uint32_t flag) {
  uint32_t anim = BE32(base, actor + 992);
  if (!InHeap(anim)) return 0;
  PPCContext saved = ctx;
  uint32_t sp = (ctx.r1.u32 - 0x7000) & ~0xFu, str = sp + 0x2000, fn = sp + 0x1000;
  size_t n = std::strlen(name);
  for (size_t i = 0; i < n; ++i) { base[str + 2 * i] = 0; base[str + 2 * i + 1] = uint8_t(name[i]); }
  base[str + 2 * n] = base[str + 2 * n + 1] = 0;
  std::memset(base + fn, 0, 16);
  ctx.r1.u64 = sp; ctx.r3.u64 = fn; ctx.r4.u64 = str; ctx.r5.u64 = 1; ctx.r6.u64 = 1;
  __imp__sub_824697D8(ctx, base);
  ctx.r1.u64 = sp; ctx.r3.u64 = anim; ctx.r4.u64 = channel; ctx.r5.u64 = fn; ctx.r6.u64 = 1; ctx.r7.u64 = flag;
  uint32_t r = 0;
  static bool s_logged_fn = false;
  if (!s_logged_fn) { s_logged_fn = true; REXLOG_INFO("bioshock-recomp: anim: component {:08X} vtable {:08X} play fn {:08X}", anim, BE32(base, anim), BE32(base, BE32(base, anim) + 8)); }
  g_our_anim = true;
  if (PPCFunc* f = rex::runtime::ResolveIndirectFunction(BE32(base, BE32(base, anim) + 8))) { f(ctx, base); r = ctx.r3.u32; }
  g_our_anim = false;
  ctx = saved;
  return r;
}

// Fire one of P2's own Big Daddy weapons the way the AI does: AWeapon::InitiateDamage(FName Attack)
// = weapon vtable+740 (from execInitiateDamage 0x824F8B08). Weapons: HandWeapon +0x1284 (fists/drill),
// ShoulderWeapon +0x1288 (slam), ThreeSixtyWeapon +0x128C. Attack names recorded from the AI.
static void SetBool(PPCContext& ctx, uint8_t* base, uint32_t obj, const char* name, bool on);
static bool FireBDWeapon(PPCContext& ctx, uint8_t* base, uint32_t pawn, uint32_t slot, const char* attack) {
  SetBool(ctx, base, pawn, "bNoWeaponFiring", false);  // our own attack: allowed
  struct Rearm { PPCContext& c; uint8_t* b; uint32_t p; ~Rearm() { if (g_p2_kbd.load()) SetBool(c, b, p, "bNoWeaponFiring", true); } } rearm{ctx, base, pawn};
  uint32_t w = BE32(base, pawn + slot);
  if (!InHeap(w)) { Log(fmt::format("P2: no weapon at +{:X}", slot)); return false; }
  PPCContext saved = ctx;
  uint32_t sp = (ctx.r1.u32 - 0x7800) & ~0xFu, str = sp + 0x2000, fn = sp + 0x1000;
  size_t n = std::strlen(attack);
  for (size_t i = 0; i < n; ++i) { base[str + 2 * i] = 0; base[str + 2 * i + 1] = uint8_t(attack[i]); }
  base[str + 2 * n] = base[str + 2 * n + 1] = 0;
  std::memset(base + fn, 0, 16);
  ctx.r1.u64 = sp; ctx.r3.u64 = fn; ctx.r4.u64 = str; ctx.r5.u64 = 1; ctx.r6.u64 = 1;
  __imp__sub_824697D8(ctx, base);
  ctx.r1.u64 = sp; ctx.r3.u64 = w; ctx.r4.u64 = fn;
  g_our_fire = true;
  if (PPCFunc* f = rex::runtime::ResolveIndirectFunction(BE32(base, BE32(base, w) + 740))) f(ctx, base);
  g_our_fire = false;
  ctx = saved;
  return true;
}

// Property lookup by name on a live object: returns data offset, and for bools the bit mask.
static bool FindPropOffset(PPCContext& ctx, uint8_t* base, uint32_t obj, const char* name, uint32_t* off, uint32_t* mask) {
  uint32_t cls = BE32(base, obj + 0x2C);
  if (!InHeap(cls) && !(cls >= 0x82000000 && cls < 0x83000000)) return false;
  PPCContext saved = ctx;
  uint32_t sp = (ctx.r1.u32 - 0x7C00) & ~0xFu, str = sp + 0x2000;
  size_t n = std::strlen(name);
  for (size_t i = 0; i < n; ++i) { base[str + 2 * i] = 0; base[str + 2 * i + 1] = uint8_t(name[i]); }
  base[str + 2 * n] = base[str + 2 * n + 1] = 0;
  auto findf = [&](const char* nm) {
    size_t k = std::strlen(nm);
    for (size_t i = 0; i < k; ++i) { base[str + 2 * i] = 0; base[str + 2 * i + 1] = uint8_t(nm[i]); }
    base[str + 2 * k] = base[str + 2 * k + 1] = 0;
    ctx.r1.u64 = sp; ctx.r3.u64 = cls; ctx.r4.u64 = str;
    __imp__sub_822A0870(ctx, base);
    return ctx.r3.u32;
  };
  if (g_prop_off_field < 0) {  // calibrate with AActor.Location (+0x1C0)
    if (uint32_t lp = findf("Location"))
      for (int o = 0x40; o < 0x90 && g_prop_off_field < 0; o += 4)
        if (BE32(base, lp + o) == 448) g_prop_off_field = o;
  }
  uint32_t prop = findf(name);
  ctx = saved;
  if (!prop || g_prop_off_field < 0) return false;
  *off = BE32(base, prop + g_prop_off_field);
  *mask = 0;
  // UBoolProperty::BitMask sits after the base UProperty fields: first single-bit word found.
  for (int o = g_prop_off_field + 4; o < g_prop_off_field + 0x40; o += 4) {
    uint32_t v = BE32(base, prop + o);
    if (v && !(v & (v - 1)) && v != 1u << 31) { *mask = v; break; }
  }
  return true;
}
static void SetBool(PPCContext& ctx, uint8_t* base, uint32_t obj, const char* name, bool on) {
  uint32_t off, mask;
  if (!FindPropOffset(ctx, base, obj, name, &off, &mask) || !mask) return;
  uint32_t v = BE32(base, obj + off);
  v = on ? (v | mask) : (v & ~mask);
  base[obj + off] = uint8_t(v >> 24); base[obj + off + 1] = uint8_t(v >> 16); base[obj + off + 2] = uint8_t(v >> 8); base[obj + off + 3] = uint8_t(v);
}
// Hide/show Jack and everything he owns (first-person arms/weapon) while you play as P2.
static void HideJack(PPCContext& ctx, uint8_t* base, bool hide) {
  uint32_t pl = g_player.load();
  if (!InHeap(pl)) return;
  SetBool(ctx, base, pl, "bHidden", hide);
  uint32_t ooff, m;
  if (!FindPropOffset(ctx, base, pl, "Owner", &ooff, &m)) return;
  uint32_t boff = 0, woff = 0, wpn = 0;
  if (!FindPropOffset(ctx, base, pl, "Base", &boff, &m)) boff = 0;
  if (FindPropOffset(ctx, base, pl, "Weapon", &woff, &m)) { wpn = BE32(base, pl + woff); if (InHeap(wpn)) SetBool(ctx, base, wpn, "bHidden", hide); else wpn = 0; }
  uint32_t d = 0, n = 0, cnt = 0, hands = 0;
  if (!FindActorList(base, pl, &d, &n)) return;
  // Two passes: the first finds PlayerHands so the second can catch what is attached to it.
  for (int pass = 0; pass < 2; ++pass)
  for (uint32_t i = 0; i < n; ++i) {
    uint32_t o = BE32(base, d + 4 * i);
    if (!InHeap(o) || o == pl) continue;
    if (pass == 0) {
      uint32_t c = BE32(base, o + 0x2C); uint32_t sp3 = (ctx.r1.u32 - 0x5800) & ~0xFu;
      if (BE32(base, o + ooff) == pl && HeapOrImg(c) && FNameStr(ctx, base, c, sp3) == "PlayerHands") hands = o;
      continue;
    }
    uint32_t ow = BE32(base, o + ooff);
    uint32_t bs = boff ? BE32(base, o + boff) : 0;
    bool mine = ow == pl || (InHeap(ow) && BE32(base, ow + ooff) == pl) || bs == pl || (wpn && (bs == wpn || ow == wpn));
    // First-person arms: PlayerHands (owned by Jack) and the held weapon attached to it (Base =
    // PlayerHands, e.g. Wrench). Also the GPS objective arrow. bHidden alone is ignored for the
    // first-person view, so they are also shrunk to nothing (DrawScale) and restored after.
    std::string cname;
    { uint32_t c = BE32(base, o + 0x2C); uint32_t sp3 = (ctx.r1.u32 - 0x5800) & ~0xFu; if (HeapOrImg(c)) cname = FNameStr(ctx, base, c, sp3); }
    if (cname == "PlayerHands") hands = o;
    bool fp = cname == "PlayerHands" || (hands && bs == hands) || cname == "GPSArrow";
    if (mine || fp) {
      SetBool(ctx, base, o, "bHidden", hide); ++cnt;
      if (fp) {
        uint32_t so, sm;
        if (FindPropOffset(ctx, base, o, "DrawScale", &so, &sm)) {
          static std::unordered_map<uint32_t, float> s_scale;
          auto gf2 = [&](uint32_t a) { uint32_t v = BE32(base, a); float f; std::memcpy(&f, &v, 4); return f; };
          auto pf2 = [&](uint32_t a, float f) { uint32_t v; std::memcpy(&v, &f, 4); base[a] = v >> 24; base[a + 1] = v >> 16; base[a + 2] = v >> 8; base[a + 3] = v; };
          if (hide) { if (!s_scale.count(o)) s_scale[o] = gf2(o + so); pf2(o + so, 0.0001f); }
          else if (s_scale.count(o)) { pf2(o + so, s_scale[o]); s_scale.erase(o); }
        }
      }
    }
  }
  Log(fmt::format("P2: Jack {} ({} owned actors)", hide ? "hidden" : "shown", cnt));
}

// AActor::StopAnimation(handle) as execStopAnimation (0x822F50C8): (actor+980)->vtable[96/4](handle).
static void StopAnim(PPCContext& ctx, uint8_t* base, uint32_t actor, uint32_t handle) {
  if (!handle) return;
  uint32_t mgr = BE32(base, actor + 980);
  if (!InHeap(mgr)) return;
  PPCContext saved = ctx;
  ctx.r1.u64 = (ctx.r1.u32 - 0x7000) & ~0xFu; ctx.r3.u64 = mgr; ctx.r4.u64 = handle;
  if (PPCFunc* f = rex::runtime::ResolveIndirectFunction(BE32(base, BE32(base, mgr) + 96))) f(ctx, base);
  ctx = saved;
}
void BsStopAnimOn(PPCContext& ctx, uint8_t* base, uint32_t actor, uint32_t handle) { StopAnim(ctx, base, actor, handle); }
// One handle per animation slot: a new anim on a slot stops the previous one, so looping anims
// (walk, drill spin) never pile up. Piled-up loops were why anims and attacks stopped working.
static uint32_t g_anim_slot[4] = {}, g_anim_owner = 0;
static uint32_t PlaySlot(PPCContext& ctx, uint8_t* base, uint32_t actor, const char* name, uint32_t channel, uint32_t flag) {
  if (g_anim_owner != actor) { g_anim_owner = actor; std::memset(g_anim_slot, 0, sizeof(g_anim_slot)); }
  StopAnim(ctx, base, actor, g_anim_slot[channel & 3]);
  g_anim_slot[channel & 3] = PlayAnim(ctx, base, actor, name, channel, flag);
  return g_anim_slot[channel & 3];
}

static void BsCamTick(PPCContext& ctx, uint8_t* base, uint32_t p) {
  auto put = [&](uint32_t a, uint32_t v) { base[a] = uint8_t(v >> 24); base[a + 1] = uint8_t(v >> 16); base[a + 2] = uint8_t(v >> 8); base[a + 3] = uint8_t(v); };
  auto getf = [&](uint32_t a) { uint32_t v = BE32(base, a); float f; std::memcpy(&f, &v, 4); return f; };
  auto putf = [&](uint32_t a, float f) { uint32_t v; std::memcpy(&v, &f, 4); put(a, v); };
  // Camera: the player's controller (player pawn +0x420) ViewTarget is at +0x5F0, bBehindView is
  // bit 0x04 of the byte at +0x564 (props ShockPlayerController, 2026-11-01).
  static uint32_t s_frame = 0, s_last564 = 0xFFFFFFFF;
  static int32_t s_bd_pitch = -2200;
  static auto s_cam_try = std::chrono::steady_clock::now() - std::chrono::seconds(10);
  uint32_t pl = g_player.load(), pc = pl ? BE32(base, pl + 0x420) : 0;
  if (!InHeap(pc)) return;
  int m = g_view_mode.load();
  if (g_p2_kbd.load() && m == 0) m = 3;  // F8 = play as P2: chase camera
  if (m == 1) {
    // Two views, alternate frames: odd = Big Daddy (window 2), even = Jack (window 1).
    bool odd = (++s_frame & 1) != 0;
    uint32_t cam = g_cam_actor.load();
    bool cam_ok = InHeap(cam) && BE32(base, cam + 0x2C) == g_cam_class.load();
    static int s_cam_tries = 0;
    if (cam_ok) s_cam_tries = 0;
    else if (s_cam_tries < 3 && std::chrono::steady_clock::now() - s_cam_try > std::chrono::seconds(3)) {
      s_cam_try = std::chrono::steady_clock::now();
      g_cam_want = true;
      BsQueueCommand("summon Engine.Mutator");
      Log(++s_cam_tries < 3 ? "split cam: spawning the Big Daddy camera"
                            : "split cam: last try - if this fails the P2 view orbits with Jack's look");
    }
    if (odd) {
      if (g_p2_kbd.load()) s_bd_pitch = std::clamp(s_bd_pitch - BsMouseDY() * 40 + (g_pad_p2.load() ? int(g_pad_pitch / 32767.0f * 300.0f) : 0), -11000, 5000);
      if (cam_ok) {
        // Orbit camera: 330 units behind the Big Daddy's head, looking along his facing.
        const float k = 6.2831853f / 65536.0f;
        int32_t yaw = int32_t(BE32(base, p + 0x1D0));
        float yr = yaw * k, pr = s_bd_pitch * k;
        float fx = std::cos(pr) * std::cos(yr), fy = std::cos(pr) * std::sin(yr), fz = std::sin(pr);
        float tx = getf(p + 0x1C0), ty = getf(p + 0x1C4), tz = getf(p + 0x1C8) + 95.0f;
        putf(cam + 0x1C0, tx - fx * 330.0f); putf(cam + 0x1C4, ty - fy * 330.0f); putf(cam + 0x1C8, tz - fz * 330.0f + 25.0f);
        put(cam + 0x1CC, uint32_t(s_bd_pitch)); put(cam + 0x1D0, uint32_t(yaw)); put(cam + 0x1D4, 0);
        put(pc + 0x5F0, cam);
        base[pc + 0x564] &= uint8_t(~0x04);  // behind-view off: camera = ViewTarget's own location/rotation
      } else {
        put(pc + 0x5F0, p);                  // fallback until the camera exists: orbit with Jack's look
        base[pc + 0x564] |= 0x04;
      }
    } else {
      put(pc + 0x5F0, pl);
      base[pc + 0x564] &= uint8_t(~0x04);
    }
    rex::graphics::split_view::state().current_view = odd ? 1u : 0u;  // stamped into this frame's swap
    static auto s_t0 = std::chrono::steady_clock::now(); static uint32_t s_n0 = 0;
    auto now = std::chrono::steady_clock::now();
    if (now - s_t0 > std::chrono::seconds(5)) {
      double sec = std::chrono::duration<double>(now - s_t0).count();
      Log(fmt::format("cam split: {:.1f} game ticks/s (each view gets half), camera {:08X} {}", (s_frame - s_n0) / sec, cam, cam_ok ? "ok" : "missing"));
      s_t0 = now; s_n0 = s_frame;
    }
    return;
  }
  if (m == 2) put(pc + 0x5F0, p);
  else if (m == 3) {
    // Chase camera behind P2: the behind-view camera orbits the view target using the
    // controller's rotation, so keep the controller facing where P2 faces. Mouse Y = pitch.
    static int32_t s_pitch = -2500;  // slightly looking down (65536 = 360 deg)
    s_pitch = std::clamp(s_pitch - BsMouseDY() * 40 + int(g_pad_pitch / 32767.0f * 300.0f), -12000, 6000);
    put(pc + 0x5F0, p);
    put(pc + 0x1CC, uint32_t(s_pitch));
    put(pc + 0x1CC + 4, BE32(base, p + 0x1CC + 4));
    base[pc + 0x564 + 0] |= 0x04;  // bBehindView (bit seen toggling in the log: 2000xxxx <-> 2400xxxx)
  } else if (m == 0) {
    // Leaving split: make sure Jack's view is his own again.
    uint32_t vt = BE32(base, pc + 0x5F0);
    if (vt == g_cam_actor.load() && vt) { put(pc + 0x5F0, pl); base[pc + 0x564] &= uint8_t(~0x04); }
  }
  uint32_t w564 = BE32(base, pc + 0x564);
  if (w564 != s_last564) { s_last564 = w564; Log(fmt::format("cam: controller {:08X} +564 = {:08X} viewtarget {:08X}", pc, w564, BE32(base, pc + 0x5F0))); }
  (void)ctx;
}

// ---- P2 "use": the Big Daddy picks up the nearest loose item; it goes into Jack's (shared)
// inventory through the same AddStackToInventory call the F9 give uses. BioShock world pickups are
// ...PickupItem actors; the item class is the actor's own class unless a property names another.
static bool GiveItemClass(PPCContext& ctx, uint8_t* base, uint32_t klass, uint32_t* count) {
  DeriveFromPlayer(base);
  uint32_t inv = g_invmgr.load();
  if (!inv || !klass) return false;
  PPCContext saved = ctx;
  uint32_t sp = (ctx.r1.u32 - 0x4000) & ~0xFu, stk = sp + 0x1000;
  auto put = [&](uint32_t a, uint32_t v) { base[a] = uint8_t(v >> 24); base[a + 1] = uint8_t(v >> 16); base[a + 2] = uint8_t(v >> 8); base[a + 3] = uint8_t(v); };
  bool ok = false;
  for (uint32_t n = std::max(1u, *count); n >= 1 && !ok; n /= 2) {  // ammo caps: halve until it fits
    std::memset(base + stk, 0, 0x200);
    put(stk + 0x48, klass); put(stk + 0x4C, n);
    ctx.r1.u64 = sp; ctx.r3.u64 = inv; ctx.r4.u64 = stk;
    __imp__sub_824BE0D8(ctx, base);
    if (ctx.r3.u32) { ok = true; *count = n; }
    ctx = saved;
  }
  ctx = saved;
  return ok;
}
// Lists an object's own script properties (name, type, offset, raw value), class chain up to Actor.
// Used once per pickup class so we can see where the item class / count live.
static void DumpProps(PPCContext& ctx, uint8_t* base, uint32_t obj, const char* why) {
  uint32_t o0 = 0, m0 = 0;
  FindPropOffset(ctx, base, obj, "Location", &o0, &m0);  // calibrates g_prop_off_field
  if (g_prop_off_field < 0) return;
  uint32_t sp = (ctx.r1.u32 - 0x5000) & ~0xFu;
  std::string out;
  int lines = 0;
  for (uint32_t c = BE32(base, obj + 0x2C), k = 0; c && HeapOrImg(c) && k < 8; c = BE32(base, c + 0x38), ++k) {
    std::string cn = FNameStr(ctx, base, c, sp);
    if (cn == "Actor" || cn == "Object") break;
    for (uint32_t f = BE32(base, c + 0x44), guard = 0; f && HeapOrImg(f) && guard < 400; f = BE32(base, f + 0x3C), ++guard) {
      std::string ty = FNameStr(ctx, base, BE32(base, f + 0x2C), sp);
      if (ty.size() < 8 || ty.compare(ty.size() - 8, 8, "Property") != 0) continue;
      uint32_t off = BE32(base, f + g_prop_off_field);
      if (off > 0x4000) continue;
      out += fmt::format("\n    {}.{} {} +{:X} = {:08X}", cn, FNameStr(ctx, base, f, sp), ty, off, BE32(base, obj + off));
      if (++lines > 120) break;
    }
  }
  REXLOG_INFO("bioshock-recomp: give: props of {:08X} ({}):{}", obj, why, out);
}

// Class lookup by bare name in any package (FindObject<UClass>(ANY_PACKAGE, Name)). 0 if none.
static uint32_t FindClassByName(PPCContext& ctx, uint8_t* base, const std::string& name) {
  PPCContext saved = ctx;
  uint32_t sp = (ctx.r1.u32 - 0x4800) & ~0xFu, s1 = sp + 0x2000;
  for (size_t i = 0; i < name.size() && i < 200; ++i) { base[s1 + 2 * i] = 0; base[s1 + 2 * i + 1] = uint8_t(name[i]); }
  base[s1 + 2 * name.size()] = base[s1 + 2 * name.size() + 1] = 0;
  ctx.r1.u64 = sp; ctx.r3.u64 = 0xFFFFFFFFu; ctx.r4.u64 = s1; ctx.r5.u64 = 0;
  __imp__sub_821C5028(ctx, base);
  uint32_t c = ctx.r3.u32;
  ctx = saved;
  return c;
}

static void BsP2Use(PPCContext& ctx, uint8_t* base, uint32_t p) {
  uint32_t pl = g_player.load(), d = 0, n = 0;
  if (!InHeap(pl) || !FindActorList(base, pl, &d, &n)) { Log("P2 use: level actor list not found"); return; }
  auto getf = [&](uint32_t a) { uint32_t v = BE32(base, a); float f; std::memcpy(&f, &v, 4); return f; };
  auto putf = [&](uint32_t a, float f) { uint32_t v; std::memcpy(&v, &f, 4); base[a] = uint8_t(v >> 24); base[a + 1] = uint8_t(v >> 16); base[a + 2] = uint8_t(v >> 8); base[a + 3] = uint8_t(v); };
  static std::vector<uint32_t> s_taken;
  float px = getf(p + 0x1C0), py = getf(p + 0x1C4), pz = getf(p + 0x1C8);
  uint32_t sp = (ctx.r1.u32 - 0x5000) & ~0xFu, best = 0;
  float bestd = 230.0f * 230.0f;  // Big Daddy reach
  std::string bestcn;
  std::vector<std::pair<float, std::string>> near;  // recon: what IS in reach when nothing matches
  for (uint32_t i = 0; i < n; ++i) {
    uint32_t a = BE32(base, d + 4 * i);
    if (!InHeap(a) || a == p || a == pl) continue;
    if (std::find(s_taken.begin(), s_taken.end(), a) != s_taken.end()) continue;
    float dx = getf(a + 0x1C0) - px, dy = getf(a + 0x1C4) - py, dz = getf(a + 0x1C8) - pz;
    float d2 = dx * dx + dy * dy;
    if (d2 >= 230.0f * 230.0f || std::fabs(dz) > 180.0f) continue;
    bool pick = false; std::string first, chain;
    for (uint32_t c = BE32(base, a + 0x2C), k = 0; c && HeapOrImg(c) && k < 12; c = BE32(base, c + 0x38), ++k) {
      std::string nm = FNameStr(ctx, base, c, sp);
      if (k == 0) first = nm;
      if (k > 0 && k < 4) chain += "<" + nm;
      if (nm == "Pickup" || nm.find("PickupItem") != std::string::npos || nm.find("Ammo") != std::string::npos ||
          nm.find("Money") != std::string::npos || nm.find("Cash") != std::string::npos) { pick = true; break; }
    }
    if (!pick) near.push_back({d2, first + chain});
    if (d2 >= bestd) continue;
    if (pick) { best = a; bestd = d2; bestcn = first; }
  }
  if (!best) {
    std::sort(near.begin(), near.end());
    std::string list;
    for (size_t i = 0; i < near.size() && i < 6; ++i) list += fmt::format(" | {} ({:.0f})", near[i].second, std::sqrt(near[i].first));
    Log("P2 use: nothing to pick up within reach");
    REXLOG_INFO("bioshock-recomp: give: P2 use: in reach but not pickups:{}", list.empty() ? " (nothing)" : list);
    return;
  }
  // Item class: a property naming it, else the actor's own class. Count: first sane int property.
  uint32_t item_cls = BE32(base, best + 0x2C), count = 1, o = 0, m = 0;
  std::string how = "own class", cnt_how = "default 1";
  for (const char* nm : {"InventoryType", "ItemClass", "InventoryClass", "PickupClass", "ItemType"}) {
    if (FindPropOffset(ctx, base, best, nm, &o, &m)) {
      uint32_t v = BE32(base, best + o);
      if (InHeap(v) || (v >= 0x82000000 && v < 0x83000000)) { item_cls = v; how = nm; break; }
    }
  }
  for (const char* nm : {"StackCount", "Count", "Quantity", "Amount", "NumItems", "AmmoAmount", "StackSize"}) {
    if (FindPropOffset(ctx, base, best, nm, &o, &m) && !m) {
      uint32_t v = BE32(base, best + o);
      if (v >= 1 && v <= 999) { count = v; cnt_how = nm; break; }
    }
  }
  uint32_t got = count;
  // World pickups (EVEHypoPickup, PistolBulletPickup, ...) are spawners, not items: resolve the item
  // class from the name. Food/drink is eaten on the spot (small heal for Jack); story items stay.
  std::string stem = bestcn;
  if (stem.size() > 6 && stem.compare(stem.size() - 6, 6, "Pickup") == 0) stem.resize(stem.size() - 6);
  auto has = [&](const char* k) { return bestcn.find(k) != std::string::npos; };
  if (has("Adam") || has("Log") || has("Diary") || has("Key") || has("Quest")) {
    Log(fmt::format("P2 use: {} is a story item - Jack has to pick that one up", bestcn));
    return;
  }
  bool given = false;
  if (how == "own class") {
    std::vector<std::string> cands;
    if (has("EVE") || has("BioAmmo")) cands.push_back("BioAmmoHypo");
    if (has("FirstAid") || has("MedKit") || has("MedHypo") || has("Health")) cands.push_back("MedHypo");
    cands.push_back(stem); cands.push_back(stem + "PickupItem"); cands.push_back(stem + "Item");
    for (const std::string& c : cands) {
      uint32_t k = FindClassByName(ctx, base, c);
      if (!k) continue;
      got = count;
      if (GiveItemClass(ctx, base, k, &got)) { given = true; how = "name -> " + c; break; }
    }
  }
  if (!given) given = GiveItemClass(ctx, base, item_cls, &got);
  static const char* kFood[] = {"PowerBar", "Whisky", "Whiskey", "Cigarette", "Chips", "Beer", "Coffee", "Soda",
                                "Bread", "Wine", "Peanut", "Pep", "Apple", "Banana", "Cake", "Gin", "Rum", "Vodka",
                                "Cereal", "Food", "Drink", "Pie", "Liquor", "Brandy", "Sardine", "Potato"};
  bool food = false;
  for (const char* f : kFood) food = food || has(f);
  if (!given && food && InHeap(pl)) {
    // Eaten: +5 health to Jack (AShockPawn::AddHealth = vtable+912, f1 amount, r5 flag).
    PPCContext saved = ctx;
    ctx.r1.u64 = (ctx.r1.u32 - 0x4000) & ~0xFu; ctx.r3.u64 = pl; ctx.r5.u64 = 0; ctx.f1.f64 = 5.0;
    if (PPCFunc* f = rex::runtime::ResolveIndirectFunction(BE32(base, BE32(base, pl) + 912))) f(ctx, base);
    ctx = saved;
    given = true; how = "eaten (+5 health for Jack)"; got = 1;
  }
  if (!given) {
    static std::vector<uint32_t> s_dumped;  // one property dump per pickup class (recon)
    uint32_t pc = BE32(base, best + 0x2C);
    if (std::find(s_dumped.begin(), s_dumped.end(), pc) == s_dumped.end()) { s_dumped.push_back(pc); DumpProps(ctx, base, best, bestcn.c_str()); }
    Log(fmt::format("P2 use: {} ({:08X}) - Jack's inventory refused it (full, or not an inventory item; class via {})", bestcn, best, how));
    return;
  }
  // Taken: hide it, drop its collision, sink it under the floor so Jack cannot take it again.
  s_taken.push_back(best);
  if (s_taken.size() > 256) s_taken.erase(s_taken.begin());
  SetBool(ctx, base, best, "bHidden", true);
  SetBool(ctx, base, best, "bCollideActors", false);
  SetBool(ctx, base, best, "bBlockActors", false);
  putf(best + 0x1C8, getf(best + 0x1C8) - 4000.0f);
  Log(fmt::format("P2 use: picked up {} x{} for Jack (class via {}, count via {})", bestcn, got, how, cnt_how));
}

void BsTraceFlush();
void BsTickP2(PPCContext& ctx, uint8_t* base) {
  BsTraceFlush();
  uint32_t p = g_p2.load();
  if (!p) return;
  if (!InHeap(p) || BE32(base, p + 0x2C) != g_p2_class.load()) { g_p2 = 0; g_p2_kbd = false; Log("P2: pawn gone"); return; }
  uint16_t b = 0; int16_t lx = 0, ly = 0, rx = 0, ry = 0; uint8_t rt = 0;
  bool attack = false, drill = false, jump = false, slam_btn = false, use = false, shove = false, roar = false, charge_btn = false;
  // Controller Back/View button = swap between Jack and the Big Daddy (like F8).
  {
    static bool s_back = false;
    uint16_t pb0 = 0; int16_t a0, a1, a2, a3; uint8_t t0;
    bool back = !g_panel_open.load() && BsPadPollIndex(0, &pb0, &a0, &a1, &a2, &a3, &t0) && (pb0 & 0x0020);
    if (back && !s_back) { if (BsCoopSplit()) BsPadSwap(); else BsP2ToggleKeyboard(); }
    s_back = back;
  }
  BsCamTick(ctx, base, p);  // camera runs even with the panel open (split view keeps routing)
  if (g_panel_open.load()) return;  // panel open: no P2 input (world is frozen anyway)
  if (g_p2_kbd.load()) {
    ly = int16_t((BsKeyDown('W') ? 32767 : 0) - (BsKeyDown('S') ? 32767 : 0));
    lx = int16_t((BsKeyDown('D') ? 32767 : 0) - (BsKeyDown('A') ? 32767 : 0));
    int turn = (BsKeyDown(0x27) ? 1 : 0) - (BsKeyDown(0x25) ? 1 : 0);  // arrows; E is now Use
    use = BsKeyDown('E');
    shove = BsKeyDown('R'); roar = BsKeyDown('T'); charge_btn = BsKeyDown('C');
    rx = int16_t(std::clamp(turn * 32767 + BsMouseDX() * 2500, -32767, 32767));
    attack = BsKeyDown(0x01);
    drill = BsKeyDown(0x02);
    jump = BsKeyDown(0x20);
    slam_btn = BsKeyDown('F') || BsKeyDown(0x04);
    // Your own controller (pad 0) also drives P2 while you are switched to him: game input is
    // blocked then, so this does not move Jack. Sticks add to WASD/mouse.
    uint16_t pb = 0; int16_t plx = 0, ply = 0, prx = 0, pry = 0; uint8_t prt = 0;
    if ((!BsCoopSplit() || g_pad_p2.load()) && BsPadPollIndex(0, &pb, &plx, &ply, &prx, &pry, &prt)) {  // co-op: pad 0 is Jack's unless swapped
      auto add = [](int16_t a, int16_t b) { return int16_t(std::clamp(int(a) + int(b), -32767, 32767)); };
      lx = add(lx, plx); ly = add(ly, ply); rx = add(rx, prx);
      g_pad_pitch = pry;
      // Pad: RT punch, LT/LB/RB drill, B slam, A jump, L3 run.
      attack = attack || prt > 100;
      drill = drill || (pb & 0x0300) || BsPadLT(0) > 100;
      jump = jump || (pb & 0x1000);
      slam_btn = slam_btn || (pb & 0x2000);
      use = use || (pb & 0x4000);  // X
      shove = shove || (pb & 0x8000);  // Y
      roar = roar || (pb & 0x0001);    // D-pad up
      charge_btn = charge_btn || (pb & 0x0008);  // D-pad right
      b |= pb;
    }
  } else if (BsPadPollIndex(1, &b, &lx, &ly, &rx, &ry, &rt)) {
    attack = rt > 100;
    drill = (b & 0x0300) != 0 || BsPadLT(1) > 100;
    use = (b & 0x4000) != 0;  // X
    shove = (b & 0x8000) != 0;  // Y
    roar = (b & 0x0001) != 0;   // D-pad up
    charge_btn = (b & 0x0008) != 0;  // D-pad right
    jump = (b & 0x1000) != 0;
    slam_btn = (b & 0x2000) != 0;
  } else {
    lx = ly = rx = ry = 0;  // no input device: camera only, leave the AI alone
  }
  bool driving = g_p2_kbd.load() || BsPadPollIndex(1, &b, &lx, &ly, &rx, &ry, &rt);
  auto put = [&](uint32_t a, uint32_t v) { base[a] = uint8_t(v >> 24); base[a + 1] = uint8_t(v >> 16); base[a + 2] = uint8_t(v >> 8); base[a + 3] = uint8_t(v); };
  auto putf = [&](uint32_t a, float f) { uint32_t v; std::memcpy(&v, &f, 4); put(a, v); };
  auto getf = [&](uint32_t a) { uint32_t v = BE32(base, a); float f; std::memcpy(&f, &v, 4); return f; };
  auto fb = [](float f) { uint32_t v; std::memcpy(&v, &f, 4); return uint64_t(v); };
  auto dz = [](int16_t v) { float f = v / 32767.0f; return std::fabs(f) < 0.2f ? 0.0f : f; };
  // Detach the AI only while someone is actually driving, and never in the first 2 s after spawn
  // (the pawn's init still uses its controller: crashed reading controller+0x438 otherwise).
  static uint32_t s_p2_seen = 0; static auto s_p2_t0 = std::chrono::steady_clock::now();
  if (s_p2_seen != p) { s_p2_seen = p; s_p2_t0 = std::chrono::steady_clock::now(); }
  bool settled = std::chrono::steady_clock::now() - s_p2_t0 > std::chrono::seconds(2);
  if (g_p2_autoswitch.load() && settled && !g_p2_kbd.load()) {
    g_p2_autoswitch = false;
    g_p2_kbd = true; BsQueueCommand("viewclass " + g_p2_view); BsQueueCommand("behindview 1");
    Log("P2: you are the Big Daddy now (F8 = back to Jack)");
    if (g_split_default.load()) { BsSplitWindow(true); Log("P2: co-op split on - controller = Jack (window 1), keyboard+mouse = Big Daddy (window 2), F7 = swap windows"); }
    driving = true;
  }
  // Squeeze: while you drive, shrink his collision radius to about Jack's so he fits through doorways and
  // narrow corridors (his model may clip walls a little). Done through the engine's own
  // AActor::SetCollisionSize (sub_821CE930, from execSetCollisionSize 0x822F6F08) so the collision hash
  // is updated properly; restored as soon as you stop driving. Height is kept (shrinking it would sink
  // the model into the floor).
  {
    static uint32_t s_sz_for = 0, s_ro = 0, s_ho = 0;
    static float s_r0 = 0, s_h0 = 0;
    static bool s_small = false;
    if (s_sz_for != p) {
      s_sz_for = p; s_small = false; s_ro = s_ho = 0;
      uint32_t m = 0;
      if (FindPropOffset(ctx, base, p, "CollisionRadius", &s_ro, &m) && FindPropOffset(ctx, base, p, "CollisionHeight", &s_ho, &m)) {
        auto rf = [&](uint32_t a) { uint32_t v = BE32(base, a); float f; std::memcpy(&f, &v, 4); return f; };
        s_r0 = rf(p + s_ro); s_h0 = rf(p + s_ho);
        Log(fmt::format("P2: collision radius {:.0f}, height {:.0f}", s_r0, s_h0));
      } else { s_ro = s_ho = 0; }
    }
    bool want = g_p2_squeeze.load() && driving && settled;
    if (s_ro && s_ho && s_r0 > 0 && want != s_small) {
      float r = s_r0;
      if (want) {
        float jr = 0; uint32_t pl = g_player.load(), jo = 0, m = 0;
        if (InHeap(pl) && FindPropOffset(ctx, base, pl, "CollisionRadius", &jo, &m)) { uint32_t v = BE32(base, pl + jo); std::memcpy(&jr, &v, 4); }
        r = std::min(s_r0, std::max(jr > 1.0f && jr < 200.0f ? jr + 2.0f : s_r0 * 0.55f, 22.0f));
      }
      PPCContext sv = ctx;
      ctx.r1.u64 = (ctx.r1.u32 - 0x5000) & ~0xFu; ctx.r3.u64 = p; ctx.f1.f64 = r; ctx.f2.f64 = s_h0;
      __imp__sub_821CE930(ctx, base);
      ctx = sv;
      s_small = want;
      Log(want ? fmt::format("P2: squeeze on - collision radius {:.0f} -> {:.0f} (fits tight spaces)", s_r0, r)
               : fmt::format("P2: squeeze off - collision radius back to {:.0f}", s_r0));
    }
  }
  if (!driving || !settled) { g_p2_yaw_of = 0; return; }
  {  // Use: E / pad X - pick up the nearest loose item for Jack
    static bool s_use_prev = false;
    if (use && !s_use_prev) BsP2Use(ctx, base, p);
    s_use_prev = use;
  }
  static uint32_t s_p2_ctrl = 0, s_p2_ctrl_of = 0;
  if (s_p2_ctrl_of != p) { s_p2_ctrl_of = p; s_p2_ctrl = BE32(base, p + 0x420); }
  if (g_p2_detach.load()) put(p + 0x420, 0);  // no AI controller fighting the input

  // Turn.
  // His facing is ours: kept here and written every tick, so the AI cannot turn him while you drive.
  static int32_t s_yaw = 0;
  if (g_p2_yaw_of.load() != p) { g_p2_yaw_of = p; s_yaw = int32_t(BE32(base, p + 0x1CC + 4)); }
  s_yaw += int32_t(dz(rx) * (drill ? 300.0f : 700.0f));  // steadier aim while drilling
  int32_t yaw = s_yaw;
  put(p + 0x1CC, 0); put(p + 0x1CC + 4, uint32_t(yaw)); put(p + 0x1CC + 8, 0);
  put(p + 0x310, 0); put(p + 0x310 + 4, uint32_t(yaw)); put(p + 0x310 + 8, 0);
  float a = yaw * (6.2831853f / 65536.0f), fwd = dz(ly), side = dz(lx);
  if (drill && fwd == 0.0f && side == 0.0f) fwd = 0.35f;  // drilling pushes you slowly into the target
  float cx = std::cos(a), cy = std::sin(a);

  PPCContext saved = ctx;
  uint32_t sp = (ctx.r1.u32 - 0x6000) & ~0xFu;

  // Move: XLevel->MoveActor(actor, Delta, Rotation, Hit, ...) = ULevel vtable+288, the same call
  // AActor::execMove (0x822F7BA0) makes. Handles collision; see the step-up logic below.
  // Big Daddy pace: heavy walk ~210 u/s, lumbering run ~390 u/s (Shift / L3).
  const float kStep = (g_p2_kbd.load() && BsKeyDown(0x10)) || (b & 0x0040) ? 6.5f : 3.5f;
  float dx = (cx * fwd - cy * side) * kStep, dy = (cy * fwd + cx * side) * kStep;
  uint32_t level = BE32(base, p + 228);
  if (InHeap(level) && (fwd != 0.0f || side != 0.0f)) {
    // MoveActor does collision but no stair stepping (that lives in the engine's walk physics, which
    // we bypass). So: 1) flat sweep, 2) if blocked, lift 24 units and sweep again (a step), else undo
    // the lift (a wall), 3) settle onto the floor. Mixing the down-push into the forward sweep made
    // slopes and floor lips eat most of the movement (the "bumps / resistance").
    PPCFunc* mv = rex::runtime::ResolveIndirectFunction(BE32(base, BE32(base, level) + 288));
    auto move = [&](float mx, float my, float mz) {
      if (!mv) return;
      std::memset(base + sp + 80, 0, 0x100);
      put(sp + 108, 1); put(sp + 116, 1);
      putf(sp + 180, 1.0f); put(sp + 184, 0xFFFFFFFFu);  // FCheckResult at sp+144: Time=1, Item=-1
      ctx.r1.u64 = sp; ctx.r3.u64 = level; ctx.r4.u64 = p;
      ctx.r5.u64 = (fb(mx) << 32) | fb(my);
      ctx.r6.u64 = fb(mz) << 32;
      ctx.r7.u64 = (uint64_t(BE32(base, p + 0x1CC)) << 32) | uint32_t(yaw);
      ctx.r8.u64 = uint64_t(BE32(base, p + 0x1CC + 8)) << 32;
      ctx.r9.u64 = sp + 144; ctx.r10.u64 = 0;
      mv(ctx, base);
      ctx = saved;
    };
    float bx = getf(p + 0x1C0), by = getf(p + 0x1C4);
    auto progress = [&] { float mx = getf(p + 0x1C0) - bx, my = getf(p + 0x1C4) - by; return std::sqrt(mx * mx + my * my); };
    float want = std::sqrt(dx * dx + dy * dy);
    move(dx, dy, 0.0f);
    bool stepped = false;
    if (progress() < want * 0.6f) {
      float px = getf(p + 0x1C0), py = getf(p + 0x1C4), pz = getf(p + 0x1C8);
      move(0.0f, 0.0f, 24.0f);
      move(dx - (px - bx), dy - (py - by), 0.0f);
      if (progress() < want * 0.6f) { putf(p + 0x1C0, px); putf(p + 0x1C4, py); putf(p + 0x1C8, pz); }
      else stepped = true;
    }
    move(0.0f, 0.0f, stepped ? -30.0f : -6.0f);
    // Truly stuck (no progress for ~1/3 s while pushing): nudge through, e.g. wedged on a corpse.
    static int s_stuck = 0;
    s_stuck = progress() < 0.05f ? s_stuck + 1 : 0;
    if (s_stuck > 20) { putf(p + 0x1C0, bx + dx); putf(p + 0x1C4, by + dy); s_stuck = 0; }
    static int s_logged = 0;
    if (s_logged < 3 || (stepped && s_logged < 6)) {
      ++s_logged;
      Log(fmt::format("P2 move: wanted {:.1f} moved {:.1f}{}", want, progress(), stepped ? " (stepped up)" : ""));
    }
  }
  {
    static auto s_mt = std::chrono::steady_clock::now();
    if (std::chrono::steady_clock::now() - s_mt > std::chrono::seconds(3)) {
      s_mt = std::chrono::steady_clock::now();
      Log(fmt::format("P2 move: pos {:.0f},{:.0f},{:.0f} input fwd {:.2f} side {:.2f} kbd {} coop {}", getf(p + 0x1C0), getf(p + 0x1C4),
                      getf(p + 0x1C8), fwd, side, g_p2_kbd.load(), BsCoopSplit()));
    }
  }
  // Jump: Space / A. Big Daddies have no jump, so this is a scripted arc on Z (~110 units high,
  // ~0.75 s); forward movement continues in the air.
  {
    static bool s_air = false, s_jprev = false; static float s_z0 = 0, s_t = 0;
    if (jump && !s_jprev && !s_air) { s_air = true; s_z0 = getf(p + 0x1C8); s_t = 0; PlaySlot(ctx, base, p, "BO_jumpBack_A", 2, 1); }
    s_jprev = jump;
    if (s_air) {
      s_t += 1.0f / 60.0f;
      const float T = 0.75f, H = 110.0f;
      float k = s_t / T;
      if (k >= 1.0f) { s_air = false; putf(p + 0x1C8, s_z0); }
      else putf(p + 0x1C8, s_z0 + 4.0f * H * k * (1.0f - k));
      putf(p + 0x1D8 + 8, 0.0f);
    }
  }
  // Walk cycle (names found by the name scan, 2026-11-01): BO_walk{FWD,BWD,LEFT,RIGHT}_{idle,agg}.
  // Pick by stick direction; "_agg" (aggressive) while the drill is held. Loop on channel 0.
  {
    static std::string s_cur;
    std::string want;
    if (fwd != 0.0f || side != 0.0f) {
      const char* dir = std::fabs(fwd) >= std::fabs(side) ? (fwd > 0 ? "FWD" : "BWD") : (side > 0 ? "RIGHT" : "LEFT");
      want = std::string("BO_walk") + dir + (drill ? "_agg" : "_idle");
      if (!g_walk_anim.empty()) want = g_walk_anim;  // manual override via p2walk
    } else {
      want = "Fidget1";
    }
    if (want != s_cur) { s_cur = want; PlaySlot(ctx, base, p, want.c_str(), 0, 8); }
  }
  {
    // Ally = the game's own hypnotized-Big-Daddy state (props + enum probe, 2026-11-01):
    // DispositionToPlayer +0x1070 (0 none, 1 hostile, 2 friendly), bProtectingPlayer,
    // EndProtectingPlayerTime +0x1078 (pushed far into the future so it never wears off).
    static int s_ally = -1; static uint32_t s_ally_pawn = 0;
    bool want = g_p2_ally.load();
    if (want) {
      base[p + 0x1070] = 2;
      putf(p + 0x1078, 1.0e9f);
    }
    if (int(want) != s_ally || s_ally_pawn != p) {
      s_ally = want; s_ally_pawn = p;
      if (!want) base[p + 0x1070] = 0;
      SetBool(ctx, base, p, "bProtectingPlayer", want);
      Log(want ? "P2: hypnotized - friendly to you, protecting you" : "P2: ally state off");
    }
  }
  // Splicers attack P2: they ignore Big Daddies unless provoked, even hypnotized ones (setting
  // AIController.Enemy was ignored). Every 0.5 s, tell each nearby splicer (AEcologyFighter) once,
  // via AddForcedEnemy, that P2 is its enemy. Map is keyed by pawn address: re-told if P2 changes.
  if (g_p2_ally.load()) {
    static auto s_agg = std::chrono::steady_clock::now();
    if (std::chrono::steady_clock::now() - s_agg > std::chrono::milliseconds(500)) {
      s_agg = std::chrono::steady_clock::now();
      uint32_t d = 0, n = 0, pl = g_player.load(), cnt = 0;
      uint32_t sp2 = (ctx.r1.u32 - 0x5000) & ~0xFu;
      if (pl && FindActorList(base, pl, &d, &n)) {
        float px = getf(p + 0x1C0), py = getf(p + 0x1C4);
        for (uint32_t i = 0; i < n; ++i) {
          uint32_t o = BE32(base, d + 4 * i);
          if (!InHeap(o) || o == p || o == pl || !(getf(o + 1356) > 0.0f)) continue;
          float ox = getf(o + 0x1C0) - px, oy = getf(o + 0x1C4) - py;
          if (ox * ox + oy * oy > 2000.0f * 2000.0f) continue;
          uint32_t cls = BE32(base, o + 0x2C);
          if (!ShouldDouble(ctx, base, cls, sp2)) continue;  // AI pawns only
          std::string cn = FNameStr(ctx, base, cls, sp2);
          if (cn.find("Bouncer") != std::string::npos || cn.find("Rosie") != std::string::npos) continue;
          // Only EcologyFighter subclasses (splicers) have the forced-enemy list.
          static std::unordered_map<uint32_t, bool> s_eco;
          auto it = s_eco.find(cls);
          if (it == s_eco.end()) {
            bool eco = false;
            for (uint32_t c2 = cls, k = 0; c2 && HeapOrImg(c2) && k < 16; c2 = BE32(base, c2 + 0x38), ++k)
              if (FNameStr(ctx, base, c2, sp2) == "EcologyFighter") { eco = true; break; }
            it = s_eco.emplace(cls, eco).first;
            Log(fmt::format("P2: {} is {}an EcologyFighter", cn, eco ? "" : "not "));
          }
          if (!it->second) continue;
          static std::unordered_map<uint32_t, uint32_t> s_forced;  // splicer -> P2 it was told about
          if (s_forced[o] == p) continue;
          s_forced[o] = p;
          PPCContext sv = ctx;
          ctx.r1.u64 = sp2; ctx.r3.u64 = o; ctx.r4.u64 = p;
          __imp__sub_825DB088(ctx, base);
          ctx = sv;
          ++cnt;

        }
      }
      if (cnt) Log(fmt::format("P2: {} splicers told your Big Daddy is their enemy", cnt));
    }
  }
  {
    static bool s_hidden = false;
    bool want_hidden = g_p2_kbd.load() && !BsCoopSplit();  // co-op: Jack is a player, keep him visible
    if (want_hidden != s_hidden) { s_hidden = want_hidden; HideJack(ctx, base, s_hidden); }
  }
  // Experimental: Jack has no third-person body (first-person hands only). Borrow a Splicer's
  // mesh for his pawn so the Big Daddy's view shows him; bOwnerNoSee keeps it out of Jack's own view.
  // No matching animations: expect a stiff pose that slides around.
  {
    static uint32_t s_body_for = 0; static int s_body_tries = 0;
    uint32_t pl = g_player.load();
    if (g_jack_body.load() && BsCoopSplit() && InHeap(pl) && s_body_for != pl && s_body_tries < 3) {
      ++s_body_tries;
      uint32_t mo = 0, mm = 0, d = 0, n = 0, mesh = 0;
      uint32_t sp = (ctx.r1.u32 - 0x5000) & ~0xFu;
      bool has_mesh_prop = FindPropOffset(ctx, base, pl, "Mesh", &mo, &mm);
      if (has_mesh_prop && FindActorList(base, pl, &d, &n)) {
        for (uint32_t i = 0; i < n && !mesh; ++i) {
          uint32_t a = BE32(base, d + 4 * i);
          if (!InHeap(a) || a == pl || a == p) continue;
          uint32_t c = BE32(base, a + 0x2C);
          if (!HeapOrImg(c)) continue;
          std::string cn = FNameStr(ctx, base, c, sp);
          if (cn.find("Thug") == std::string::npos && cn.find("Aggressor") == std::string::npos &&
              cn.find("Splicer") == std::string::npos) continue;
          uint32_t o2 = 0, m2 = 0;
          if (FindPropOffset(ctx, base, a, "Mesh", &o2, &m2) && HeapOrImg(BE32(base, a + o2))) {
            mesh = BE32(base, a + o2);
            Log(fmt::format("Jack body: using the mesh of {} {:08X}", cn, a));
          }
        }
      }
      if (mesh) {
        put(pl + mo, mesh);
        uint32_t dto = 0, dtm = 0;
        if (FindPropOffset(ctx, base, pl, "DrawType", &dto, &dtm)) base[pl + dto] = 2;  // DT_Mesh
        SetBool(ctx, base, pl, "bOwnerNoSee", true);
        SetBool(ctx, base, pl, "bHidden", false);
        s_body_for = pl;
        Log(fmt::format("Jack body: applied (Mesh +{:X}, DrawType {})", mo, dto ? "set" : "not found"));
      } else {
        Log(has_mesh_prop ? "Jack body: no Splicer with a mesh in this level yet" : "Jack body: Jack's pawn has no Mesh property");
      }
    }
    if (!g_jack_body.load()) s_body_tries = 0;
  }
  // Keep Jack trailing P2 while you play as P2: the engine only fully simulates/streams around
  // the player, so a far-away P2 stopped responding. Jack is parked 250 units behind P2.
  if (g_p2_kbd.load() && !BsCoopSplit()) {  // co-op: Jack is played by the controller, leave him be
    uint32_t pl = g_player.load();
    if (InHeap(pl)) {
      // Closer (120) so Jack is not parked off a ledge; same floor height as P2.
      putf(pl + 0x1C0, getf(p + 0x1C0) - cx * 120.0f);
      putf(pl + 0x1C4, getf(p + 0x1C4) - cy * 120.0f);
      putf(pl + 0x1C8, getf(p + 0x1C8));
      putf(pl + 0x1D8, 0.0f); putf(pl + 0x1D8 + 4, 0.0f); putf(pl + 0x1D8 + 8, 0.0f);  // no fall speed
      // Jack is invisible and can't fight back while you are P2: keep him at full health.
      static auto s_heal = std::chrono::steady_clock::now();
      if (std::chrono::steady_clock::now() - s_heal > std::chrono::milliseconds(300)) {
        s_heal = std::chrono::steady_clock::now();
        float hp = getf(pl + 1356), mh = getf(pl + 1876);
        if (mh > 0 && hp < mh) putf(pl + 1356, mh);
      }
    }
  }
  // Silence the AI brain: its native code (not script) still started animations, so blocking the
  // script path was not enough. With Controller.Pawn = None the controller stops driving him, while
  // Pawn.Controller stays set (hit reactions need it - nulling that one crashed). Restored on F8.
  {
    static uint32_t s_ctl = 0, s_ctl_pawn_off = 0, s_silenced_pawn = 0;
    bool want = g_p2_silence.load() && g_p2_kbd.load();
    uint32_t ctl = BE32(base, p + 0x420);
    if (want && !s_silenced_pawn && InHeap(ctl)) {
      uint32_t o, m;
      if (FindPropOffset(ctx, base, ctl, "Pawn", &o, &m) && BE32(base, ctl + o) == p) {
        s_ctl = ctl; s_ctl_pawn_off = o; s_silenced_pawn = p; put(ctl + o, 0);
        Log("P2: AI brain silenced (you have full control)");
      }
    } else if (!want && s_silenced_pawn) {
      if (InHeap(s_ctl) && BE32(base, s_ctl + s_ctl_pawn_off) == 0) put(s_ctl + s_ctl_pawn_off, s_silenced_pawn);
      s_silenced_pawn = 0;
      Log("P2: AI brain back on");
    }
  }
  // AI steering: animations are blocked (counter in the log), but the AI also turns him toward
  // enemies (physics RotationRate toward its focus) and walks him (GroundSpeed). Zero both while
  // you drive; our movement uses MoveActor and direct rotation writes, which ignore them.
  {
    static uint32_t s_for = 0; static float s_gs = 0; static uint32_t s_rr[3] = {}; static bool s_on = false;
    uint32_t go = 0, ro = 0, m = 0;
    bool want = g_p2_kbd.load();
    if (want != s_on || s_for != p) {
      bool okg = FindPropOffset(ctx, base, p, "GroundSpeed", &go, &m);
      bool okr = FindPropOffset(ctx, base, p, "RotationRate", &ro, &m);
      if (want) {
        if (okg) { s_gs = getf(p + go); putf(p + go, 0.0f); }
        if (okr) for (int i = 0; i < 3; ++i) { s_rr[i] = BE32(base, p + ro + 4 * i); put(p + ro + 4 * i, 0); }
        Log("P2: AI steering off (GroundSpeed/RotationRate 0)");
      } else if (s_for == p) {
        if (okg) putf(p + go, s_gs);
        if (okr) for (int i = 0; i < 3; ++i) put(p + ro + 4 * i, s_rr[i]);
      }
      s_on = want; s_for = p;
    }
  }
  // You are in charge: while driven, the AI may not move him or attack on its own.
  {
    static uint32_t s_hold_of = 0; static float s_hx = 0, s_hy = 0;
    if (g_p2_kbd.load() || driving) {
      if (s_hold_of != p || fwd != 0.0f || side != 0.0f) { s_hold_of = p; s_hx = getf(p + 0x1C0); s_hy = getf(p + 0x1C4); }
      else { putf(p + 0x1C0, s_hx); putf(p + 0x1C4, s_hy); }  // idle: stay exactly where you left him
    }
    // Blind his AI: no Enemy / Focus / MoveTarget means nothing to chase, face or attack.
    // Offsets cached per controller class (the lookup walks the class fields).
    if (g_p2_blind.load()) {
      static uint32_t s_ccls = 0, s_off[3] = {};
      uint32_t ctl = BE32(base, p + 0x420);
      if (InHeap(ctl)) {
        uint32_t cc = BE32(base, ctl + 0x2C);
        if (cc != s_ccls) {
          s_ccls = cc;
          const char* names[3] = {"Enemy", "Focus", "MoveTarget"};
          std::string found;
          for (int i = 0; i < 3; ++i) {
            uint32_t o = 0, m = 0;
            s_off[i] = FindPropOffset(ctx, base, ctl, names[i], &o, &m) ? o : 0;
            found += fmt::format(" {}=+{:X}", names[i], s_off[i]);
          }
          Log("P2: AI blinded (cleared every tick):" + found);
        }
        for (uint32_t o : s_off) if (o) put(ctl + o, 0);
      }
    }
    static bool s_nofire = false;
    bool want_nofire = g_p2_kbd.load();
    if (want_nofire != s_nofire) { s_nofire = want_nofire; SetBool(ctx, base, p, "bNoWeaponFiring", want_nofire); }
  }
  // Locomotion: the pawn's movement anims (MovementAnims +0x600) blend from Velocity/Acceleration.
  // Write both every tick (zero when idle) so the walk cycle plays instead of sliding.
  putf(p + 0x1D8, dx * 60.0f); putf(p + 0x1D8 + 4, dy * 60.0f);
  putf(p + 0x1E4, dx * 60.0f * 4.0f); putf(p + 0x1E4 + 4, dy * 60.0f * 4.0f); putf(p + 0x1E4 + 8, 0.0f);

  // Attack: left mouse / RT. Hits every pawn within 250 units in front of P2 for 60 damage
  // (AShockPawn::RemoveHealth = vtable+916, f1 = amount). 0.7 s cooldown.
  static auto s_last = std::chrono::steady_clock::now();
  auto now = std::chrono::steady_clock::now();
  // Right mouse / RB: drill spin (loops while held, damages a little every 0.25 s).
  static bool s_drilling = false;
  // Anim names/channels recorded from an AI Big Daddy (2026-11-01): melee BO_AttackMelee_F/H/I/K
  // and BO_AttackShoulderSlamLONG on channel 2 flag 1; drill spin BO_DrillSpin channel 3 flag 8 (loop).
  if (drill && !s_drilling) { s_drilling = true; uint32_t h = PlaySlot(ctx, base, p, "BO_DrillSpin", 3, 8);
    put(p + 0x12A4, h);  // DrillSpinAnimationHandle: the Bouncer keeps the spin's handle here
    Log(fmt::format("P2: drill on (spin handle {:08X})", h)); }
  if (!drill && s_drilling) { s_drilling = false; PlaySlot(ctx, base, p, "BO_AggToIdle", 3, 1); }
  static bool s_slam_prev = false;
  bool slam = slam_btn;  // F / middle mouse / LB
  // Real weapon hits fire part-way into the animation (wind-up), like the AI.
  static std::chrono::steady_clock::time_point s_pending_t{};
  static uint32_t s_pending_slot = 0; static const char* s_pending_attack = nullptr;
  if (s_pending_attack && now >= s_pending_t) {
    // weapon needs the AI controller present for target/knockback bookkeeping
    if (InHeap(s_p2_ctrl)) put(p + 0x420, s_p2_ctrl);
    // Snapshot health of nearby AI pawns, fire, compare: shows whether the real weapon connected.
    std::vector<std::pair<uint32_t, float>> near_hp;
    {
      uint32_t d = 0, n = 0, pl = g_player.load();
      if (pl && FindActorList(base, pl, &d, &n)) {
        float px = getf(p + 0x1C0), py = getf(p + 0x1C4);
        for (uint32_t i = 0; i < n; ++i) {
          uint32_t o = BE32(base, d + 4 * i);
          if (!InHeap(o) || o == p || o == pl) continue;
          float ox = getf(o + 0x1C0) - px, oy = getf(o + 0x1C4) - py;
          if (ox * ox + oy * oy < 400.0f * 400.0f && getf(o + 1356) > 0.0f) near_hp.push_back({o, getf(o + 1356)});
        }
      }
    }
    bool ok = FireBDWeapon(ctx, base, p, s_pending_slot, s_pending_attack);
    std::string res;
    for (auto& [o, hp] : near_hp) {
      float now_hp = getf(o + 1356);
      if (now_hp != hp) res += fmt::format(" {:08X} {:.0f}->{:.0f}", o, hp, now_hp);
    }
    Log(fmt::format("P2: weapon '{}' {} | {} nearby, damaged:{}", s_pending_attack, ok ? "fired" : "missing",
                    near_hp.size(), res.empty() ? " none" : res));
    s_pending_attack = nullptr;
  }
  // Charge: B while running (L3 / Shift held), or C. 0.8 s rush at ~3x walk speed, hand-weapon hit
  // every 0.2 s on whatever is in front. Slam stays on B / F when not running.
  const bool running = (g_p2_kbd.load() && BsKeyDown(0x10)) || (b & 0x0040);
  static std::chrono::steady_clock::time_point s_charge_end{}, s_charge_hit{};
  static bool s_charge_prev = false;
  bool charge_req = charge_btn || (slam && running);
  if (charge_req && !s_charge_prev && now > s_charge_end && now - s_last > std::chrono::milliseconds(900)) {
    s_charge_end = now + std::chrono::milliseconds(800);
    s_last = now;
    PlaySlot(ctx, base, p, "BO_walkFWD_agg", 0, 8);
    Log("P2: charge");
  }
  s_charge_prev = charge_req;
  if (now < s_charge_end) {
    // forward push on top of normal movement (MoveActor handles walls)
    PPCFunc* mv = rex::runtime::ResolveIndirectFunction(BE32(base, BE32(base, BE32(base, p + 228)) + 288));
    if (mv && InHeap(BE32(base, p + 228))) {
      std::memset(base + sp + 80, 0, 0x100);
      put(sp + 108, 1); put(sp + 116, 1); putf(sp + 180, 1.0f); put(sp + 184, 0xFFFFFFFFu);
      ctx.r1.u64 = sp; ctx.r3.u64 = BE32(base, p + 228); ctx.r4.u64 = p;
      ctx.r5.u64 = (fb(cx * 8.0f) << 32) | fb(cy * 8.0f); ctx.r6.u64 = 0;
      ctx.r7.u64 = (uint64_t(BE32(base, p + 0x1CC)) << 32) | uint32_t(yaw);
      ctx.r8.u64 = uint64_t(BE32(base, p + 0x1CC + 8)) << 32;
      ctx.r9.u64 = sp + 144; ctx.r10.u64 = 0;
      mv(ctx, base);
      ctx = saved;
    }
    if (now - s_charge_hit > std::chrono::milliseconds(200)) {
      s_charge_hit = now;
      FireBDWeapon(ctx, base, p, 0x1284, "BigLeftHook");
    }
    slam = false;  // B was used for the charge
  }
  // Shove: Y / R. Push-back animation, hand-weapon hit, and a knockback impulse on enemies in front.
  static bool s_shove_prev = false;
  if (shove && !s_shove_prev && now - s_last > std::chrono::milliseconds(900)) {
    s_last = now;
    static int s_sv = 0;
    PlaySlot(ctx, base, p, (s_sv++ & 1) ? "BO_PushBackSweep" : "BO_PushBack", 2, 1);
    s_pending_slot = 0x1284; s_pending_attack = "BigLeftHook";
    s_pending_t = now + std::chrono::milliseconds(400);
    uint32_t d = 0, n = 0, pl = g_player.load(), pushed = 0;
    if (pl && FindActorList(base, pl, &d, &n)) {
      float px = getf(p + 0x1C0), py = getf(p + 0x1C4);
      for (uint32_t i = 0; i < n; ++i) {
        uint32_t o = BE32(base, d + 4 * i);
        if (!InHeap(o) || o == p || o == pl || !(getf(o + 1356) > 0.0f)) continue;
        float ox = getf(o + 0x1C0) - px, oy = getf(o + 0x1C4) - py, dd = std::sqrt(ox * ox + oy * oy);
        if (dd < 1.0f || dd > 260.0f || (ox * cx + oy * cy) / dd < 0.2f) continue;
        putf(o + 0x1D8, ox / dd * 900.0f); putf(o + 0x1D8 + 4, oy / dd * 900.0f); putf(o + 0x1D8 + 8, 300.0f);
        ++pushed;
      }
    }
    Log(fmt::format("P2: shove ({} pushed)", pushed));
  }
  s_shove_prev = shove;
  // Roar: D-pad up / T. Threaten animations, no damage.
  static bool s_roar_prev = false;
  if (roar && !s_roar_prev && now - s_last > std::chrono::milliseconds(900)) {
    s_last = now;
    static const char* kRoar[] = {"BO_ThreatenA", "BO_Threaten_B", "BO_Threaten_D", "BO_insult_A"};
    static int s_r = 0;
    PlaySlot(ctx, base, p, kRoar[s_r++ & 3], 2, 1);
    Log("P2: roar");
  }
  s_roar_prev = roar;
  bool slam_hit = false;
  if (slam && !s_slam_prev && now - s_last > std::chrono::milliseconds(900)) {
    PlaySlot(ctx, base, p, "BO_AttackShoulderSlamLONG", 2, 1);
    slam_hit = true;
    Log("P2: slam");
  }
  s_slam_prev = slam;
  bool drill_tick = s_drilling && now - s_last > std::chrono::milliseconds(250);
  // Real drill: the AI's spinning drill is BouncerThreeSixtyWeapon (+0x128C) firing 'BigRightHook'
  // (recorded 2026-11-01). Fire it every 0.35 s while right mouse / RB is held.
  static std::chrono::steady_clock::time_point s_drill_t{};
  // Drill = spinning bit (channel 3 loop) + drill-hand strikes. The AI's ThreeSixtyWeapon was a
  // spin attack that hits all around him ("smashing around me"), so use the hand weapon and
  // re-trigger a melee strike animation on the attack channel every 0.8 s while held.
  static std::chrono::steady_clock::time_point s_drill_anim_t{};
  if (s_drilling && now - s_drill_anim_t > std::chrono::milliseconds(1300)) {
    s_drill_anim_t = now;
    static int s_dc = 0;
    PlaySlot(ctx, base, p, (s_dc++ & 1) ? "BO_AttackMelee_K" : "BO_AttackMelee_I", 2, 1);
  }
  if (s_drilling && g_p2_real_weapons.load() && now - s_drill_t > std::chrono::milliseconds(400)) {
    s_drill_t = now;
    if (InHeap(s_p2_ctrl)) put(p + 0x420, s_p2_ctrl);
    float before = 0; uint32_t tgt = 0;
    {
      uint32_t d = 0, n = 0, pl = g_player.load();
      if (pl && FindActorList(base, pl, &d, &n)) {
        float px = getf(p + 0x1C0), py = getf(p + 0x1C4), best = 1e9f;
        for (uint32_t i = 0; i < n; ++i) {
          uint32_t o = BE32(base, d + 4 * i);
          if (!InHeap(o) || o == p || o == pl || !(getf(o + 1356) > 0.0f)) continue;
          float ox = getf(o + 0x1C0) - px, oy = getf(o + 0x1C4) - py, dd = ox * ox + oy * oy;
          if (dd < best && dd < 300.0f * 300.0f) { best = dd; tgt = o; }
        }
      }
      if (tgt) before = getf(tgt + 1356);
    }
    FireBDWeapon(ctx, base, p, 0x1284, "BigRightHook");  // drill hand
    if (tgt && getf(tgt + 1356) != before) Log(fmt::format("P2: drill {:08X} {:.0f}->{:.0f}", tgt, before, getf(tgt + 1356)));
  }
  if (g_p2_real_weapons.load()) drill_tick = false;  // real drill replaces the simple damage
  if ((attack && now - s_last > std::chrono::milliseconds(900)) || drill_tick || slam_hit) {
    s_last = now;
    if (!drill_tick && !slam_hit) {
      static const char* kMelee[] = {"BO_AttackMelee_I", "BO_AttackMelee_K", "BO_AttackMelee_F", "BO_AttackMelee_H"};
      static int s_combo = 0;
      int c = s_combo++ & 3;
      PlaySlot(ctx, base, p, kMelee[c], 2, 1);
      s_pending_slot = 0x1284; s_pending_attack = (c & 1) ? "BigLeftHook" : "BigRightHook";
      s_pending_t = now + std::chrono::milliseconds(450);
    } else if (slam_hit) {
      s_pending_slot = 0x1288; s_pending_attack = g_slam_name.c_str();  // shoulder weapon
      s_pending_t = now + std::chrono::milliseconds(600);
    }
    const bool fake = !(g_p2_real_weapons.load() && !drill_tick);
    const double dmg = drill_tick ? 25.0 : 100.0;
    uint32_t d = 0, n = 0, hits = 0;
    std::string diag;
    uint32_t player = g_player.load();
    if (fake && player && FindActorList(base, player, &d, &n)) {
      float px = getf(p + 0x1C0), py = getf(p + 0x1C4), pz = getf(p + 0x1C8);
      for (uint32_t i = 0; i < n; ++i) {
        uint32_t o = BE32(base, d + 4 * i);
        if (!InHeap(o) || o == p || o == player) continue;
        float ox = getf(o + 0x1C0) - px, oy = getf(o + 0x1C4) - py, oz = getf(o + 0x1C8) - pz;
        float dist2 = ox * ox + oy * oy + oz * oz;
        if (!(dist2 < 350.0f * 350.0f) || dist2 < 1.0f) continue;
        float dist = std::sqrt(dist2), dot = (ox * cx + oy * cy) / dist;
        uint32_t cls = BE32(base, o + 0x2C);
        bool ai = ShouldDouble(ctx, base, cls, sp);
        float hp = getf(o + 1356);
        if (ai && diag.size() < 600)
          diag += fmt::format(" | {} d{:.0f} dot{:.2f} hp{:.0f}", FNameStr(ctx, base, cls, sp), dist, dot, hp);
        if (!ai || !(hp > 0.0f) || dot < -0.2f) continue;
        // Damage stimulus type byte: unknown which one splicers do not resist. Calibrate once:
        // try types until health actually drops, then keep that type. Instigator = P1 so no
        // AI-faction friendly-fire filter applies.
        static int s_dtype = 17;  // found 2026-11-01 (type 17 = melee-ish, splicers take it)
        auto deal = [&](int t, double amount) {
          std::memset(base + sp + 0x1400, 0, 8);  // FName None
          ctx.r1.u64 = sp; ctx.r3.u64 = o; ctx.r4.u64 = uint32_t(t); ctx.f1.f64 = amount; ctx.f2.f64 = 1.0;
          ctx.r5.u64 = 0; ctx.r6.u64 = 0; ctx.r7.u64 = player; ctx.r8.u64 = sp + 0x1400;
          __imp__sub_8250EFB0(ctx, base);
          ctx = saved;
        };
        if (s_dtype < 0) {
          if (InHeap(s_p2_ctrl)) put(p + 0x420, s_p2_ctrl);
          for (int t = 0; t < 24 && s_dtype < 0; ++t) {
            float before = getf(o + 1356);
            deal(t, 1.0);
            if (getf(o + 1356) < before) { s_dtype = t; Log(fmt::format("P2: damage type {} works", t)); }
          }
          if (s_dtype < 0) { Log("P2: no damage type lowered health"); s_dtype = 99; }
        }
        // Kills notify every pawn's sensors (OnOtherPawnDied), which use P2's controller:
        // put it back for the duration of the damage call (null controller -> crash at +0x47C).
        if (InHeap(s_p2_ctrl)) put(p + 0x420, s_p2_ctrl);
        if (s_dtype != 99) deal(s_dtype, dmg);
        if (g_p2_detach.load()) put(p + 0x420, 0);
        Log(fmt::format("P2: hit {:08X} health {:.0f} -> {:.0f}", o, hp, getf(o + 1356)));
        ++hits;
      }
    }
    if (fake && (!drill_tick || hits)) Log(fmt::format("P2: {} , {} hit; pawns in reach:{}", drill_tick ? "drill" : "swing", hits, diag.empty() ? " none" : diag));
  }
}

extern "C" REX_FUNC(sub_822F8B80) {
  if (g_cam_want.load() && !g_in_double && ctx.r4.u32) {
    uint32_t sp_ = (ctx.r1.u32 - 0x5000) & ~0xFu;
    __imp__sub_822F8B80(ctx, base);
    PPCContext after = ctx;
    if (uint32_t a = after.r3.u32; InHeap(a)) {
      uint32_t cls = BE32(base, a + 0x2C);
      std::string cn = FNameStr(ctx, base, cls, sp_);
      if (cn == "Mutator") {
        g_cam_class = cls; g_cam_actor = a; g_cam_want = false;
        Log(fmt::format("split cam: camera actor {:08X}", a));
      }
    }
    ctx = after;
    return;
  }
  if (g_p2_want.load() && !g_in_double && ctx.r4.u32) {
    // Disarm before spawning: a Big Daddy spawns its own parts through this same function, and those
    // inner spawns were briefly captured as P2 (and could be doubled). g_in_double blocks both.
    g_p2_want = false;
    g_in_double = true;
    __imp__sub_822F8B80(ctx, base);
    g_in_double = false;
    if (uint32_t sp_ = ctx.r3.u32) {
      g_p2_class = BE32(base, sp_ + 0x2C); g_p2 = sp_;
      Log(fmt::format("P2: controlling pawn {:08X} (second controller)", sp_));
    } else {
      g_p2_want = true;  // spawn failed (no room): try the next one
    }
    return;
  }
  // Never double the player's Big Daddy (a second D-pad x5 spawn with Double encounters on made a hostile clone).
  if (!g_double.load() || g_in_double || !ctx.r4.u32 || (g_p2_class.load() && ctx.r4.u32 == g_p2_class.load())) {
    __imp__sub_822F8B80(ctx, base); return;
  }
  PPCContext args = ctx;
  __imp__sub_822F8B80(ctx, base);
  uint32_t spawned = ctx.r3.u32;
  if (!spawned) return;
  PPCContext after = ctx;
  uint32_t sp = (args.r1.u32 - 0x5000) & ~0xFu;
  if (!ShouldDouble(ctx, base, args.r4.u32, sp)) { ctx = after; return; }
  // Location of the original (spawned actor +448) plus an offset; try 4 sides until one fits.
  uint32_t loc = sp + 0x1300;
  auto rf = [&](uint32_t a) { uint32_t v = BE32(base, a); float f; std::memcpy(&f, &v, 4); return f; };
  auto wf = [&](uint32_t a, float f) { uint32_t v; std::memcpy(&v, &f, 4); base[a] = v >> 24; base[a + 1] = v >> 16; base[a + 2] = v >> 8; base[a + 3] = v; };
  float x = rf(spawned + 448), y = rf(spawned + 452), z = rf(spawned + 456);
  static const float kOff[][2] = {{90, 0}, {-90, 0}, {0, 90}, {0, -90}, {130, 130}, {-130, -130}};
  g_in_double = true;
  uint32_t clone = 0;
  for (auto& o : kOff) {
    wf(loc, x + o[0]); wf(loc + 4, y + o[1]); wf(loc + 8, z);
    ctx = args;
    ctx.r1.u64 = sp; ctx.r7.u64 = loc;
    __imp__sub_822F8B80(ctx, base);
    if ((clone = ctx.r3.u32)) break;
  }
  g_in_double = false;
  REXLOG_INFO("bioshock-recomp: double: spawned {:08X} -> clone {:08X}", spawned, clone);
  ctx = after;
}

uint32_t BsP2Pawn();
// ---- Attack recorder: watch an AI Big Daddy attack, log the animations and damage calls it uses.
extern "C" void __imp__sub_822F6558(PPCContext& __restrict ctx, uint8_t* base);  // AActor::execPlayAnimationOnChannel
extern "C" void __imp__sub_822FEF30(PPCContext& __restrict ctx, uint8_t* base);  // AActor::execPlayAnimationChainOnChannel
extern "C" void __imp__sub_824F8B08(PPCContext& __restrict ctx, uint8_t* base);  // AWeapon::execInitiateDamage
extern "C" void __imp__sub_824A7B18(PPCContext& __restrict ctx, uint8_t* base);  // UAttackAbility::execInitiateDamage
namespace {
std::atomic<bool> g_rec{false};
std::string ClassOf(PPCContext& ctx, uint8_t* base, uint32_t obj) {
  if (!InHeap(obj)) return "?";
  uint32_t c = BE32(base, obj + 0x2C);
  return HeapOrImg(c) ? FNameStr(ctx, base, c, (ctx.r1.u32 - 0x5000) & ~0xFu) : "?";
}
bool Interesting(const std::string& c) {
  return c.find("Bouncer") != std::string::npos || c.find("Rosie") != std::string::npos ||
         c.find("Protector") != std::string::npos;
}
}  // namespace
void BsSetAttackRecorder(bool on) { g_rec = on; Log(on ? "recorder ON: make an AI Big Daddy attack" : "recorder off"); }
bool BsP2KeyboardActive();
void BsStopAnimOn(PPCContext& ctx, uint8_t* base, uint32_t actor, uint32_t handle);
// ---- Self-learning AI freeze. The actor Tick steps state code through sub_8247E270 (return address
// 0x82302E5C); the hook below records whose state code is running (t_state_owner). If that code then
// tries to animate or swing your Big Daddy while you drive, the owner is logged and, if it belongs to
// him (Owner/Instigator is his pawn or controller, or it is one of them), added to the freeze list.
thread_local uint32_t t_state_owner = 0;
static std::vector<uint32_t> g_frz_learned;
static uint32_t g_frz_for = 0;
static bool IsLearnedFrozen(uint32_t o) {
  if (g_frz_for != g_p2.load()) { g_frz_learned.clear(); g_frz_for = g_p2.load(); }
  return std::find(g_frz_learned.begin(), g_frz_learned.end(), o) != g_frz_learned.end();
}
static void LearnFreeze(PPCContext& ctx, uint8_t* base, const char* why, bool quiet_native = false) {
  uint32_t o = t_state_owner, p2 = g_p2.load();
  if (!o) {
    static int s_native = 0;
    if (!quiet_native && s_native++ < 3) REXLOG_INFO("bioshock-recomp: give: P2: {} on your Big Daddy came from native AI code (no state code): blocked at the native layer", why);
    return;
  }
  if (!p2 || !InHeap(o) || IsLearnedFrozen(o)) return;
  uint32_t pl = g_player.load(), ctl = BE32(base, p2 + 0x420);
  if (o == pl || (pl && o == BE32(base, pl + 0x420))) return;  // never Jack
  static std::vector<uint32_t> s_reported;
  bool related = (o == p2 || o == ctl);
  for (const char* f : {"Owner", "Instigator", "Pawn"}) {
    uint32_t off = 0, m = 0;
    if (!related && FindPropOffset(ctx, base, o, f, &off, &m)) { uint32_t v = BE32(base, o + off); related = (v == p2 || v == ctl); }
  }
  if (std::find(s_reported.begin(), s_reported.end(), o) == s_reported.end()) {
    s_reported.push_back(o);
    uint32_t sp = (ctx.r1.u32 - 0x5000) & ~0xFu;
    REXLOG_INFO("bioshock-recomp: give: P2: state code of {:08X} ({}) did {} on your Big Daddy -> {}", o,
                FNameStr(ctx, base, BE32(base, o + 0x2C), sp), why, related ? "FROZEN while you drive" : "not his, left alone");
  }
  if (related) g_frz_learned.push_back(o);
}

extern "C" REX_FUNC(sub_822F6558) {
  uint32_t self = ctx.r3.u32, sp0 = ctx.r1.u32, resp = ctx.r5.u32;
  bool block = self && self == BsP2Pawn() && BsP2KeyboardActive();
  if (block && !g_our_anim) { Probe("ai.ScriptAnim", base, ctx); PPCContext k = ctx; LearnFreeze(ctx, base, "an animation"); ctx = k; }
  if (!g_rec.load() && !block) { __imp__sub_822F6558(ctx, base); return; }
  __imp__sub_822F6558(ctx, base);
  PPCContext keep = ctx;
  // While you drive P2, his AI's own animations (idle, attacks, hops) would override yours: the
  // script call must still run (it consumes its parameters), so stop the anim it just started.
  if (block && resp) { uint32_t h = BE32(base, resp); BsStopAnimOn(ctx, base, self, h); ctx = keep; base[resp] = base[resp + 1] = base[resp + 2] = base[resp + 3] = 0; }
  if (!g_rec.load()) return;
  std::string c = ClassOf(ctx, base, self);
  if (Interesting(c)) {
    uint32_t fr = sp0 - 144;  // callee frame (stwu -144): channel @+84, FName @+88
    REXLOG_INFO("bioshock-recomp: rec: {}{} {:08X} anim '{}' channel {} flag {}", self == BsP2Pawn() ? "[P2-AI] " : "", c, self,
                FNameStr(ctx, base, fr + 88 - 0x24, (sp0 - 0x6000) & ~0xFu), BE32(base, fr + 84), BE32(base, fr + 80));
  }
  ctx = keep;
}
extern "C" REX_FUNC(sub_822FEF30) {
  uint32_t self = ctx.r3.u32, resp = ctx.r5.u32;
  __imp__sub_822FEF30(ctx, base);
  PPCContext keep = ctx;
  if (self && self == BsP2Pawn() && BsP2KeyboardActive() && resp) {
    BsStopAnimOn(ctx, base, self, BE32(base, resp)); ctx = keep;
  }
  if (!g_rec.load()) return;
  std::string c = ClassOf(ctx, base, self);
  if (Interesting(c)) REXLOG_INFO("bioshock-recomp: rec: {} anim CHAIN", c);
  ctx = keep;
}
extern "C" REX_FUNC(sub_824F8B08) {
  // Our own attacks call the native InitiateDamage directly, so any script call on P2's weapons
  // while you drive is the AI swinging on its own. Count it and log the native target (vtable+740)
  // so it can be hooked and blocked if this shows up.
  if (uint32_t p2 = BsP2Pawn(); p2 && BsP2KeyboardActive() && InHeap(p2)) {
    uint32_t w = ctx.r3.u32;
    if (w && (w == BE32(base, p2 + 0x1284) || w == BE32(base, p2 + 0x1288) || w == BE32(base, p2 + 0x128C))) {
      Probe("ai.SwingExec", base, ctx);
      { PPCContext k = ctx; LearnFreeze(ctx, base, "a weapon swing"); ctx = k; }
      uint32_t n = ++g_ai_swings;
      if (n <= 3 || (n % 20) == 0) {
        uint32_t vt = BE32(base, w);
        REXLOG_INFO("bioshock-recomp: give: P2: AI swing #{} with weapon {:08X} (native InitiateDamage {:08X})", n, w,
                    InHeap(vt) || (vt >= 0x82000000 && vt < 0x83000000) ? BE32(base, vt + 740) : 0);
      }
    }
  }
  if (g_rec.load()) {
    PPCContext keep = ctx;
    uint32_t w = ctx.r3.u32;
    ctx = keep;
    uint32_t sp0 = ctx.r1.u32;
    __imp__sub_824F8B08(ctx, base);
    PPCContext k2 = ctx;
    std::string wc = ClassOf(ctx, base, w), an = FNameStr(ctx, base, sp0 - 128 + 88 - 0x24, (sp0 - 0x6000) & ~0xFu);
    REXLOG_INFO("bioshock-recomp: rec: Weapon.InitiateDamage on {} ({:08X}) attack '{}'", wc, w, an);
    if (wc.find("Shoulder") != std::string::npos && !an.empty() && an != "None" && g_slam_name != an) {
      g_slam_name = an;
    }
    ctx = k2;
    return;
  }
  __imp__sub_824F8B08(ctx, base);
}
extern "C" REX_FUNC(sub_824A7B18) {
  if (g_rec.load()) {
    PPCContext keep = ctx;
    uint32_t a = ctx.r3.u32;
    REXLOG_INFO("bioshock-recomp: rec: AttackAbility.InitiateDamage on {} ({:08X})", ClassOf(ctx, base, a), a);
    ctx = keep;
  }
  __imp__sub_824A7B18(ctx, base);
}

void BsSetVerbose(bool v) { g_verbose = v; }
void BsClearGiveLog() { std::lock_guard<std::mutex> lk(g_gmu); g_give_log.clear(); }

// Lowest-level "play animation" (anim component vtable+8, found at runtime 2026-11-01). Every
// animation - script or native AI - goes through here. While you drive P2, only animations started
// by our input code are allowed on his anim component; the AI's are dropped (handle 0 = "none").
extern "C" void __imp__sub_823509D0(PPCContext& __restrict ctx, uint8_t* base);
uint32_t BsP2Pawn();
bool BsP2KeyboardActive();
extern "C" REX_FUNC(sub_823509D0) {
  if (!g_our_anim) {
    uint32_t p = BsP2Pawn();
    if (p && BsP2KeyboardActive() && ctx.r3.u32 == BE32(base, p + 992)) {
      Probe("ai.NativeAnim", base, ctx);
      static uint32_t s_n = 0;
      if ((++s_n & 15) == 1) REXLOG_INFO("bioshock-recomp: P2 anim block: {} AI anim calls dropped", s_n);
      ctx.r3.u64 = 0; return;
    }
  }
  __imp__sub_823509D0(ctx, base);
}

// AWeapon::InitiateDamage native (vtable+740 of the Big Daddy weapons; found by the AI swing detector,
// 2026-11-02). While you drive P2, only your own attacks (g_our_fire) may swing his weapons.
extern "C" void __imp__sub_824F9EF8(PPCContext& __restrict ctx, uint8_t* base);
extern "C" REX_FUNC(sub_824F9EF8) {
  if (!g_our_fire) {
    uint32_t p2 = BsP2Pawn(), w = ctx.r3.u32;
    if (p2 && BsP2KeyboardActive() && InHeap(p2) && w &&
        (w == BE32(base, p2 + 0x1284) || w == BE32(base, p2 + 0x1288) || w == BE32(base, p2 + 0x128C))) {
      static uint32_t s_n = 0;
      if ((++s_n & 15) == 1) REXLOG_INFO("bioshock-recomp: give: P2: AI swing blocked ({} so far)", s_n);
      ctx.r3.u64 = 0;
      return;
    }
  }
  __imp__sub_824F9EF8(ctx, base);
}

// Inventory / interaction execs (natfind 2026-11-02). Log + backtrace only; behaviour unchanged.
extern "C" void __imp__sub_824BD440(PPCContext& __restrict ctx, uint8_t* base);  // UInventoryManager::execUseItem
extern "C" void __imp__sub_824BCBC8(PPCContext& __restrict ctx, uint8_t* base);  // UInventoryManager::execOnUsedInventoryItem
extern "C" void __imp__sub_824E6698(PPCContext& __restrict ctx, uint8_t* base);  // AShockPlayer::execOnStartedInteractingWithMachine
extern "C" REX_FUNC(sub_824BD440) { Probe("inv.UseItem", base, ctx, "", 6); __imp__sub_824BD440(ctx, base); }
extern "C" REX_FUNC(sub_824BCBC8) { Probe("inv.OnUsedItem", base, ctx, "", 6); __imp__sub_824BCBC8(ctx, base); }
extern "C" REX_FUNC(sub_824E6698) { Probe("use.Machine", base, ctx, "", 6); __imp__sub_824E6698(ctx, base); }

// ---- P2 script tracer. sub_82480E00 = UObject::CallFunction(this r3, FFrame* r4, Result r5,
// UFunction* r6); sub_8247E270 = the bytecode runner native code enters
// (ProcessEvent/ProcessInternal; not hooked, see below). Found with the call-stack probe, 2026-11-02. When on, every
// script call on P2's pawn / AI controller / weapons is counted by name and summarized every 5 s.
std::atomic<bool> g_trace{true};
void BsSetTrace(bool on) { g_trace = on; }
namespace {
std::mutex g_tr_mu;
std::unordered_map<std::string, uint32_t> g_tr_counts;
std::unordered_map<uint32_t, std::string> g_tr_names;     // UFunction* -> name
const char* TraceTag(uint8_t* base, uint32_t obj) {
  uint32_t p2 = g_p2.load();
  if (!p2 || !obj || !InHeap(p2)) return nullptr;
  if (obj == p2) return "pawn";
  if (obj == BE32(base, p2 + 0x420)) return "ai";
  if (obj == BE32(base, p2 + 0x1284) || obj == BE32(base, p2 + 0x1288) || obj == BE32(base, p2 + 0x128C)) return "weapon";
  return nullptr;
}
void TraceNote(PPCContext& ctx, uint8_t* base, const char* site, const char* tag, uint32_t fn) {
  if (!HeapOrImg(fn)) return;
  uint32_t sp = (ctx.r1.u32 - 0x6000) & ~0xFu;
  std::lock_guard<std::mutex> lk(g_tr_mu);
  auto it = g_tr_names.find(fn);
  if (it == g_tr_names.end()) it = g_tr_names.emplace(fn, FNameStr(ctx, base, fn, sp)).first;
  ++g_tr_counts[fmt::format("{} {}.{}", site, tag, it->second)];
}
}  // namespace
void BsTraceFlush() {
  static auto s_t0 = std::chrono::steady_clock::now();
  if (std::chrono::steady_clock::now() - s_t0 < std::chrono::seconds(5)) return;
  s_t0 = std::chrono::steady_clock::now();
  std::vector<std::pair<std::string, uint32_t>> v;
  { std::lock_guard<std::mutex> lk(g_tr_mu); v.assign(g_tr_counts.begin(), g_tr_counts.end()); g_tr_counts.clear(); }
  if (v.empty()) return;
  std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second > b.second; });
  std::string out;
  for (size_t i = 0; i < v.size() && i < 40; ++i) out += fmt::format("\n    {:6} {}", v[i].second, v[i].first);
  REXLOG_INFO("bioshock-recomp: trace P2 (5 s, {} distinct, kbd {}):{}", v.size(), g_p2_kbd.load(), out);
}
extern "C" void __imp__sub_82480E00(PPCContext& __restrict ctx, uint8_t* base);
// AI-decision calls on your Big Daddy while you drive. Each one triggers the self-learning freeze (if the
// running state code belongs to him it gets frozen) and logs one guest call stack per name, so a native
// AI caller can be found and hooked next.
static bool IsAIDecision(const std::string& n) {
  static const char* k[] = {"BecomeAggressive", "BecomePassive", "SetBusy", "SetShouldRun", "PlaySpeech",
                            "RemoveUnreachableTarget", "HandleAIAttackNotification", "IsTargetToAttackOnSight",
                            "BeginFiring", "AddForcedEnemy", "SetDoorPawnIsIn", "InterruptCurrentAction"};
  for (const char* x : k) if (n == x) return true;
  return false;
}
extern "C" REX_FUNC(sub_82480E00) {
  if (g_trace.load()) {
    if (const char* tag = TraceTag(base, ctx.r3.u32)) {
      PPCContext k = ctx;
      uint32_t fn = ctx.r6.u32;
      TraceNote(ctx, base, "call", tag, fn);
      if (BsP2KeyboardActive()) {
        std::string name;
        { std::lock_guard<std::mutex> lk(g_tr_mu); auto it = g_tr_names.find(fn); if (it != g_tr_names.end()) name = it->second; }
        if (!name.empty() && IsAIDecision(name)) {
          Probe((std::string("ai.decision.") + name).c_str(), base, k, "", 2);
          LearnFreeze(ctx, base, name.c_str(), true);
        }
      }
      ctx = k;
    }
  }
  __imp__sub_82480E00(ctx, base);
}
// sub_8247E270 is the bytecode runner. Its tracer hook was removed (guessing whether r4 was a function
// or a frame crashed twice). It IS hooked for one exact case: the actor Tick (sub_82302E20) calls it
// through vtable+20 with the frame time in f1 to step the actor's STATE CODE (latent "attack, wait,
// move" sequences). That call returns to 0x82302E5C. Skipping it for your Big Daddy's AI controller
// while you drive freezes the AI's decision loop (BeginFiring / SetBusy / SetShouldRun come from there)
// without touching script calls, events or params. Address compare only: no lookups.
std::atomic<bool> g_p2_freeze_ai{true}, g_p2_freeze_pawn{true};
void BsP2FreezeAI(bool ctl, bool pawn) { g_p2_freeze_ai = ctl; g_p2_freeze_pawn = pawn; }
extern "C" void __imp__sub_8247E270(PPCContext& __restrict ctx, uint8_t* base);
extern "C" REX_FUNC(sub_8247E270) {
  if (uint32_t(ctx.lr) != 0x82302E5Cu) { __imp__sub_8247E270(ctx, base); return; }
  if (BsP2KeyboardActive() && IsLearnedFrozen(ctx.r3.u32)) {
    static uint32_t s_l = 0;
    if ((++s_l % 600) == 1) REXLOG_INFO("bioshock-recomp: give: P2: learned AI state code frozen ({} ticks skipped)", s_l);
    return;
  }
  if (BsP2KeyboardActive()) {
    uint32_t p2 = g_p2.load(), self = ctx.r3.u32;
    if (p2 && InHeap(p2) && self) {
      const bool is_ctl = self == BE32(base, p2 + 0x420), is_pawn = self == p2;
      // Which object steps state code here (recon: the controller never showed up, 2026-11-03).
      static uint32_t s_seen_ctl = 0, s_seen_pawn = 0;
      if (is_ctl) ++s_seen_ctl;
      if (is_pawn) ++s_seen_pawn;
      // His body's state code also plays death: never freeze it once he is down.
      uint32_t hv = BE32(base, p2 + 1356); float hp; std::memcpy(&hp, &hv, 4);
      if ((g_p2_freeze_ai.load() && is_ctl) || (g_p2_freeze_pawn.load() && is_pawn && hp > 0.0f)) {
        static uint32_t s_n = 0;
        if ((++s_n % 600) == 1)
          REXLOG_INFO("bioshock-recomp: give: P2: AI state code frozen ({} ticks skipped; seen controller {} body {})", s_n,
                      s_seen_ctl, s_seen_pawn);
        return;
      }
    }
  }
  uint32_t prev_owner = t_state_owner;
  t_state_owner = ctx.r3.u32;
  __imp__sub_8247E270(ctx, base);
  t_state_owner = prev_owner;
}

// ---- Native AI think. AActor::Tick (sub_82302F68) calls the AI pawn's virtual tick (vtable+432,
// sub_825786A8), which ends in sub_8258E290(this, f1 dt): perception (sub_8257F5C0, sub_8258D9E8),
// behaviour (sub_82584DE0) and the AI message dispatcher (sub_8250F9E8) that fires BecomeAggressive /
// SetShouldRun / BeginFiring / AddForcedEnemy into script (decision probes, 2026-11-03). Skipped for
// your Big Daddy while you drive; runs normally at 0 health so death is handled.
std::atomic<bool> g_p2_freeze_think{true};
void BsP2FreezeThink(bool on) { g_p2_freeze_think = on; }
extern "C" void __imp__sub_8258E290(PPCContext& __restrict ctx, uint8_t* base);
extern "C" REX_FUNC(sub_8258E290) {
  if (g_p2_freeze_think.load() && BsP2KeyboardActive()) {
    uint32_t p2 = g_p2.load();
    if (p2 && ctx.r3.u32 == p2 && InHeap(p2)) {
      uint32_t hv = BE32(base, p2 + 1356); float hp; std::memcpy(&hp, &hv, 4);
      if (hp > 0.0f) {
        static uint32_t s_n = 0;
        if ((++s_n % 600) == 1) REXLOG_INFO("bioshock-recomp: give: P2: native AI think skipped ({} ticks)", s_n);
        return;
      }
    }
  }
  __imp__sub_8258E290(ctx, base);
}
