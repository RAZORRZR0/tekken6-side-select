// Side swap for player 2 of an online match (Tekken 7 style): his battle is seen from player 1's side - the camera on
// the other side of the fighters - and his controls follow it, so he plays from the left like player 1. Player 1's
// screen stays as it is; tekken6.exe and the DLLs of the launcher's build hash stay as built.
//
// His controls: the game reads a player's pad by his side on the screen (player word +652, set from the camera each
// frame by sub_822A4510): left / right -> forward / back, and the moves' side requirements (sub_8229D3D8) pick the
// sidestep - up is always into the background. From the other side of the fighters that background is the
// foreground, and up still has to be jump / down crouch, so his up / down cannot be swapped; his side has to be,
// in the simulation of both games (GGPO: they simulate the same inputs and states). So the DLLs talk in the
// padding of the GGPO input (byte 15): every one marks its player's input kInputCapable; player 2's, once player 1's
// input carries that mark, marks his input kInputSideTurned while his camera is turned, and both games turn player
// 2's side for each frame whose synchronized input carries it (SyncInput -> sub_822A4510). Against a game without
// this DLL player 2's left / right are swapped in his input instead (his sidesteps stay mirrored).
//
// This is winmm.dll next to tekken6.exe: the exe imports winmm, so this loads before any game code runs, and every
// winmm export is forwarded to the system DLL (winmm.def). DllMain patches the loaded exe. Nothing is found by address
// (the addresses move with every build), everything by signature:
//   - guest functions by their guest address in the recompiler's guest -> host table (PPCFuncMappings): the table entry
//     is redirected (indirect calls: the runtime builds its lookup from the table) and so are the direct calls (E8
//     rel32 to the host function right after the store of lr)
//   - GGPO's Peer2PeerBackend::AddLocalInput: its vtable found by RTTI, the slot checked by the function's log string;
//     SyncInput is the next slot (backends/backend.h)
// Anything not found leaves the game unpatched. side_swap.log next to the exe says what was found; T6_SIDE_SWAP=0 in
// the environment turns it off.
//
// The camera: once a frame the camera manager (sub_8231C488) sets up the views, the active camera first, and the draw
// root (t6_resim.cpp) runs right after. The active camera is turned half round about its up axis through its target
// for its view setup and again for the draw root, and put back after each: the draw (view, culling, lighting) sees one
// consistent turned camera. In between the game decides the side of each player on the screen from the camera (the
// words +0x284..+0x28C of the player objects: game state, the fight goes apart if one side computes them turned), and
// the game's camera logic and the rollback snapshots (frame boundary) come later: they all see it as it is.
//
// The HUD (tk::sprite::battle): its sprites sit at placements {x, y, anchor, flip} of a table (guest 0x82011390,
// read-only XEX data, 110 of them), left / right ones in pairs; the HUD code takes the left or the right one by the
// player's side. While the camera is turned the pairs are swapped, so each player's plate, name, round marks... are
// drawn at the other side in that side's layout. The health gauges (CGaugeTcb_t, one task per side) keep their
// placements 6, 7 and 10 - 13 (their fill offsets +124 / +128 belong to their side: swapped, the fills vanish) and the
// two gauges' values are swapped for the draw root instead.

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr uint32_t kDrawRoot = 0x8221CA68u;       // tcb_drawroot_t: the frame's draw subtree walk and draw pass
constexpr uint32_t kViewSetup = 0x8231CFE0u;      // view from a camera object: eye +96, target +112, up +128
constexpr uint32_t kActiveCameraLr = 0x8231C7FCu; // ... its call for the camera manager's active camera (sub_8231C488)
constexpr uint32_t kEye = 96, kTarget = 112, kUp = 128;
constexpr uint32_t kGameMode = 0x8292E66Cu;       // t6_common.h T6GameMode()
constexpr uint32_t kSideUpdate = 0x822A4510u;     // a fight player's frame: +652 from the camera, then his pad
constexpr uint32_t kPlayerIndex = 76, kInputSide = 652;  // fight player: 0 / 1 (pad 8 / 9); 0 left, 1 right
// rexglue PPCContext: r3 +0x00, lr +0x100 (each call site stores lr there before the call)
constexpr size_t kR3 = 0x00, kLr = 0x100;
// GGPOSession's virtual functions (backends/backend.h, Itanium ABI): ~D1, ~D0, DoPoll, AddPlayer, AddLocalInput,
// SyncInput
constexpr int kAddLocalInputSlot = 4, kSyncInputSlot = 5;
constexpr char kAddLocalInputLog[] = "setting local connect status for local queue";  // p2p.cpp AddLocalInput
constexpr char kP2pTypeName[] = "16Peer2PeerBackend";                                  // its RTTI name
// GGPO input of a player: XINPUT_GAMEPAD in guest byte order + padding (t6_netplay_internal.h kNetInput; the game
// gets the first 12 bytes), the marks in the last byte
constexpr int kNetInput = 16, kMarks = 15;
constexpr uint8_t kInputCapable = 0x01;     // this DLL runs the game of the input's player
constexpr uint8_t kInputSideTurned = 0x02;  // player 2's input: his side is turned
constexpr uint64_t kRecentMs = 2000;  // "running": player 2's input went to GGPO / a turned draw / a sync, this recently
constexpr uint32_t kHudPlacements = 0x82011390u;
constexpr uint32_t kHudX0 = 0x433B0000u, kHudX1 = 0x44882000u;  // x of placements 0 / 1 as built: 187.0, 1089.0
// left / right placement pairs (same y, x mirrored about the middle; not the gauges' 6, 7, 10 - 13; 8 / 9 is the
// low health glow, a task of its own with the side at +132)
constexpr uint8_t kHudPairs[][2] = {
    {0, 1}, {2, 3}, {4, 5}, {8, 9}, {14, 15}, {16, 17}, {18, 19}, {20, 21}, {22, 23}, {25, 26}, {27, 28}, {29, 30},
    {31, 32}, {33, 34}, {35, 37}, {36, 38}, {39, 40}, {41, 42}, {43, 44}, {45, 47}, {46, 48}, {49, 50}, {51, 52},
    {53, 54}, {58, 59}, {60, 61}, {62, 63}, {64, 65}, {66, 67}, {68, 69}, {70, 71}, {72, 73}, {74, 75}, {76, 77},
    {78, 79}, {80, 81}, {82, 83}, {84, 85}, {86, 87}, {88, 89}, {90, 91}, {92, 93}, {94, 95}, {96, 97}, {98, 99},
    {100, 101}, {102, 103}, {104, 105}, {106, 107}, {108, 109}};
