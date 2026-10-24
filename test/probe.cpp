// Test build of side_swap.cpp for match_test.py: the same DLL, plus scripted input and a line per drawn frame in
// <T6_NET_DUMP_DIR>\probe.csv: script frame, game mode, turned; per player (1, 2): position x y z, screen x y and w
// through the drawn view-projection, input side +652; the drawn camera's eye and target.
// Script (local player, counted in the fight, game mode 4): player 1 stands, walks back (left) for frames 60 - 359,
// stands again and plays the random input from frame 1500 on; player 2, every 240 frames: holds right 0 - 59, taps up
// at 120, down at 180 (never attacks).

#define DllMain DllMain_side_swap
#include "../src/side_swap.cpp"

#include <initializer_list>
#undef DllMain

namespace {

GuestFn g_probe_view_setup, g_probe_draw_root;
AddLocalInputFn g_probe_next;
uint32_t g_probe_cam;
int g_script_frame = -1;
FILE* g_probe_log;

void ProbeViewSetup(uint8_t* ctx, uint8_t* base) {
  if (U32(ctx, kLr) == kActiveCameraLr) g_probe_cam = U32(ctx, kR3);
  g_probe_view_setup(ctx, base);
}

// called by DrawRootHook: camera (turned or not) and HUD as drawn
void ProbeDrawRoot(uint8_t* ctx, uint8_t* base) {
  const uint32_t cam = g_probe_cam, p1 = Be32(base + kPlayers), p2 = Be32(base + kPlayers + 4);
  const uint32_t mode = Be32(base + kGameMode);
  if (g_probe_log && cam && p1 && p2 && (mode == 3 || mode == 4 || mode == 5 || mode == 6 || mode == 10)) {
    float m[16];
    for (int i = 0; i < 16; i++) m[i] = BeF(base + cam + 160 + 4 * i);
    std::fprintf(g_probe_log, "%d,%u,%d", g_script_frame, mode, g_turned_cam != 0);
    for (const uint32_t p : {p1, p2}) {
      const float x = BeF(base + p), y = BeF(base + p + 4), z = BeF(base + p + 8);
      float c[4];
      for (int j = 0; j < 4; j++) c[j] = x * m[j] + y * m[4 + j] + z * m[8 + j] + m[12 + j];
      std::fprintf(g_probe_log, ",%.1f,%.1f,%.1f,%.4f,%.4f,%.1f,%u", x, y, z, c[0] / c[3], c[1] / c[3], c[3],
                   Be32(base + p + kInputSide));
    }
    for (const uint32_t o : {kCamVectors, kCamVectors + 16})
      std::fprintf(g_probe_log, ",%.1f,%.1f,%.1f", BeF(base + cam + o), BeF(base + cam + o + 4), BeF(base + cam + o + 8));
    std::fputc('\n', g_probe_log);
    std::fflush(g_probe_log);
  }
  g_probe_draw_root(ctx, base);
}

int ScriptHook(void* self, int player, void* values, int size) {
  const uint8_t* base = g_base.load(std::memory_order_relaxed);
  if (size != kNetInput || !base || Be32(base + kGameMode) != 4) return g_probe_next(self, player, values, size);
  const int k = ++g_script_frame;
  if (player == 1 && k >= 1500) return g_probe_next(self, player, values, size);
  uint8_t pad[kNetInput] = {};
  const int c = k % 240;
  if (player == 1) pad[1] = k >= 60 && k < 360 ? 0x04 : 0;                                    // DPAD_LEFT
  else pad[1] = c < 60 ? 0x08 : c >= 120 && c < 123 ? 0x01 : c >= 180 && c < 183 ? 0x02 : 0;  // RIGHT, UP, DOWN
  return g_probe_next(self, player, pad, size);
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE dll, DWORD reason, LPVOID) {
  if (reason != DLL_PROCESS_ATTACH) return TRUE;
  DisableThreadLibraryCalls(dll);
  Exe e;
  void** slot = e.Load() ? AddLocalInputSlot(e) : nullptr;  // before Install points the slot at its hook
  Install();
  if (!g_draw_root || !slot) return TRUE;
  g_probe_view_setup = g_view_setup;
  g_view_setup = ProbeViewSetup;
  g_probe_draw_root = g_draw_root;
  g_draw_root = ProbeDrawRoot;
  char path[MAX_PATH];
  const DWORD n = GetEnvironmentVariableA("T6_NET_DUMP_DIR", path, MAX_PATH - 16);
  if (n && n < MAX_PATH - 16) g_probe_log = std::fopen(std::strcat(path, "\\probe.csv"), "w");
  g_probe_next = reinterpret_cast<AddLocalInputFn>(*slot);
  void* const hook = reinterpret_cast<void*>(&ScriptHook);
  Write(slot, &hook, sizeof(hook));
  return TRUE;
}
