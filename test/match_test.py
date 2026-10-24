"""Two local instances of the game in an online match with Side-select on both: checks what player 2 sees and does.

    py test/match_test.py "<game root>" [--seconds 120] [--out test\\out]

<game root> holds native\\tekken6.exe, profile\\, tools\\menu_vs.bin and builder\\llvm-mingw. The test builds
test\\probe.cpp (src\\side_swap.cpp plus scripted input and a log of every drawn frame, see there) and puts it into
native\\ as winmm.dll for the run only: the installed winmm.dll and side_swap.log are put back afterwards (checked byte
for byte). Both windows are screenshot twice a second (out\\p1_NNN.png, p2_NNN.png). Checks:
  - side_swap.log: the turned camera DLL, everything found
  - no desync: the fight state of every frame both games logged is the same
  - player 2's camera is turned in his game only (half round about the camera target), and both fighters stay on his
    screen whenever they are on player 1's
  - player 2 is on the left of his screen; holding right walks him toward player 1; up sidesteps him into the
    background of his screen, down into the foreground
  - the health bars, their red damage and the low health glow sit at the swapped sides of player 2's HUD
Needs Python 3 with Pillow and numpy.
"""

import argparse
import ctypes
import math
import os
import shutil
import subprocess
import sys
import tempfile
import time
from ctypes import wintypes

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))

user32, gdi32 = ctypes.windll.user32, ctypes.windll.gdi32
user32.SetProcessDPIAware()
user32.GetDC.restype = wintypes.HDC
user32.GetDC.argtypes = [wintypes.HWND]
user32.ReleaseDC.argtypes = [wintypes.HWND, wintypes.HDC]
user32.PrintWindow.argtypes = [wintypes.HWND, wintypes.HDC, wintypes.UINT]
gdi32.CreateCompatibleDC.restype = wintypes.HDC
gdi32.CreateCompatibleDC.argtypes = [wintypes.HDC]
gdi32.CreateCompatibleBitmap.restype = wintypes.HBITMAP
gdi32.CreateCompatibleBitmap.argtypes = [wintypes.HDC, ctypes.c_int, ctypes.c_int]
gdi32.SelectObject.argtypes = [wintypes.HDC, wintypes.HGDIOBJ]
gdi32.GetDIBits.argtypes = [wintypes.HDC, wintypes.HBITMAP, wintypes.UINT, wintypes.UINT, ctypes.c_void_p,
                            ctypes.c_void_p, wintypes.UINT]
gdi32.DeleteObject.argtypes = [wintypes.HGDIOBJ]
gdi32.DeleteDC.argtypes = [wintypes.HDC]


def window(pid):
    """The largest visible window of the process"""
    found = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def each(h, _):
        p = wintypes.DWORD()
        user32.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid and user32.IsWindowVisible(h):
            r = wintypes.RECT()
            user32.GetClientRect(h, ctypes.byref(r))
            found.append((r.right * r.bottom, h))
        return True

    user32.EnumWindows(each, 0)
    return max(found)[1] if found else None


def grab(h):
    r = wintypes.RECT()
    user32.GetClientRect(h, ctypes.byref(r))
    w, ht = r.right, r.bottom
    if w < 64 or ht < 64:
        return None
    dc = user32.GetDC(h)
    mdc = gdi32.CreateCompatibleDC(dc)
    bmp = gdi32.CreateCompatibleBitmap(dc, w, ht)
    gdi32.SelectObject(mdc, bmp)
    user32.PrintWindow(h, mdc, 3)  # client area, full content (D3D)
    info = (ctypes.c_int32 * 11)(40, w, -ht, 1 | 32 << 16, 0, 0, 0, 0, 0, 0, 0)  # BITMAPINFOHEADER, 32 bit top-down
    buf = ctypes.create_string_buffer(w * ht * 4)
    gdi32.GetDIBits(mdc, bmp, 0, ht, buf, info, 0)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mdc)
    user32.ReleaseDC(h, dc)
    return Image.frombuffer("RGB", (w, ht), buf, "raw", "BGRX", 0, 1)