constexpr uint32_t kTaskRoot = 0x82828C60u;     // root of the task tree (links +16..+40, t6_hooks.cpp T6_TASKDUMP)
constexpr uint32_t kHeapLo = 0xA7290000u, kHeapHi = 0xABA90000u;  // the game heaps (task objects)
constexpr uint32_t kGaugeVtable = 0x82011C5Cu;  // tk::sprite::battle::CGaugeTcb_t: side +84
constexpr uint32_t kGaugeValues[] = {92, 100, 108};  // what its draw shows: damage trail, fill, fill flash

using GuestFn = void (*)(uint8_t* ctx, uint8_t* base);
using AddLocalInputFn = int (*)(void* self, int player, void* values, int size);
using SyncInputFn = int (*)(void* self, void* values, int size, int* disconnect_flags);

GuestFn g_draw_root, g_view_setup, g_side_update;
AddLocalInputFn g_add_local_input;
SyncInputFn g_sync_input;
std::atomic<uint64_t> g_p2_input_ms{0};  // GetTickCount64 of the last local input of player 2
std::atomic<uint64_t> g_turned_ms{0};    // ... of the last turned camera
std::atomic<uint64_t> g_p1_capable_ms{0};  // ... of the last synchronized input of player 1 marked kInputCapable
std::atomic<uint64_t> g_sync_ms{0};        // ... of the last synchronized input
std::atomic<bool> g_p2_side_turned{false};  // the last synchronized input of player 2 is marked kInputSideTurned
std::atomic<uint8_t*> g_base{nullptr};   // guest memory base (from the view setups)
// (main thread: the view setups and the draw root run there)
uint32_t g_turn_cam = 0;    // guest address of the camera object to turn for this frame's draw root (0: none)
uint32_t g_turned_eye = 0;  // guest address of the turned eye (0: none) and its bytes before
uint8_t g_saved_eye[12];

