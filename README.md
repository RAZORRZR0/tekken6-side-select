# Tekken 6 Side-select

Tekken 7-style side select for [Tekken 6 Native Online](https://github.com/Ekspym/tekken6-native-online-source): as
player 2 of an online match you see and play the fight from player 1's side, on the left.

It is one `winmm.dll` that goes next to `tekken6.exe`. The game, the exe and the files the launcher's matchmaking
checks stay as built, and the DLL holds no game code or data.

## What it does

As player 2 of an online match:

- **Camera**: the fight is seen from the other side of the fighters, so you stand on the left.
- **HUD**: health bars, names, rank plates and round marks are swapped to match.
- **Controls**: right is forward and left is back, as on the left side.
- **Sidesteps**: up goes into the background of your view when your opponent runs Side-select too (0.1 or later).
  Against a game without it the sidesteps stay mirrored; see [Limitations](#limitations).

Player 1's screen, offline fights, practice and the menus stay as they are.

## Install

1. Build the game with the Tekken 6 Native Online launcher, so that `native\tekken6.exe` exists.
2. Download `winmm.dll` from the [latest release](../../releases/latest) and put it into the game's `native\`
   folder, next to `tekken6.exe`.
3. Play online. After the game starts, `native\side_swap.log` should read `side swap: on - ...`.

To uninstall it, delete `native\winmm.dll`. To turn it off without deleting it, set the environment variable
`T6_SIDE_SWAP=0`. If the log says `side swap: not patched - ...`, the game runs as normal without it; please open an
issue with that line.

## Limitations

- **The sidestep fix needs Side-select on both sides.** The other game only sees your buttons, and in it "up" is
  both the sidestep (tap) and the jump (hold). Only both games turning your side in their simulation fixes it
  without breaking jump and crouch. The two DLLs agree on this by themselves through spare bytes of the online
  input, so mixed versions stay in sync. [FINDINGS.md](FINDINGS.md#sidesteps) has the details.
- Tested in local two-instance online sessions (0 desyncs in every run of about 4000 frames), not yet in a match over
  the internet.
- The swap of the low-health glow is not visually confirmed yet.
- Tested with the Tekken 6 (Europe) Rev 1 image. The DLL finds its places in the exe by signature, so rebuilds of the
  game do not break it.

## Building

With [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) (the copy in the launcher's `builder\llvm-mingw` works):

```
x86_64-w64-mingw32-clang++ -std=c++23 -O2 -march=x86-64-v2 -shared -s -o winmm.dll src/side_swap.cpp src/winmm.def
```

`src/winmm.def` forwards every export of the system `winmm.dll`, so the game's own winmm calls still work.

## How it works

`tekken6.exe` imports `winmm.dll`, so this proxy loads before any game code runs. It patches the loaded exe in memory:

- It turns the active camera half round for the view setup and the draw, and puts it back after each, so the game
  logic never sees it turned.
- It swaps the left / right pairs of the HUD placement table and the values of the two health gauges, only while
  the frame is drawn.
- It hooks GGPO's `AddLocalInput` and `SyncInput` and the game's per-frame side update of a fighter, to turn player
  2's side in both games.

Everything is found by signature, never by host address. [FINDINGS.md](FINDINGS.md) documents the reverse
engineering: addresses, structures, experiments and results.

## Credits

- **RAZORRZR0**: Tekken 6 Side-select.
- **[Ekspym](https://github.com/Ekspym)**: [Tekken 6 Native Online](https://github.com/Ekspym/tekken6-native-online-source),
  the native port, rollback netplay and launcher this mod is built for. Its netplay test switches and fight state
  dumps were used to check the sync.
- **[mahmoud-mess](https://github.com/mahmoud-mess)**: [tekken6-recomp](https://github.com/mahmoud-mess/tekken6-recomp),
  the original static recompilation of Tekken 6 that Tekken 6 Native Online is based on.
- **[ReXGlue SDK](https://github.com/rexglue/rexglue-sdk)**: the Xbox 360 recompiler and runtime (BSD 3-Clause),
  derived from [Xenia](https://github.com/xenia-project/xenia).
- **[GGPO](https://github.com/pond3r/ggpo)** by Tony Cannon: the rollback netcode (MIT) whose input this mod marks.
- **[llvm-mingw](https://github.com/mstorsjo/llvm-mingw)** by Martin Storsjö: the toolchain.

Tekken is a trademark of Bandai Namco Entertainment. This project is not affiliated with or endorsed by Bandai Namco.

## License

[MIT](LICENSE). It covers this repository's code only; no game code or data is in it.
