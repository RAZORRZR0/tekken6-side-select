# Findings

Notes from building Tekken 6 Side-select on Tekken 6 Native Online: a ReXGlue static recompilation of the Xbox 360
`Default.xex` (Tekken 6 Europe Rev 1), with GGPO rollback netplay.

- Guest addresses (`0x82...`) are addresses in the game image. They are the same in every build of the port.
- Host addresses move with every build, so nothing here depends on them.
- `sub_XXXXXXXX` is the recompiled guest function at that address.
- Offsets are given in decimal (`+652`) or hex (`+0x28C`); both mean the same bytes.
- Every guest value is big-endian.

## Loading: why `winmm.dll`

- `tekken6.exe` imports `winmm.dll` (`timeBeginPeriod`, `timeGetTime`). A proxy next to the exe loads before any game
  code runs, and it can patch the exe from `DllMain` under the loader lock.
- `src/winmm.def` forwards all 180 named exports and the nameless ordinal 2 to
  `\\.\GLOBALROOT\SystemRoot\System32\winmm.dll`.
- An earlier `dxgi.dll` proxy failed. It forwarded only the `CreateDXGIFactory*` functions, but `rexruntime.dll` also
  looks up `DXGIGetDebugInterface1`. The runtime then reports "Failed to get DXGI, Direct3D 12 or FXC function
  addresses", and the game stops with "Unable to initialize Direct3D 12".
- The launcher's build hash covers only the exe, `rexruntime.dll`, `rexgpu-xenos.dll`, the menu, the exclude list and
  the profile. `winmm.dll` does not change matchmaking.

## Finding code without host addresses

**Guest functions.** ReXGlue's `PPCFuncMappings` is a table of 16-byte `{u64 guest, u64 host}` entries, sorted by guest
address.

- An entry is accepted if it holds the guest address and its neighbours pass these checks:
  - `q[-1]`, `q[1]` and `q[3]` (the host pointers) lie in `.text`.
  - `q[-2] < guest < q[2] < guest + 0x100000`.
- The runtime builds its indirect-call lookup from this table, so redirecting the entry catches indirect calls.
- Direct calls are `E8 rel32` to the host function. Each call site stores the link register right before the call:
  `mov qword [reg + 0x100], reg` (PPCContext `lr` at `+0x100`) within the 16 bytes before.
- Those calls are pointed at a `jmp [rip]` stub in a page allocated within rel32 reach.
- PPCContext layout: `r3` at `+0x00`, `r4` at `+0x20`, `r5` at `+0x28`, `lr` at `+0x100`.

**GGPO.** The vtable of `Peer2PeerBackend` is found by RTTI:

1. Find the type name `16Peer2PeerBackend`.
2. From it, find its type info.
3. From that, find the vtable with offset-to-top 0.
4. Check slot 4 (`AddLocalInput`): it must load (`lea`) the string "setting local connect status for local queue".
   The slot order comes from `backends/backend.h`: `~D1`, `~D0`, `DoPoll`, `AddPlayer`, `AddLocalInput`.

## Game state used