uint32_t Be32(const uint8_t* p) {
  uint32_t v;
  std::memcpy(&v, p, 4);
  return _byteswap_ulong(v);
}
float BeF(const uint8_t* p) {
  const uint32_t v = Be32(p);
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}
uint32_t U32(const uint8_t* ctx, size_t off) {  // the low word of a 64-bit register / lr
  uint32_t v;
  std::memcpy(&v, ctx + off, 4);
  return v;
}
bool Recent(const std::atomic<uint64_t>& ms) { return GetTickCount64() - ms.load(std::memory_order_relaxed) < kRecentMs; }

// Local player 2 of a running session, in the battle (round call, fight, KO, replay, between rounds: one view, no cut
// between them); every other mode (menus) stays as it is
bool Swapped(const uint8_t* base) {
  if (!base || !Recent(g_p2_input_ms)) return false;
  const uint32_t mode = Be32(base + kGameMode);
  return mode == 3 || mode == 4 || mode == 5 || mode == 6 || mode == 10;
}

void PutBackEye(uint8_t* base) {
  if (g_turned_eye) std::memcpy(base + g_turned_eye, g_saved_eye, 12);
  g_turned_eye = 0;
}

// eye' = target + 2 (d.u) u - d, d = eye - target, u = unit up
void TurnEye(uint8_t* base, uint32_t cam) {
  float e[3], t[3], u[3];
  for (int i = 0; i < 3; i++) {
    e[i] = BeF(base + cam + kEye + 4 * i);
    t[i] = BeF(base + cam + kTarget + 4 * i);
    u[i] = BeF(base + cam + kUp + 4 * i);
  }
  const float len2 = u[0] * u[0] + u[1] * u[1] + u[2] * u[2];
  if (!(len2 > 1e-12f) || !std::isfinite(len2)) return;
  const float d[3] = {e[0] - t[0], e[1] - t[1], e[2] - t[2]};
  const float along = (d[0] * u[0] + d[1] * u[1] + d[2] * u[2]) / len2;
  std::memcpy(g_saved_eye, base + cam + kEye, 12);
  g_turned_eye = cam + kEye;
  for (int i = 0; i < 3; i++) {
    const float v = t[i] + 2.0f * along * u[i] - d[i];
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    bits = _byteswap_ulong(bits);
    std::memcpy(base + g_turned_eye + 4 * i, &bits, 4);
  }
  g_turned_ms.store(GetTickCount64(), std::memory_order_relaxed);
}

void ViewSetupHook(uint8_t* ctx, uint8_t* base) {
  if (U32(ctx, kLr) != kActiveCameraLr) return g_view_setup(ctx, base);
  g_base.store(base, std::memory_order_relaxed);
  g_turn_cam = Swapped(base) ? U32(ctx, kR3) : 0;
  if (g_turn_cam) TurnEye(base, g_turn_cam);
  g_view_setup(ctx, base);
  PutBackEye(base);
}

// Placement pairs swapped (on) or as built. Checked on the table itself: it stays as it is between frames and is no
// game memory (no snapshot brings an older state back); another layout than the known one is left alone.
void SetHudSwapped(uint8_t* base, bool on) {
  uint8_t* t = base + kHudPlacements;
  const uint32_t x0 = Be32(t), x1 = Be32(t + 16);
  const bool built = x0 == kHudX0 && x1 == kHudX1, swapped = x0 == kHudX1 && x1 == kHudX0;
  if ((!built && !swapped) || swapped == on) return;
  DWORD old;
  if (!VirtualProtect(t, 110 * 16, PAGE_READWRITE, &old)) return;
  for (const auto& p : kHudPairs) {
    uint8_t tmp[16];
    std::memcpy(tmp, t + 16 * p[0], 16);
    std::memcpy(t + 16 * p[0], t + 16 * p[1], 16);
    std::memcpy(t + 16 * p[1], tmp, 16);
  }
  VirtualProtect(t, 110 * 16, old, &old);
}

// The two gauge tasks (sides 0, 1) by a walk of the task tree; false unless exactly one of each
bool FindGauges(const uint8_t* base, uint32_t out[2]) {
  static uint32_t seen[8192], todo[8192];  // (open addressing set of the visited nodes, cleared per walk)
  std::memset(seen, 0, sizeof(seen));
  out[0] = out[1] = 0;
  int n = 0, visited = 0;
  todo[n++] = Be32(base + kTaskRoot);
  while (n > 0 && visited < 6000) {
    const uint32_t a = todo[--n];
    if (a < kHeapLo || a >= kHeapHi || (a & 3)) continue;
    uint32_t h = (a >> 2) * 2654435761u >> 19;
    while (seen[h] && seen[h] != a) h = (h + 1) & 8191;
    if (seen[h]) continue;
    seen[h] = a;
    visited++;
    if (Be32(base + a) == kGaugeVtable) {
      const uint32_t side = Be32(base + a + 84);
      if (side > 1 || out[side]) return false;
      out[side] = a;
    }
    for (uint32_t o = 16; o <= 40 && n < 8192; o += 4) todo[n++] = Be32(base + a + o);
  }
  return out[0] && out[1];
}

