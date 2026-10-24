# Tekken 6 Side-select

Tekken 7-style side select for [Tekken 6 Native Online](https://github.com/Ekspym/tekken6-native-online-source): as
player 2 of an online match you see and play the fight mirrored, from the left, like Tekken 7's side select.

It is one `winmm.dll` that goes next to `tekken6.exe`. The game, the exe and the files the launcher's matchmaking
checks stay as built, and the DLL holds no game code or data.

## What it does

As player 2 of an online match:

- **Picture**: the 3D scene is drawn mirrored left to right, so you stand on the left. The camera is the game's own,
  so the framing is the same as on player 1's screen.
- **HUD**: health bars, names, rank plates and round marks are swapped to match. Text is not mirrored.
- **Controls**: right is forward and left is back, as on the left side.
- **Sidesteps**: up goes into the background and down into the foreground, on both screens.

Only your own game changes. Your opponent doesn't need Side-select, and the fight itself is the same as without it.
Player 1's screen, offline fights, practice and the menus stay as they are.

## Install

1. Build the game with the Tekken 6 Native Online launcher, so that `native\tekken6.exe` exists.
2. Download `winmm.dll` from the [latest release](../../releases/latest) and put it into the game's `native\`
   folder, next to `tekken6.exe`.
3. Play online. After the game starts, `native\side_swap.log` should read `side swap: on (mirror) - ...`. Version
   0.1 wrote `side swap: on - ...` without `(mirror)`.

To uninstall it, delete `native\winmm.dll`. To turn it off without deleting it, set the environment variable
`T6_SIDE_SWAP=0`. If the log says `side swap: not patched - ...`, the game runs as normal without it; please open an
issue with that line.

## Limitations

- Like in Tekken 7, the mirrored picture also mirrors the characters and the stage: a left-handed stance looks
  right-handed, and text in the 3D scene reads backwards.
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

- It negates the x column of the active camera's projection matrices while the frame's draw lists are built, which
  mirrors the picture. The game logic never sees it, because it only reads the camera position.
- Mirroring turns every triangle the other way round, so it flips the front face of every cull mode the game sets
  for a mirrored frame.
- It swaps the left / right pairs of the HUD placement table and the values of the two health gauges, only while
  the frame is drawn.
- It hooks GGPO's `AddLocalInput` to swap left / right in player 2's input before it is sent, so both games
  simulate the same input.

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
- **[GGPO](https://github.com/pond3r/ggpo)** by Tony Cannon: the rollback netcode (MIT) whose input this mod hooks.
- **[llvm-mingw](https://github.com/mstorsjo/llvm-mingw)** by Martin Storsjö: the toolchain.

Tekken is a trademark of Bandai Namco Entertainment. This project is not affiliated with or endorsed by Bandai Namco.

## License

[MIT](LICENSE). It covers this repository's code only; no game code or data is in it.
