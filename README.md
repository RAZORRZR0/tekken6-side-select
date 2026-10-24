# Tekken 6 Side-select

Tekken 7-style side select for [Tekken 6 Native Online](https://github.com/Ekspym/tekken6-native-online-source): as
player 2 of an online match you see the fight from the other side of the stage and play it from the left, like
Tekken 7's side select.

It is one `winmm.dll` that goes next to `tekken6.exe`. The game, the exe and the files the launcher's matchmaking
checks stay as built, and the DLL holds no game code or data.

## What it does

As player 2 of an online match:

- **Camera**: the game's camera, turned half round about the point it looks at, so you stand on the left. When a
  fighter would end up farther out to the side of your screen than on player 1's, it moves back a little.
- **HUD**: health bars, names, rank plates, round marks and the low-health (rage) glow are swapped to match.
- **Controls**: right is forward and left is back, as on the left side.
- **Sidesteps**: up goes into the background of your screen and down into the foreground.

**Both players need Side-select 0.3.** The sidesteps can't be fixed in player 2's input alone (up is both the
sidestep and the jump), so both games turn player 2's side in the fight; the two DLLs agree on it through a spare byte
of the network input. Against a game without it, or with 0.2, nothing changes. Player 1's screen, offline fights,
practice and the menus stay as they are.

## Install

1. Build the game with the Tekken 6 Native Online launcher, so that `native\tekken6.exe` exists.
2. Download `winmm.dll` from the [latest release](../../releases/latest) and put it into the game's `native\`
   folder, next to `tekken6.exe`.
3. Do the same on the other player's PC.
4. Play online. After the game starts, `native\side_swap.log` should read `side swap: on (turned camera) - ...`.
   Version 0.2 wrote `(mirror)`, 0.1 `side swap: on - ... side update ...`.

To uninstall it, delete `native\winmm.dll`. To turn it off without deleting it, set the environment variable
`T6_SIDE_SWAP=0`. If the log says `side swap: not patched - ...`, the game runs as normal without it; please open an
issue with that line.

## Known bug

- When the camera moves back to keep a fighter on the screen, it moves almost at once. The game's own camera eases
  into such moves; this one looks like a jump.

## Limitations

- From the other side you see the other half of the stage. Only one stage was tested; none with a wall behind player
  2's camera.
- Tested in local two-instance online sessions (0 desyncs in every run, 3800 to 6400 frames each), not yet in a match
  over the internet.
- Tested with the Tekken 6 (Europe) Rev 1 image. The DLL finds its places in the exe by signature, so rebuilds of the
  game do not break it.

## Building

With [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) (the copy in the launcher's `builder\llvm-mingw` works):

```
x86_64-w64-mingw32-clang++ -std=c++23 -O2 -march=x86-64-v2 -shared -s -o winmm.dll src/side_swap.cpp src/winmm.def
```

`src/winmm.def` forwards every export of the system `winmm.dll`, so the game's own winmm calls still work.

## Testing

```
py test/match_test.py "<game folder>"
```

It builds `test/probe.cpp` (the DLL plus scripted input and a per-frame log) with the launcher's llvm-mingw, puts it
into `native\` for the run (the installed `winmm.dll` and `side_swap.log` are put back afterwards), and runs two
instances of the game against each other. It checks no desync, player 2's camera turned in his game only, both
fighters on his screen, him on the left, walking and sidesteps going the right way on his screen, and the HUD
swapped (screenshots). Needs Python 3 with Pillow and numpy; about two and a half minutes.

## How it works

`tekken6.exe` imports `winmm.dll`, so this proxy loads before any game code runs. It patches the loaded exe in memory:

- It turns the active camera half round about its target while the views are set up and the frame's draw lists are
  built, and puts it back after. The game logic decides the players' sides from the camera, so it never sees the
  turned one.
- It turns player 2's input side in both games' simulation for frames whose network input says so, which flips his
  sidesteps and his forward / back.
- It swaps the left / right pairs of the HUD placement table, the values of the two health gauges and the sides of the
  low-health glows, only while the frame is drawn.
- It hooks GGPO's `AddLocalInput` and `SyncInput` for the flags in the input's spare byte.

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