void SwapGaugeValues(uint8_t* base, const uint32_t g[2]) {
  for (const uint32_t o : kGaugeValues) {
    uint8_t tmp[4];
    std::memcpy(tmp, base + g[0] + o, 4);
    std::memcpy(base + g[0] + o, base + g[1] + o, 4);
    std::memcpy(base + g[1] + o, tmp, 4);
  }
}

void DrawRootHook(uint8_t* ctx, uint8_t* base) {
  const bool turned = g_turn_cam != 0;
  SetHudSwapped(base, turned);
  uint32_t gauges[2];
  const bool swap_gauges = turned && FindGauges(base, gauges);
  if (swap_gauges) SwapGaugeValues(base, gauges);
  if (turned) TurnEye(base, g_turn_cam);
  g_turn_cam = 0;
  g_draw_root(ctx, base);
  PutBackEye(base);
  if (swap_gauges) SwapGaugeValues(base, gauges);
}

// GGPO's handle of player n is n (p2p.h QueueToPlayerHandle). The marks go into GGPO's copy of the input; the game's
// buffer stays as read (its press latching compares the next read with it). Player 2 with a turned camera: his side
// turned in both games (player 1's game marked kInputCapable), else his d-pad left / right and left stick x swapped
// (no changed input with an unturned view).
int AddLocalInputHook(void* self, int player, void* values, int size) {
  uint8_t pad[kNetInput];
  if (size != kNetInput) return g_add_local_input(self, player, values, size);
  std::memcpy(pad, values, size);
  pad[kMarks] |= kInputCapable;
  if (player == 2) {
    g_p2_input_ms.store(GetTickCount64(), std::memory_order_relaxed);
    if (Recent(g_turned_ms) && Swapped(g_base.load(std::memory_order_relaxed))) {
      if (Recent(g_p1_capable_ms)) {
        pad[kMarks] |= kInputSideTurned;
      } else {
        if (const uint8_t dpad = pad[1] & 0x0C; dpad == 0x04 || dpad == 0x08) pad[1] ^= 0x0C;  // DPAD_LEFT 4, RIGHT 8
        const int16_t lx = static_cast<int16_t>(pad[4] << 8 | pad[5]);
        const int16_t mx = lx == INT16_MIN ? INT16_MAX : static_cast<int16_t>(-lx);
        pad[4] = static_cast<uint8_t>(static_cast<uint16_t>(mx) >> 8);
        pad[5] = static_cast<uint8_t>(mx);
      }
    }
  }
  return g_add_local_input(self, player, pad, size);
}

// values = both players' inputs of the frame the game runs next (live or re-simulated: every frame of a session gets
// them first, t6_session.cpp SyncInputs)
int SyncInputHook(void* self, void* values, int size, int* disconnect_flags) {
  const int r = g_sync_input(self, values, size, disconnect_flags);
  if (r != 0 || size != 2 * kNetInput) return r;  // (GGPO_OK 0)
  const auto* in = static_cast<const uint8_t*>(values);
  const uint64_t now = GetTickCount64();
  if (in[kMarks] & kInputCapable) g_p1_capable_ms.store(now, std::memory_order_relaxed);
  g_p2_side_turned.store((in[kNetInput + kMarks] & kInputSideTurned) != 0, std::memory_order_relaxed);
  g_sync_ms.store(now, std::memory_order_relaxed);
  return r;
}

// After the game set the player's side from the camera: player 2's turned if his input of this frame says so (the
// sync right before; none for a while = no session, an offline fight stays as it is)
void SideUpdateHook(uint8_t* ctx, uint8_t* base) {
  const uint32_t pl = U32(ctx, kR3);
  g_side_update(ctx, base);
  if (!g_p2_side_turned.load(std::memory_order_relaxed) || !Recent(g_sync_ms) || Be32(base + pl + kPlayerIndex) != 1) return;
  const uint32_t side = _byteswap_ulong(Be32(base + pl + kInputSide) ^ 1);
  std::memcpy(base + pl + kInputSide, &side, 4);
}