def build_probe(root, out):
    cxx = os.path.join(root, "builder", "llvm-mingw", "bin", "x86_64-w64-mingw32-clang++.exe")
    subprocess.run([cxx, "-std=c++23", "-O2", "-march=x86-64-v2", "-shared", "-s", "-o", out,
                    os.path.join(HERE, "probe.cpp"), os.path.join(HERE, "..", "src", "winmm.def")], check=True)


def start(root, run, player, log):
    native = os.path.join(root, "native")
    shutil.copytree(os.path.join(root, "profile"), os.path.join(run, "user"))
    env = dict(os.environ, T6_FIX_SEED="1", T6_PLAY=os.path.join(root, "tools", "menu_vs.bin"), T6_VIRTUAL_TB="1",
               T6_SOUND_SEPARATE="1", T6_SNAP_EXCLUDE_FILE=os.path.join(root, "tools", "netplay_exclude.txt"),
               T6_ROUNDS="3", T6_NET="1", T6_NET_DELAY="1", T6_NET_INPUT="random", REX_GPU_VSYNC="1",
               REX_FULLSCREEN="0", REX_LOG_LEVEL="info", T6_NET_PLAYER=str(player), T6_NET_DUMP_DIR=run,
               T6_NET_LOCAL_PORT=str(7199 + player), T6_NET_REMOTE=f"127.0.0.1:{7202 - player}")
    env["ggpo.network.delay"] = "0"
    return subprocess.Popen([os.path.join(native, "tekken6.exe"), f"--game_data_root={native}\\game",
                             f"--user_data_root={run}\\user", f"--cache_root={run}\\cache", f"--log_file={log}",
                             "--fullscreen=false", "--async_shader_compilation=true",
                             "--render_target_path_d3d12=rov", "--input_backend=mmjoystick"], cwd=native, env=env)


def wait_for(path, text, timeout):
    end = time.time() + timeout
    while time.time() < end:
        if os.path.exists(path) and text in open(path, encoding="utf-8", errors="replace").read():
            return True
        time.sleep(0.5)
    return False


def fight_states(path):
    """frame -> its fight state (the last 4 columns of fight_state.csv)"""
    out = {}
    for line in open(path):
        c = line.rstrip("\n").split(",")
        if len(c) > 5 and c[0].isdigit():
            out[int(c[0])] = tuple(c[-4:])
    return out


def probe_rows(path):
    rows = []
    for line in open(path):
        c = line.split(",")
        if len(c) == 23:
            rows.append([int(c[0]), int(c[1]), int(c[2])] + [float(v) for v in c[3:]])
    return rows


