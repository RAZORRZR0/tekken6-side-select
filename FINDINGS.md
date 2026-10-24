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
5. Slot 5 is `SyncInput`. The slot order comes from `backends/backend.h`: `~D1`, `~D0`, `DoPoll`, `AddPlayer`,
   `AddLocalInput`, `SyncInput`.

## Game state used

| What | Where |
|---|---|
| Game mode | `0x8292E66C`. The battle (round call, fight, KO, replay, between rounds) is modes 3, 4, 5, 6 and 10 |
| Fight player objects | `*(0x828DCE88)` (P1) and `*(0x828DCE8C)` (P2), `0xC40` bytes. Position x at `+0x0`, z at `+0x8` |
| Player index | player `+76`: 0 or 1, read from pad 8 or 9 |
| Task tree | root `0x82828C60`, child / sibling links at `+16..+40`; task objects in `0xA7290000-0xABA90000` |

## The camera

- The camera manager `sub_8231C488` sets up the views once a frame, the active camera first, through `sub_8231CFE0`.
  That call has lr `0x8231C7FC`.
- The camera object holds the eye at `+96`, the target at `+112` and up at `+128`.
- The draw root `tcb_drawroot_t` (`0x8221CA68`) runs right after.
- Turning half round about the up axis through the target: `eye' = target + 2 (d.u) u - d`, where `d = eye - target`
  and `u` is the unit up vector.
- The eye is turned for the active camera's view setup and again for the draw root, and put back after each. View,
  culling and lighting all see one consistent turned camera.

Approaches that failed:

- **Keeping the camera turned for the whole frame** desynced 3505 of 4164 frames. The first difference was at frame
  659, in bytes `0x287`, `0x28B` and `0x28F` of the player objects. Those are the side words below, which the game
  computes from the camera.
- **The look-at function** `sub_82515C10` (lr `0x8221E7B8`) is never called in a fight.
- **Mirroring inside** `sub_82307CF0` (lr `0x82307D8C`) gave inconsistent culling and lighting.

## The HUD

- The `tk::sprite::battle` sprites take their position from a placement table at `0x82011390`: 110 entries of
  `{float x, float y, int anchor, int flip}`.
  - The table is read-only image data, so it needs `VirtualProtect`; writing without it crashed the game.
  - The HUD code picks the left or the right entry of a pair by the player's side.
  - Entries 0 and 1 have x = 187.0 and 1089.0 as built. That tells whether the table is swapped.
- 50 left / right pairs are swapped while the camera is turned and swapped back for unturned frames. The table is not
  game state (no snapshot brings an old state back).
  - Most pairs are neighbours. The exceptions are `{35, 37}`, `{36, 38}`, `{45, 47}` and `{46, 48}`.
  - Entries 24, 55, 56 and 57 are centred and have no pair.
- **Health gauges** (`CGaugeTcb_t`, vtable `0x82011C5C`, side at `+84`, draw `sub_822EC570`) use placements 6, 7 and
  10-13.
  - Swapping those placements makes the fills vanish, because the fill offsets `+124` and `+128` belong to the side.
  - Instead, the gauges keep their placements, and their values `+92` (damage trail), `+100` (fill) and `+108` are
    swapped between the two gauge tasks around the draw root only.
- **Low-health glow**: a child task (vtable `0x82011C88`, side at `+132`, draw `sub_822EC948`) using pair `{8, 9}`.
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
background of the camera the game simulates with. Experiment: player 2's `+652` turned right after `sub_822A4510`,
in both games, with scripted input on player 2 (hold right 60 frames, then two taps of up):

| | Up tap: movement across the fight axis | Holding right | Desync |
|---|---|---|---|
| as built | about −1180 per sidestep | back | 0 of 4084 frames |
| `+652` turned | about +1410 to +1600 per sidestep | forward | 0 of 4058 frames |

**Why this can't be fixed on player 2's side alone.** Player 2's turned view has the game's background in front. In
the input, up is both the sidestep (tap) and the jump (hold). The two only differ after the press.

- To get the other sidestep, the input would have to say down on the frame of the press, before anyone can know
  whether it is a tap or a hold.
- If the press turns out to be a hold, the other game then sees a crouch.
- Holding every up / down press back until it is known adds lag to jumps, crouches and every diagonal input.

So both games must turn player 2's side in their simulation.

**Protocol (byte 15 of the 16-byte GGPO input).** The input is the XINPUT_GAMEPAD in guest byte order (12 bytes) plus
4 bytes of padding. The game gets only the first 12 bytes; the padding bytes are 0 as sent by the port.

- Every Side-select DLL ORs `0x01` (capable) into its player's input.
- Player 2's DLL ORs `0x02` (side turned) into his input while his camera is turned and player 1's synchronized input
  carries `0x01`.
- Each game hooks `SyncInput`, which runs before every simulated frame, live or re-simulated. It notes whether player
  2's input of that frame carries `0x02`.
- After `sub_822A4510` runs for the fighter with index 1, the hook turns `+652` (`^ 1`), unless no sync happened in
  the last 2 s (an offline fight).
- When player 1 is not capable, player 2's DLL swaps d-pad left / right and the left stick's x in his input instead,
  which is the 0.1 fallback, and his sidesteps stay mirrored.

Results with the release DLL in two local instances (scripted input as above):

| Player 1 | Sidestep on player 2's view | Desync |
|---|---|---|
| Side-select | into the background (turned, about +1400 per sidestep) | 0 of 3923 frames |
| Side-select off (`T6_SIDE_SWAP=0`) | mirrored (about −950 to −1350 per sidestep) | 0 of 3990 frames |

Earlier runs with random input: camera only, 0 of 4795 frames; camera and HUD, 0 of 4216 frames.

## Testing setup

Two instances on one PC through the port's netplay switches (`T6_NET=1`, `T6_NET_PLAYER`, `T6_NET_REMOTE`,
`T6_NET_INPUT=random`, `T6_FIX_SEED=1`, `T6_PLAY=menu_vs.bin`, ...).

- **Desync check**: the per-frame players hash and RNG columns of both `fight_state.csv` files.
- **Byte diffs**: `T6_NET_PLAYER_DUMP=1` writes the player objects of every confirmed frame.
- **Screenshots**: `PrintWindow`.
- **Watchpoints**: debug registers set on the game threads from a test DLL, with hits mapped back to guest functions
  through `.pdata` and `PPCFuncMappings`.

## Open

- A mirrored picture (the camera on player 1's side, the 3D scene drawn flipped left-to-right) would fix sidesteps
  without the opponent's DLL. The catch: everything is mirrored, and culling has to be inverted.
- A real online match over the internet.
- Visual check of the low-health glow `{8, 9}`.