// ---- the loaded exe ----

struct Range {
  uint8_t* lo;
  uint8_t* hi;
  bool Has(uint64_t p) const { return p >= reinterpret_cast<uint64_t>(lo) && p < reinterpret_cast<uint64_t>(hi); }
};

struct Exe {
  uint8_t* base = nullptr;
  Range text{};
  Range data[8]{};  // the initialized sections that are not code
  int ndata = 0;
  const RUNTIME_FUNCTION* funcs = nullptr;
  size_t nfuncs = 0;

  bool Load() {
    base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) return false;
    const IMAGE_SECTION_HEADER* s = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, s++) {
      const Range r{base + s->VirtualAddress, base + s->VirtualAddress + s->Misc.VirtualSize};
      if (s->Characteristics & IMAGE_SCN_MEM_EXECUTE) {
        if (!text.lo) text = r;
      } else if ((s->Characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA) && ndata < 8) {
        data[ndata++] = r;
      }
    }
    const IMAGE_DATA_DIRECTORY& ex = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    funcs = reinterpret_cast<const RUNTIME_FUNCTION*>(base + ex.VirtualAddress);
    nfuncs = ex.Size / sizeof(RUNTIME_FUNCTION);
    return text.lo && nfuncs;
  }

  // f(p) for every 8-aligned qword == v in the data sections, p at least 16 bytes after its section start and 32 before
  // its end (room for the neighbours a caller looks at)
  template <class F>
  void EachQword(uint64_t v, F f) const {
    for (int i = 0; i < ndata; i++)
      for (uint8_t* p = data[i].lo + 16; p + 32 <= data[i].hi; p += 8)
        if (*reinterpret_cast<const uint64_t*>(p) == v) f(p);
  }

  template <class F>
  void EachString(const char* s, size_t n, F f) const {
    for (int i = 0; i < ndata; i++)
      for (uint8_t* p = data[i].lo; p + n <= data[i].hi; p++)
        if (*p == static_cast<uint8_t>(*s) && std::memcmp(p, s, n) == 0) f(p);
  }

  // the function (.pdata) that starts at fn, as [begin, end)
  bool Function(const uint8_t* fn, Range& out) const {
    const auto rva = static_cast<DWORD>(fn - base);
    size_t lo = 0, hi = nfuncs;
    while (lo < hi) {
      const size_t mid = (lo + hi) / 2;
      if (funcs[mid].BeginAddress < rva) lo = mid + 1;
      else hi = mid;
    }
    if (lo == nfuncs || funcs[lo].BeginAddress != rva) return false;
    out = {base + funcs[lo].BeginAddress, base + funcs[lo].EndAddress};
    return true;
  }
};

// The host slot of a guest function: PPCFuncMappings[] = {guest, host} pairs of 16 bytes, sorted by guest address
uint64_t* GuestEntry(const Exe& e, uint32_t guest) {
  uint64_t* entry = nullptr;
  int hits = 0;
  e.EachQword(guest, [&](uint8_t* p) {
    auto* q = reinterpret_cast<uint64_t*>(p);
    if (e.text.Has(q[1]) && e.text.Has(q[-1]) && e.text.Has(q[3]) && q[-2] < guest && q[2] > guest && q[2] < guest + 0x100000) {
      entry = q + 1;
      hits++;
    }
  });
  return hits == 1 ? entry : nullptr;
}

// Direct calls (E8 rel32) of fn that store lr right before: mov qword ptr [reg + 0x100], reg in the 16 bytes before
int GuestCallSites(const Exe& e, const uint8_t* fn, uint8_t** out, int max) {
  int n = 0;
  for (uint8_t* p = e.text.lo + 16; p < e.text.hi - 5; p++) {
    p = static_cast<uint8_t*>(std::memchr(p, 0xE8, e.text.hi - 5 - p));
    if (!p) break;
    int32_t rel;
    std::memcpy(&rel, p + 1, 4);
    if (p + 5 + rel != fn) continue;
    bool lr_store = false;
    for (const uint8_t* q = p - 16; q <= p - 7 && !lr_store; q++)
      lr_store = (q[0] == 0x48 || q[0] == 0x4C) && q[1] == 0x89 && (q[2] & 0xC0) == 0x80 && (q[2] & 7) != 4 &&
                 q[3] == 0x00 && q[4] == 0x01 && q[5] == 0x00 && q[6] == 0x00;
    if (!lr_store) continue;
    if (n == max) return -1;
    out[n++] = p;
  }
  return n;
}