def match(root, seconds, out):
    native = os.path.join(root, "native")
    kept = {n: open(os.path.join(native, n), "rb").read() for n in ("winmm.dll", "side_swap.log")
            if os.path.exists(os.path.join(native, n))}
    work = tempfile.mkdtemp(prefix="t6match_")
    procs = []
    try:
        probe = os.path.join(work, "winmm.dll")
        build_probe(root, probe)
        shutil.copyfile(probe, os.path.join(native, "winmm.dll"))
        runs = [os.path.join(work, f"p{p}") for p in (1, 2)]
        for r in runs:
            os.makedirs(r)
        logs = [os.path.join(r, "tekken6.log") for r in runs]
        procs.append(start(root, runs[0], 1, logs[0]))
        if not wait_for(logs[0], "T6 net: session at", 180):
            sys.exit("player 1 never got to the session")
        procs.append(start(root, runs[1], 2, logs[1]))
        side_log = open(os.path.join(native, "side_swap.log")).read() if wait_for(
            os.path.join(native, "side_swap.log"), "side swap", 30) else ""
        os.makedirs(out, exist_ok=True)
        shots, end, n = [], time.time() + seconds, 0
        while time.time() < end and all(p.poll() is None for p in procs):
            t = time.time()
            hs = [window(p.pid) for p in procs]
            if all(hs):
                a, b = grab(hs[0]), grab(hs[1])
                if a and b and a.size == b.size:
                    a.save(os.path.join(out, f"p1_{n:03d}.png"))
                    b.save(os.path.join(out, f"p2_{n:03d}.png"))
                    small = (320, 320 * a.height // a.width)  # all the checks need
                    shots.append((a.resize(small), b.resize(small)))
                    n += 1
            time.sleep(max(0.0, 0.5 - (time.time() - t)))
        for p in procs:
            p.kill()
            p.wait()
        states = [fight_states(os.path.join(r, "fight_state.csv")) for r in runs]
        probes = [probe_rows(os.path.join(r, "probe.csv")) for r in runs]
        return side_log, states, probes, shots
    finally:
        for p in procs:
            if p.poll() is None:
                p.kill()
        for n in ("winmm.dll", "side_swap.log"):
            path = os.path.join(native, n)
            if n in kept:
                open(path, "wb").write(kept[n])
                assert open(path, "rb").read() == kept[n], f"{path} not put back"
            elif os.path.exists(path):
                os.remove(path)
        shutil.rmtree(work, ignore_errors=True)


# ---- the probe logs: (frame, mode, turned, p1 x y z sx sy w side, p2 x y z sx sy w side, eye x y z, target x y z)

FRAME, MODE, TURNED, P1, P2, EYE, TARGET = 0, 1, 2, 3, 10, 17, 20
X, Z, SX = 0, 2, 3


def xz(r, at):
    return r[at + X], r[at + Z]


def camera(p1_rows, p2_rows):
    fails = []
    fight2 = [r for r in p2_rows if r[MODE] == 4]
    turned = [r for r in fight2 if r[TURNED]]
    print(f"player 2's game: {len(fight2)} drawn fight frames, {len(turned)} turned; player 1's game: "
          f"{sum(r[TURNED] for r in p1_rows)} turned")
    if len(turned) < 600 or len(turned) < 0.9 * len(fight2) or any(r[TURNED] for r in p1_rows):
        fails.append("player 2's camera not turned in his fight, or player 1's turned")

    # the same frame in both games (same positions and camera target): both fighters on player 2's screen whenever
    # both are well inside player 1's (the position words move in steps, the drawn fighters smoothly: a margin)
    key = lambda r: (r[P1 + X], r[P1 + Z], r[P2 + X], r[P2 + Z], r[TARGET + X], r[TARGET + Z])
    p1_by_key = {key(r): r for r in p1_rows if r[MODE] == 4}
    pairs = [(p1_by_key[key(r)], r) for r in turned if key(r) in p1_by_key]
    inside = lambda r: max(abs(r[P1 + SX]), abs(r[P2 + SX]))
    out = [(r2[FRAME], round(inside(r1), 2), round(inside(r2), 2)) for r1, r2 in pairs
           if inside(r1) <= 0.9 and inside(r2) > 1]
    print(f"framing: {len(pairs)} frames found in both games, {len(out)} with a fighter off player 2's screen but "
          f"inside player 1's; fighters up to {max(math.dist(xz(r, P1), xz(r, P2)) for r in turned):.0f} apart")
    if len(pairs) < 200 or out:
        fails.append(f"a fighter off player 2's screen (frame, |x| on screen 1, on screen 2): {out[:6]}")

    # sides swap in the fight (throws, circling): player 2 on the left of his screen whenever player 1 is on player 1's
    wrong = [r2[FRAME] for r1, r2 in pairs if r1[P1 + SX] < r1[P2 + SX] and not r2[P2 + SX] < r2[P1 + SX]]
    print(f"player 2 on the left of his screen in {len(pairs) - len(wrong)} of {len(pairs)} frames, on the right in "
          f"{len(wrong)} with player 1 on the left of his")
    if wrong:
        fails.append(f"player 2 not on the left of his screen (frames {wrong[:6]})")

    # the script (probe.cpp): every 240 frames right 0 - 59, up at 120, down at 180; player 1 stands until frame 1500;
    # the first 240 are the round call
    at = {}
    for r in turned:
        at.setdefault(r[FRAME], r)
    walks, steps = [], []
    for c in range(1, 6):
        k = 240 * c
        if k in at and k + 59 in at:
            a, b = at[k], at[k + 59]
            to = np.subtract(xz(a, P1), xz(a, P2))
            walks.append(float(np.dot(np.subtract(xz(b, P2), xz(a, P2)), to / np.linalg.norm(to))))
        for tap, sign in ((k + 120, 1), (k + 180, -1)):
            if tap in at and tap + 55 in at:  # (the position words move at the end of the sidestep)
                a, b = at[tap], at[tap + 55]
                ahead = np.subtract(xz(a, TARGET), xz(a, EYE))  # into the screen of player 2's camera
                steps.append(sign * float(np.dot(np.subtract(xz(b, P2), xz(a, P2)), ahead / np.linalg.norm(ahead))))
    print(f"right held 60 frames, toward player 1: {[round(w) for w in walks]}; sidesteps into the screen (up) / out "
          f"of it (down), signed so that right is positive: {[round(s) for s in steps]}")
    if len(walks) < 4 or min(walks) < 50:
        fails.append("holding right does not walk player 2 toward player 1")
    if len(steps) < 8 or min(steps) < 300:
        fails.append("up / down do not sidestep player 2 into the background / foreground of his screen")
    return fails


# ---- the pictures

def bar_rows(img):
    """the health bar rows (bar frames, fills, red damage, low health glow)"""
    x = np.asarray(img, dtype=np.int16)
    return x[int(x.shape[0] * 0.07):int(x.shape[0] * 0.15)]


def has_hud(img):
    """the health bars are on the screen: some of their teal fill"""
    x = bar_rows(img)
    return int(((x[..., 0] < 90) & (x[..., 1] > 150) & (x[..., 2] > 120)).sum()) >= 3


def red(img):
    x = bar_rows(img)
    r, g, b = x[..., 0], x[..., 1], x[..., 2]
    return (r > 110) & (r > g + 60) & (r > b + 50)


def grow(m):
    o = m.copy()
    o[1:] |= m[:-1]
    o[:-1] |= m[1:]
    o[:, 1:] |= o[:, :-1].copy()
    o[:, :-1] |= o[:, 1:].copy()
    return o


def stray_red(a, b):
    """red pixels of the bar rows of player 2 not at a mirrored red place of player 1's, and the other way round"""
    ra, rb = red(a)[:, ::-1], red(b)
    return int((rb & ~grow(grow(ra))).sum()), int((ra & ~grow(grow(rb))).sum())


def hud(shots):
    fight = [i for i, (p1, p2) in enumerate(shots) if has_hud(p1) and has_hud(p2)]
    stray = [(i, *st) for i in fight if max(st := stray_red(*shots[i])) > 100]
    print(f"HUD: {len(shots)} screenshot pairs, {len(fight)} with the health bars, {len(stray)} with red of the bar "
          f"rows away from the other side's place")
    if len(fight) < 30:
        return [f"only {len(fight)} screenshot pairs with the health bars"]
    if len(stray) > 1:  # one: the glow pulsing between the two screenshots
        return [f"HUD red (bar frames, damage, low health glow) not swapped (pair, p2, p1 pixels): {stray[:8]}"]
    return []


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root")
    ap.add_argument("--seconds", type=float, default=120)
    ap.add_argument("--out", default=os.path.join(HERE, "out"))
    a = ap.parse_args()
    side_log, (s1, s2), (r1, r2), shots = match(a.root, a.seconds, a.out)
    fails = []

    print(side_log.strip())
    if not side_log.startswith("side swap: on (turned camera)"):
        fails.append("side_swap.log: not the turned camera DLL or not everything found")

    common = sorted(set(s1) & set(s2))
    bad = [f for f in common if s1[f] != s2[f]]
    print(f"fight state: {len(common)} frames in both games, {len(bad)} different")
    if len(common) < 1000 or bad:
        fails.append(f"desync: {len(bad)} of {len(common)} frames differ (first {bad[:1]})")

    fails += camera(r1, r2)
    fails += hud(shots)
    for f in fails:
        print("FAIL:", f)
    print("ok" if not fails else f"{len(fails)} failed")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