| What | Where |
|---|---|
| Game mode | `0x8292E66C`. The battle (round call, fight, KO, replay, between rounds) is modes 3, 4, 5, 6 and 10 |
| Fight player objects | `*(0x828DCE88)` (P1) and `*(0x828DCE8C)` (P2), `0xC40` bytes. Position x at `+0x0`, z at `+0x8`; these words move in steps (see [The turned camera](#the-camera)) |
| Player index | player `+76`: 0 or 1, read from pad 8 or 9 |
| Task tree | root `0x82828C60`, child / sibling links at `+16..+40`; task objects in `0xA7290000-0xABA90000` |

## The camera

- The camera manager `sub_8231C488` sets up the views once a frame, the active camera first, through `sub_8231CFE0`.
  That call has lr `0x8231C7FC`.
- The camera object holds the eye at `+96`, the target at `+112` and up at `+128`.
- `sub_8231CFE0(cam)` tail-calls `sub_82307CF0(cam + 96, cam + 224, cam + 288, cam + 160)`. That function:
  1. builds the perspective matrix with `sub_82516430` and stores it at `cam + 224`;
  2. builds the look-at matrix with `sub_82515C10` (lr `0x82307D8C`) and stores it at `cam + 288`;
  3. multiplies them in the VMX registers `v92-v95` (the current matrix of the engine's matrix library) and stores
     the result at `cam + 160`.
- The matrices are row-vector (`clip = v M`), row-major, 16 big-endian floats. Read from a fight:
  `P = [2.1446 0 0 0 | 0 3.8126 0 0 | 0 0 -1 -1 | 0 0 -100 0]`, and `cam + 160` equals `V · P`.
- The draw root `tcb_drawroot_t` (`0x8221CA68`) runs right after. It doesn't call the cull mode setter below. All
  of that setter's calls come from another thread, so the draw root seems to only fill draw lists.
- The fight reads only the camera's eye (`sub_82319688` returns `cam + 96`, used by `sub_82309D30` for the sides),
  not its matrices.

**The mirror (0.2).** For the draw root, the x column of `cam + 160` and `cam + 224` is negated (floats 0, 4, 8, 12;
for a big-endian float that is bit 7 of its first byte), and put back after it. `clip.x → −clip.x` mirrors the
picture left to right. Every triangle then winds the other way, so culling has to take the other side:

- `sub_824D0350` is `D3DDevice_SetRenderState_CullMode(device, value)`. It puts the value into the low 3 bits of
  `device + 10568` (PA_SU_SC_MODE_CNTL: cull front 1, cull back 2, front face 4) and sets the dirty bit `0x40` of the
  u64 at `device + 16`. In a fight the game passes almost only `D3DCULL_CW` 2 and none 0, a few hundred calls a frame.
- The neighbours fit the same register: `sub_824D0370` is the getter (no direct callers), `sub_824D0380` sets the
  fill mode (bits 3-10), and `sub_824D0E58` / `sub_824D0F20` set the polygon offset enables (bits 11, 12).
- Every culling value set between one draw root and the next (`value & 3 != 0`) gets its front face bit flipped
  (`^ 4`).

Results (two local instances, P1's screen as built):

| Run | P2's picture | Desync |
|---|---|---|
| Matrices mirrored, culling as built | mirrored, but the sky missing (black) and the characters' colours wrong | 0 of 4074 frames |
| Matrices mirrored, culling flipped | exact mirror of P1's screen; HUD readable; round call, KO, replay, win pose correct | 0 of 4320 frames |
| Same, P1 without the DLL (`T6_SIDE_SWAP=0`), random input | as above | 0 of 4238 frames |

**The turned camera (0.1).** Version 0.1 turned the active camera half round about its up axis through its target
(`eye' = target + 2 (d.u) u - d`, `d = eye - target`) for the view setup and the draw root, and turned player 2's side
in both games (see [Sidesteps](#sidesteps)).

- Reports against 0.1 (sidesteps wrong, player 1 behind player 2's camera when backing far away) came from a player 2
  PC that still ran a build from before 0.1: its `winmm.dll` was 70.5 KB (SHA-256 `f7f59f37...`) and its
  `side_swap.log` had no side update hook, so that build turned the camera but never player 2's side.
- 0.2 replaced the turned camera with the mirror on a wrong diagnosis (that the two turned cameras facing each other
  make the sidesteps wrong).

Approaches that failed before 0.1:

- **Keeping the camera turned for the whole frame** desynced 3505 of 4164 frames. The first difference was at frame
  659, in bytes `0x287`, `0x28B` and `0x28F` of the player objects. Those are the side words below, which the game
  computes from the camera's eye.
- **The look-at function** `sub_82515C10` (lr `0x8221E7B8`) is never called in a fight.
- **Mirroring the look-at result** inside `sub_82307CF0` (lr `0x82307D8C`) gave wrong culling and lighting. That was
  before the cull mode setter was known; mirroring the projection instead leaves view space as it is.

**The turned camera (0.3).** Players did not want the mirrored picture. 0.3 turns the camera again, with 0.1's side
turn:

- **Pivot**: the vertical through the camera's target. `eye.xz' = 2 target.xz - eye.xz`, y kept, up's x and z
  negated. Over a fight (2591 frames): eye to target 4600 (up to 6779); the camera is level (eye y - target y,
  median 0); the middle of the fighters' position words is within ±482 of the target in depth and ±915 across
  (median 7 and 24).
- **Not the middle of the fighters**: their position words move in steps. Walking back, player 1's moved by 284 every
  22 frames; a sidestep moved by about 1030 at its end; at the round call, up to 1355 at once. A camera turned about
  their middle jumps with them.
- **Framing**: from the other side the fighter in the back is in front. Turned as is, 56 of 3874 fight frames had a
  fighter off player 2's screen (screen x 1.0 to 1.01) while he was within 0.9 on player 1's. So the turned eye moves
  back along the view when a fighter would be farther out than 0.85 of the half width and than on player 1's
  screen: per fighter `|x| / widest - (2 len - z)` (x, z: across and along player 1's view from his eye, len: eye to
  target, widest: the larger `|x| / z` of the two, at least `0.85 / P[0][0]`). It is eased: half of the gap a frame
  going back, 0.05 coming in. Then 0 of 4666 frames.
- **Known bug, not fixed**: half of the gap a frame makes the move back nearly instant. Unlike the game's own camera it
  looks like a jump.
- Player 2's side is turned in both games, so both players need 0.3. Until player 1's input carries the capable
  mark, player 2's camera is not turned.

## The HUD

- The `tk::sprite::battle` sprites take their position from a placement table at `0x82011390`: 110 entries of
  `{float x, float y, int anchor, int flip}`.
  - The table is read-only image data, so it needs `VirtualProtect`; writing without it crashed the game.
  - The HUD code picks the left or the right entry of a pair by the player's side.
  - Entries 0 and 1 have x = 187.0 and 1089.0 as built. That tells whether the table is swapped.
- 49 left / right pairs are swapped while player 2's view is turned and swapped back for other frames. The table is
  not game state (no snapshot brings an old state back).
  - Most pairs are neighbours. The exceptions are `{35, 37}`, `{36, 38}`, `{45, 47}` and `{46, 48}`.
  - Entries 24, 55, 56 and 57 are centred and have no pair.
- **Health gauges** (`CGaugeTcb_t`, vtable `0x82011C5C`, side at `+84`, draw `sub_822EC570`) use placements 6, 7 and
  10-13.
  - Swapping those placements makes the fills vanish, because the fill offsets `+124` and `+128` belong to the side.
  - Instead, the gauges keep their placements, and their values `+92` (damage trail), `+100` (fill) and `+108` are
    swapped between the two gauge tasks around the draw root only.
- **Low-health glow**: a child task (vtable `0x82011C88`, side at `+132`, draw `sub_822EC948`). Its draw takes
  placement `8 + side` and an x offset by side (the float at `0x82011C2C` for side 0, at `0x820074F8` for side 1).
  - 0.2 swapped placements 8 and 9 with the other pairs. The glow then sat off its bar, out at the screen edge (12 of
    105 screenshot pairs with HUD in a 0.2 run).
  - Instead, the glow keeps its placements and its side word is flipped around the draw root only.
  - `sub_822EC948` was the only side-dependent geometry found among the 22 functions that call the placement helpers
    `sub_822EA120`-`sub_822EB1C8` (read by hand after an automated first pass that missed it).
- The battle HUD main task is `CMainTcb_t`, vtable `0x82011BAC`.

## A fighter's side

Hardware watchpoints on player 2's words `+0x284`, `+0x288` and `+0x28C` over 25 s of a fight (1500 frames):

| Word | Written | Read |
|---|---|---|
| `+644` (`0x284`), `+648` (`0x288`) | once a frame by `sub_82309D30` | `+648`: `sub_822A4510`, and `sub_822688E0` (copies it into a per-pad record), once a frame |
| `+652` (`0x28C`) | once a frame by `sub_822A4510` | only by move requirement checks in `sub_8229D3D8`, a few times |

- **`sub_82309D30(self, opponent)`**, reached through `sub_8230A768`, projects both fighters with the active camera.
  It sets `+644` and `+648` to 0 (left) or 1 (right) and returns that value. At round start player 1 is 0 and player
  2 is 1.
- **`sub_822A4510(player, stick_out)`** does a fighter's input step each frame:
  1. Calls the function above with the opponent, taken from the player table `0x828DCE88`.
  2. Copies `+648` to `+652`, the input side.
  3. Reads pad `8 + index` with `sub_82268148`, returns its direction bits and optionally converts them to stick
     floats.
  4. If `sub_822D1838()` is non-zero, `+652` is set to `sub_822D1960(index)` instead. What that mode check means is not
     known.
- **`sub_822A46B8`** calls it. In the pad path it passes `+652` as the side argument of `sub_822A4068`, probably the
  command reader. Its new presses are `input & ~previous`, with the previous input at `+1960`.
- **`sub_8229D3D8`** is the requirement switch. Its side cases include `side == 0`, `side != 0`, and both of these
  combined with the facing halfword `+180` above or below `0x4000`. Its rare reads fit a sidestep move being chosen by
  such a requirement; the experiment below confirms the effect.

## Sidesteps

The input side `+652` decides both left / right → forward / back and which way up sidesteps: always into the
background of the camera the game simulates with, player 1's. From the other side that background is player 2's
foreground. In the input, up is both the sidestep (tap) and the jump (hold); the two only differ after the press, so
no input swap can pick the other sidestep without breaking jump and crouch or adding lag.

0.3 (like 0.1) turns player 2's `+652` (`^ 1`) right after `sub_822A4510` in both games, for the frames whose
synchronized input of player 2 says so. With the turned side, right is also forward, so the input is not changed.
The two DLLs agree on it through byte 15 of the 16-byte GGPO input (the game reads only the first 12):

- every DLL ORs `0x01` (capable) into its player's input;
- player 2's DLL ORs `0x02` (side turned) while his camera is turned, which needs player 1's input to carry `0x01`;
- both games turn `+652` of the fighter with index 1 for frames whose synchronized input of player 2 carries `0x02`.

Scripted with `test/probe.cpp` (player 2 holds right 60 frames, taps up, taps down, every 240 frames; player 1
stands): each up moved player 2 +817 to +956 into his screen within 55 frames, each down −893 to −948 out of it;
holding right walked him 441 to 1389 toward player 1. 0 of 6429 frames desynced.

0.2 (mirror) only swapped left / right in player 2's input; it sets and reads no marks, so 0.3 against 0.2 turns
nothing. 0.1 and 0.3 use the same marks. [INFERENCE: reasoned from the code; mixed versions were not run together.]

## Testing setup

Two instances on one PC through the port's netplay switches (`T6_NET=1`, `T6_NET_PLAYER`, `T6_NET_REMOTE`,
`T6_NET_INPUT=random`, `T6_FIX_SEED=1`, `T6_PLAY=menu_vs.bin`, ...).

- **Desync check**: the per-frame players hash and RNG columns of both `fight_state.csv` files.
- **Byte diffs**: `T6_NET_PLAYER_DUMP=1` writes the player objects of every confirmed frame.
- **Screenshots**: `PrintWindow`.
- **`test/match_test.py`**: two instances with `test/probe.cpp` (this DLL plus the scripted input above and a line
  per drawn frame: positions, their screen x through the drawn view-projection, the drawn camera). It checks the
  log line, the desync, that player 2's camera is turned in his game only, both fighters on his screen whenever they
  are on player 1's, player 2 on the left whenever player 1 is on the left of his, walking and sidesteps, and the red
  of the HUD's bar rows (frames, damage, glow) at the swapped side in the screenshots.
- **Watchpoints**: debug registers set on the game threads from a test DLL, with hits mapped back to guest functions
  through `.pdata` and `PPCFuncMappings`.

## Open

- The camera's move back is nearly instant (see [The turned camera (0.3)](#the-camera)).
- A real online match over the internet.
- Only one stage was tested (the one `menu_vs.bin` picks), none with walls behind player 2's camera.