// The vtable slot of Peer2PeerBackend::AddLocalInput: RTTI name -> type info -> primary vtable (offset to top 0); the
// function in the slot has to load the address of AddLocalInput's log string (lea reg, [rip + disp32])
void** AddLocalInputSlot(const Exe& e) {
  const uint8_t* log = nullptr;
  int logs = 0;
  e.EachString(kAddLocalInputLog, sizeof(kAddLocalInputLog) - 1, [&](uint8_t* p) { log = p; logs++; });
  if (logs != 1) return nullptr;
  void** slot = nullptr;
  int hits = 0;
  e.EachString(kP2pTypeName, sizeof(kP2pTypeName), [&](uint8_t* name) {
    e.EachQword(reinterpret_cast<uint64_t>(name), [&](uint8_t* name_ref) {
      const uint8_t* type_info = name_ref - 8;  // {vptr, name, ...}
      e.EachQword(reinterpret_cast<uint64_t>(type_info), [&](uint8_t* ti_ref) {
        if (*reinterpret_cast<const int64_t*>(ti_ref - 8) != 0) return;
        void** vt = reinterpret_cast<void**>(ti_ref + 8);
        const auto* fn = static_cast<const uint8_t*>(vt[kAddLocalInputSlot]);
        Range f;
        if (!e.text.Has(reinterpret_cast<uint64_t>(fn)) || !e.Function(fn, f)) return;
        for (const uint8_t* p = f.lo; p + 7 <= f.hi; p++) {
          int32_t disp;
          std::memcpy(&disp, p + 3, 4);
          if ((p[0] & 0xFB) == 0x48 && p[1] == 0x8D && (p[2] & 0xC7) == 0x05 && p + 7 + disp == log) {
            slot = &vt[kAddLocalInputSlot];
            hits++;
            return;
          }
        }
      });
    });
  });
  return hits == 1 ? slot : nullptr;
}

bool Write(void* at, const void* bytes, size_t n) {
  DWORD old;
  if (!VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &old)) return false;
  std::memcpy(at, bytes, n);
  VirtualProtect(at, n, old, &old);
  FlushInstructionCache(GetCurrentProcess(), at, n);
  return true;
}

// A page within rel32 reach of the exe's code, for the jumps of the patched calls
uint8_t* NearPage(const Exe& e) {
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  const uintptr_t g = si.dwAllocationGranularity;
  const uintptr_t lo = reinterpret_cast<uintptr_t>(e.base) & ~(g - 1), hi = (reinterpret_cast<uintptr_t>(e.text.hi) + g) & ~(g - 1);
  for (uintptr_t d = 0; d < 0x40000000u; d += g) {
    const uintptr_t tries[2] = {lo - g - d, hi + d};
    for (const uintptr_t at : tries)
      if (void* p = VirtualAlloc(reinterpret_cast<void*>(at), 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE))
        return static_cast<uint8_t*>(p);
  }
  return nullptr;
}

// A guest function to hook: its host function (orig) and where the calls to it are
struct GuestHook {
  uint32_t guest;
  void* hook;
  GuestFn* orig;
  uint64_t* entry = nullptr;
  uint8_t* calls[32]{};
  int ncalls = 0;

  bool Find(const Exe& e) {
    entry = GuestEntry(e, guest);
    if (!entry) return false;
    ncalls = GuestCallSites(e, reinterpret_cast<uint8_t*>(*entry), calls, 32);
    return ncalls >= 0;
  }
  // stub = jmp [rip] to the hook, in the near page; calls and table entry to the hook
  int Install(uint8_t* stub) {
    *orig = reinterpret_cast<GuestFn>(*entry);
    const uint8_t jmp[6] = {0xFF, 0x25, 0, 0, 0, 0};
    std::memcpy(stub, jmp, 6);
    std::memcpy(stub + 6, &hook, 8);
    int n = 0;
    for (int i = 0; i < ncalls; i++) {
      const int32_t rel = static_cast<int32_t>(stub - (calls[i] + 5));
      n += Write(calls[i] + 1, &rel, 4);
    }
    const uint64_t h = reinterpret_cast<uint64_t>(hook);
    n += Write(entry, &h, 8);
    return n;
  }
};

void Log(const char* text) {
  wchar_t path[MAX_PATH];
  DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
  while (n && path[n - 1] != L'\\') n--;
  if (!n || n + 14 >= MAX_PATH) return;
  std::memcpy(path + n, L"side_swap.log", sizeof(L"side_swap.log"));
  const HANDLE f = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) return;
  DWORD w;
  WriteFile(f, text, static_cast<DWORD>(std::strlen(text)), &w, nullptr);
  CloseHandle(f);
}

// Under the loader lock, before the exe's own code (and the runtime that reads PPCFuncMappings) runs: no other thread
// runs the bytes patched here
void Install() {
  char env[8];
  const DWORD n = GetEnvironmentVariableA("T6_SIDE_SWAP", env, sizeof(env));
  if (n == 1 && env[0] == '0') return Log("side swap: off (T6_SIDE_SWAP=0)\n");
  Exe e;
  if (!e.Load()) return Log("side swap: not patched - the exe is not an x64 image with .pdata\n");
  GuestHook hooks[3] = {{kDrawRoot, reinterpret_cast<void*>(&DrawRootHook), &g_draw_root},
                        {kViewSetup, reinterpret_cast<void*>(&ViewSetupHook), &g_view_setup},
                        {kSideUpdate, reinterpret_cast<void*>(&SideUpdateHook), &g_side_update}};
  char line[320];
  for (GuestHook& h : hooks)
    if (!h.Find(e)) {
      std::snprintf(line, sizeof(line), "side swap: not patched - guest %08X not in the function table or too many calls\n", h.guest);
      return Log(line);
    }
  if (hooks[1].ncalls == 0) return Log("side swap: not patched - no call of the view setup found\n");
  if (hooks[2].ncalls == 0) return Log("side swap: not patched - no call of the side update found\n");
  void** slot = AddLocalInputSlot(e);
  if (!slot) return Log("side swap: not patched - GGPO Peer2PeerBackend::AddLocalInput not found\n");
  void** sync_slot = slot + (kSyncInputSlot - kAddLocalInputSlot);
  Range sync_fn;
  if (!e.text.Has(reinterpret_cast<uint64_t>(*sync_slot)) || !e.Function(static_cast<const uint8_t*>(*sync_slot), sync_fn))
    return Log("side swap: not patched - GGPO Peer2PeerBackend::SyncInput not found\n");
  uint8_t* page = NearPage(e);
  if (!page) return Log("side swap: not patched - no memory near the exe for the call stubs\n");

  g_add_local_input = reinterpret_cast<AddLocalInputFn>(*slot);
  g_sync_input = reinterpret_cast<SyncInputFn>(*sync_slot);
  void* const slot_hooks[2] = {reinterpret_cast<void*>(&AddLocalInputHook), reinterpret_cast<void*>(&SyncInputHook)};
  if (!Write(slot, slot_hooks, sizeof(slot_hooks))) return Log("side swap: not patched - vtable not writable\n");
  int done[3];
  for (int i = 0; i < 3; i++) done[i] = hooks[i].Install(page + 16 * i);
  DWORD old;
  VirtualProtect(page, 0x1000, PAGE_EXECUTE_READ, &old);
  FlushInstructionCache(GetCurrentProcess(), page, 48);
  const auto rva = [&](const void* p) { return static_cast<unsigned long long>(static_cast<const uint8_t*>(p) - e.base); };
  std::snprintf(line, sizeof(line),
                "side swap: on - draw root +%llX (%d of %d calls + table), view setup +%llX (%d of %d calls + table), "
                "side update +%llX (%d of %d calls + table), AddLocalInput +%llX, SyncInput +%llX\n",
                rva(reinterpret_cast<void*>(g_draw_root)), done[0] - 1, hooks[0].ncalls, rva(reinterpret_cast<void*>(g_view_setup)),
                done[1] - 1, hooks[1].ncalls, rva(reinterpret_cast<void*>(g_side_update)), done[2] - 1, hooks[2].ncalls,
                rva(reinterpret_cast<void*>(g_add_local_input)), rva(reinterpret_cast<void*>(g_sync_input)));
  Log(line);
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE dll, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(dll);
    Install();
  }
  return TRUE;
}
