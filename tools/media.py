#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["pillow==13.0.0"]
# ///
"""The README's screenshots and GIFs, recorded on the emulated cart.

usage: media.py art            (host) cover pictures for the showcase card
       media.py run [SCENE...] (torii-emu container) runs tools/media/*.opts
       media.py pack           (host) writes docs/media/*.png and *.gif

`just media` runs all three. The showcase card holds the homebrew test ROMs
under made-up names, each with a cover drawn here, so no game, box art or
other work of someone else's shows up in the pictures.

Each scene is an options file in tests/shots.py's format, run on a fresh copy
of the card. Its -s key frames become docs/media/SCENE-KEY.png at 2x, and
every -H range becomes docs/media/SCENE.gif (one GIF per scene: its -H ranges
in order), with runs of the same frame merged.
"""

import os
import re
import shutil
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
os.chdir(ROOT)
sys.path.insert(0, str(ROOT / "tests"))
sys.path.insert(1, str(ROOT / "tools"))

SCENES = Path("tools/media")
WORK = Path("dist/media")
ART = WORK / "art"
OUT = Path("docs/media")
SCALE = 2

# file on the card, test ROM, cover colours (sky top, sky bottom, ground)
GAMES = [
    ("GBA/Star Courier.gba", "dist/media/scene.gba", "stars"),
    ("GBA/Moonlit Garden.gba", "tests/roms/idle/idle.gba", "moon"),
    ("GBA/Pocket Planets.gba", "tests/roms/searchtest/searchtest.gba", "planet"),
    ("GBA/Ruins of Aster.gba", "tests/roms/cheattest/cheattest.gba", "ruins"),
    ("GBA/Night Market.gba", "tests/roms/ingame/ingame.gba", "city"),
    ("GBA/Sky Lantern.gba", "tests/roms/savetest/flash64.gba", "lantern"),
    ("GBA/Tidal Run.gba", "tests/roms/idle/idle.gba", "sea"),
    ("GBA/Ember Peak.gba", "tests/roms/idle/idle.gba", "volcano"),
    ("GBC/Lantern Woods.gbc", "tests/shots/data/cheat.gb", "forest"),
    ("GB/Tiny Golem.gb", "tests/shots/data/cheat.gb", "mono"),
    ("NES/Castle Run.nes", "tests/shots/data/cheat.nes", "castle"),
]

CHEATS = {
    "GBA/Star Courier": [
        ("Infinite lives", "32001000 0063", True, None, None),
        ("Max money", "82001002 270F", True, None, "press Select+Up"),
        ("Moon jump (hold A)", "74000130 03FE+32001004 0001", False, None, None),
        ("Walk through walls", "32001006 0001", False, None, None),
        ("Ship: Speed boost", "32001010 0004", False, "Ship", None),
        ("Ship: Infinite fuel", "82001012 03E7", False, "Ship", None),
        ("Ship: All parts", "32001014 00FF", False, "Ship", None),
        ("Start sector - Home", "32001018 0000", False, None, None),
        ("Start sector - Nebula", "32001018 0001", False, None, None),
        ("Start sector - Rift", "32001018 0002", False, None, None),
        ("Unlock every map", "3200101A 00FF", False, None, None),
    ],
    "GBC/Lantern Woods": [
        ("Infinite lives", "1F0-01B-609", True, None, None),
        ("Always 9 coins", "1F0-01B", False, None, None),
        ("Max money", "01FF00A1", False, None, None),
        ("Skip the logo", "001-00F", False, None, None),
    ],
}


def cht(cheats) -> bytes:
    lines = [f"cheats = {len(cheats)}", ""]
    for n, (desc, code, on, folder, keys) in enumerate(cheats):
        lines += [f'cheat{n}_desc = "{desc}"', f'cheat{n}_code = "{code}"']
        lines.append(f"cheat{n}_enable = {'true' if on else 'false'}")
        if folder:
            lines.append(f'cheat{n}_folder = "{folder}"')
        if keys:
            lines.append(f'cheat{n}_keys = "{keys}"')
        lines.append("")
    return "\n".join(lines).encode()


# --- art (host) ---------------------------------------------------------------

PALETTES = {
    "stars": ((10, 14, 48), (70, 40, 120), (30, 30, 60)),
    "moon": ((24, 36, 80), (120, 90, 160), (40, 90, 70)),
    "planet": ((8, 8, 24), (30, 20, 70), (0, 0, 0)),
    "ruins": ((250, 190, 120), (240, 120, 90), (120, 70, 60)),
    "city": ((20, 16, 50), (150, 50, 110), (30, 20, 40)),
    "lantern": ((40, 60, 140), (240, 150, 120), (60, 50, 90)),
    "sea": ((120, 200, 240), (230, 240, 250), (30, 110, 180)),
    "volcano": ((60, 20, 30), (230, 100, 40), (40, 30, 30)),
    "forest": ((180, 230, 200), (250, 240, 200), (40, 110, 70)),
    "mono": ((200, 210, 140), (160, 180, 100), (60, 80, 50)),
    "castle": ((90, 140, 230), (190, 220, 250), (70, 140, 60)),
}


def draw_cover(kind: str, size: int = 160):
    import random

    from PIL import Image, ImageDraw

    top, bottom, ground = PALETTES[kind]
    rnd = random.Random(kind)
    img = Image.new("RGB", (size, size))
    d = ImageDraw.Draw(img)
    for y in range(size):
        t = y / (size - 1)
        d.line(
            [(0, y), (size, y)],
            fill=tuple(int(a + (b - a) * t) for a, b in zip(top, bottom, strict=True)),
        )
    dark = sum(top) < 300
    if dark:
        for _ in range(40):
            x, y = rnd.randrange(size), rnd.randrange(size * 2 // 3)
            r = rnd.choice((1, 1, 2))
            d.ellipse([x - r, y - r, x + r, y + r], fill=(250, 245, 220))

    def hills(base, amp, colour, seed):
        r2 = random.Random(seed)
        pts = [(0, size)]
        phase = r2.random() * 6
        import math

        for x in range(0, size + 8, 8):
            pts.append((x, base + amp * math.sin(x / 23 + phase) + r2.randrange(-3, 4)))
        pts.append((size, size))
        d.polygon(pts, fill=colour)

    lighter = tuple(min(255, c + 40) for c in ground)
    if kind == "planet":
        d.ellipse([30, 40, 130, 140], fill=(230, 120, 80))
        d.ellipse([46, 52, 120, 126], fill=(250, 160, 100))
        d.arc([10, 75, 150, 105], 160, 380, fill=(250, 230, 180), width=4)
        d.ellipse([112, 20, 132, 40], fill=(150, 200, 250))
    elif kind in ("moon", "stars", "lantern", "city"):
        d.ellipse([100, 22, 136, 58], fill=(250, 240, 200))
        if kind == "moon":
            d.ellipse([110, 18, 142, 52], fill=top)
        hills(112, 8, lighter, kind + "1")
        hills(126, 6, ground, kind + "2")
        if kind == "city":
            for i in range(9):
                x = 6 + i * 17
                h = rnd.randrange(30, 70)
                d.rectangle([x, size - h, x + 14, size], fill=(25, 15, 35))
                for wy in range(size - h + 6, size - 6, 9):
                    if rnd.random() < 0.6:
                        d.rectangle([x + 4, wy, x + 8, wy + 3], fill=(250, 200, 90))
        if kind == "lantern":
            for _ in range(6):
                x, y = rnd.randrange(20, 140), rnd.randrange(40, 110)
                d.ellipse([x, y, x + 10, y + 13], fill=(250, 170, 60))
                d.ellipse([x + 3, y + 3, x + 7, y + 9], fill=(255, 230, 150))
        if kind == "stars":
            d.polygon([(40, 100), (80, 70), (70, 104)], fill=(220, 230, 250))
            d.polygon([(40, 100), (25, 112), (52, 104)], fill=(250, 120, 80))
    elif kind == "volcano":
        d.polygon([(10, 160), (70, 50), (90, 50), (150, 160)], fill=ground)
        d.polygon([(70, 50), (90, 50), (84, 80), (76, 70)], fill=(250, 120, 40))
        for _ in range(8):
            x = 80 + rnd.randrange(-30, 30)
            y = 40 - rnd.randrange(0, 30)
            d.ellipse([x - 4, y - 4, x + 4, y + 4], fill=(90, 70, 70))
    elif kind == "sea":
        d.ellipse([20, 20, 56, 56], fill=(255, 230, 120))
        for i, y in enumerate(range(96, 161, 10)):
            hills(y, 3, tuple(max(0, c - i * 8) for c in ground), f"sea{i}")
        d.polygon([(90, 92), (90, 52), (120, 88)], fill=(250, 250, 250))
        d.polygon([(76, 94), (126, 94), (118, 102), (84, 102)], fill=(120, 60, 40))
    elif kind == "ruins":
        d.ellipse([56, 40, 104, 88], fill=(255, 230, 160))
        hills(118, 5, ground, "ruins")
        for x in (30, 56, 104, 130):
            d.rectangle([x - 6, 70, x + 6, 124], fill=(200, 160, 130))
        d.rectangle([20, 62, 140, 72], fill=(210, 170, 140))
    elif kind == "forest":
        hills(118, 6, lighter, "forest")
        for i in range(7):
            x = 10 + i * 24 + rnd.randrange(-4, 4)
            h = rnd.randrange(50, 80)
            d.polygon([(x, 150 - h), (x - 16, 150), (x + 16, 150)], fill=ground)
        d.rectangle([76, 120, 84, 140], fill=(90, 60, 40))
        d.ellipse([72, 104, 88, 122], fill=(250, 210, 90))
    elif kind == "mono":
        hills(124, 5, ground, "mono")
        d.rectangle([60, 70, 100, 124], fill=(90, 110, 70))
        d.rectangle([68, 82, 76, 90], fill=(200, 210, 140))
        d.rectangle([84, 82, 92, 90], fill=(200, 210, 140))
        d.rectangle([50, 84, 60, 112], fill=(90, 110, 70))
        d.rectangle([100, 84, 110, 112], fill=(90, 110, 70))
    elif kind == "castle":
        hills(128, 6, ground, "castle")
        d.rectangle([50, 70, 110, 130], fill=(170, 160, 170))
        for x in range(50, 111, 12):
            d.rectangle([x, 62, x + 6, 70], fill=(170, 160, 170))
        d.rectangle([40, 50, 58, 130], fill=(150, 140, 155))
        d.rectangle([102, 50, 120, 130], fill=(150, 140, 155))
        d.polygon([(38, 50), (49, 30), (60, 50)], fill=(200, 60, 60))
        d.polygon([(100, 50), (111, 30), (122, 50)], fill=(200, 60, 60))
        d.rectangle([72, 100, 88, 130], fill=(80, 60, 50))
    return img


def draw_scene():
    """Star Courier's one screen: a ship over a ringed planet's horizon."""
    import math
    import random

    from PIL import Image, ImageDraw

    w, h = 240, 160
    rnd = random.Random("scene")
    img = Image.new("RGB", (w, h))
    d = ImageDraw.Draw(img)
    for y in range(h):
        t = y / (h - 1)
        top, bottom = (6, 8, 30), (60, 30, 100)
        d.line(
            [(0, y), (w, y)],
            fill=tuple(int(a + (b - a) * t) for a, b in zip(top, bottom, strict=True)),
        )
    for _ in range(90):
        x, y = rnd.randrange(w), rnd.randrange(h)
        c = rnd.choice(((250, 245, 220), (170, 190, 250), (250, 200, 160)))
        if rnd.random() < 0.15:
            d.line([(x - 2, y), (x + 2, y)], fill=c)
            d.line([(x, y - 2), (x, y + 2)], fill=c)
        else:
            d.point((x, y), fill=c)
    # the planet, its rim lit, and its ring
    d.ellipse([-80, 104, 320, 420], fill=(230, 110, 70))
    d.ellipse([-70, 112, 310, 420], fill=(200, 80, 60))
    for i in range(6):
        y = 126 + i * 7
        d.arc([-120, y - 40, 360, y + 40], 200, 340, fill=(170, 60, 50), width=2)
    d.arc([-160, 98, 400, 150], 180, 360, fill=(250, 220, 170), width=3)
    # a moon
    d.ellipse([180, 20, 212, 52], fill=(200, 220, 250))
    d.ellipse([186, 26, 196, 36], fill=(170, 190, 230))
    d.ellipse([198, 38, 204, 44], fill=(170, 190, 230))
    # the courier ship with its trail
    sx, sy = 92, 66
    for i in range(18):
        r = 3 - i * 0.15
        x = sx - 6 - i * 4
        y = sy + 6 + math.sin(i / 2) * 1.5
        d.ellipse([x - r, y - r, x + r, y + r], fill=(250, 200 - i * 6, 90))
    d.polygon(
        [(sx - 8, sy), (sx + 30, sy + 6), (sx - 8, sy + 12)], fill=(220, 230, 250)
    )
    d.polygon(
        [(sx - 8, sy + 12), (sx + 30, sy + 6), (sx + 6, sy + 12)], fill=(150, 160, 200)
    )
    d.polygon([(sx, sy), (sx + 8, sy - 10), (sx + 14, sy + 3)], fill=(240, 90, 80))
    d.polygon([(sx, sy + 12), (sx + 8, sy + 22), (sx + 14, sy + 9)], fill=(200, 60, 60))
    d.ellipse([sx + 12, sy + 2, sx + 20, sy + 8], fill=(120, 220, 250))
    # a score line, as a game would show
    d.rectangle([0, 0, w, 11], fill=(10, 10, 30))
    for i in range(3):
        x = 6 + i * 10
        d.polygon([(x, 2), (x + 7, 5), (x, 9)], fill=(240, 90, 80))
    for i in range(6):
        x = 170 + i * 11
        d.rectangle([x, 3, x + 7, 8], fill=(250, 220, 120))
    return img


def bgr555(img) -> bytes:
    out = bytearray()
    for r, g, b in img.convert("RGB").get_flattened_data():
        v = (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)
        out += v.to_bytes(2, "little")
    return bytes(out)


def art() -> int:
    import art as artlib

    shutil.rmtree(ART, ignore_errors=True)
    ART.mkdir(parents=True)
    for path, _, kind in GAMES:
        pic = ART / f"{kind}.png"
        draw_cover(kind).save(pic)
        name = Path(path).name
        (ART / f"{name}.bin").write_bytes(artlib.convert(pic))
    scene = draw_scene()
    scene.save(ART / "scene.png")
    (WORK / "scene.bin").write_bytes(bgr555(scene))
    # The viewer's screenshot: the scene as the in-game menu would save it
    scene.save(ART / "scene.bmp")
    print(f"media: {len(GAMES)} covers and the scene in {ART}")
    return 0


# --- run (container) ------------------------------------------------------------


def build_card(img: Path) -> None:
    import shots

    stage = WORK / "sd"
    shutil.rmtree(stage, ignore_errors=True)
    stage.mkdir(parents=True)
    for path, rom, _ in GAMES:
        dest = stage / path
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(rom, dest)
    torii = stage / "torii"
    custom = torii / "art" / "custom"
    custom.mkdir(parents=True)
    for bin_ in ART.glob("*.bin"):
        shutil.copy(bin_, custom / bin_.name)
    for stem, cheats in CHEATS.items():
        dest = torii / "cheats" / f"{stem}.cht"
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(cht(cheats))
    (torii / "lang").mkdir()
    shutil.copy("dist/lang/sv.lng", torii / "lang" / "sv.lng")
    shots_dir = torii / "screenshots"
    shots_dir.mkdir()
    shutil.copy(
        ART / "scene.bmp",
        shots_dir / "Star Courier 2026-10-28 14-03-22.bmp",
    )
    (torii / "recent.txt").write_text(
        "0\t/GBA/Star Courier.gba\n0\t/GBC/Lantern Woods.gbc\n"
        "0\t/GBA/Night Market.gba\n0\t/NES/Castle Run.nes\n",
        encoding="utf-8",
    )
    (torii / "favorites.txt").write_text(
        "/GBA/Star Courier.gba\n/GBA/Tidal Run.gba\n", encoding="utf-8"
    )
    img.unlink(missing_ok=True)
    shots.run(
        [
            "mkfs.fat",
            "-C",
            "-F",
            "32",
            "-i",
            "70C0FFEE",
            "-n",
            "TORII",
            str(img),
            "65536",
        ]
    )
    shots.copy_tree(str(img), stage, "::")


def run(names: list[str]) -> int:
    import shots

    shots.SHOTS = SCENES
    shots.FRAMES = WORK / "frames"
    every = sorted(p.stem for p in SCENES.glob("*.opts"))
    names = names or every
    img = WORK / "card.img"
    build_card(img)

    def one(name: str) -> int:
        out = WORK / "run" / name
        shutil.rmtree(out, ignore_errors=True)
        out.mkdir(parents=True)
        proc = shots.boot(name, img, WORK / "carts" / name, out)
        (out / "log.txt").write_text(proc.stderr + proc.stdout, encoding="utf-8")
        frames = re.findall(
            r"^hash frame (\d+): ([0-9A-F]+)$", proc.stdout, re.MULTILINE
        )
        (out / "frames.txt").write_text(
            "".join(f"{f} {h}\n" for f, h in frames), encoding="utf-8"
        )
        errors = [
            line
            for line in proc.stderr.splitlines()
            if line.startswith(("kernel error:", "crash at", "timed out", "cannot"))
        ]
        if proc.returncode or errors:
            print(f"FAIL: media {name}: exit {proc.returncode}: {errors[:3]}")
            return 1
        stills = len(list(out.glob("*.png")))
        print(f"media {name}: {stills} stills, {len(frames)} frames")
        return 0

    with ThreadPoolExecutor(max_workers=4) as pool:
        status = max(pool.map(one, names), default=0)
    return status


# --- pack (host) ----------------------------------------------------------------


def options(name: str) -> dict[str, str]:
    """A scene's "#! media KEY=VALUE..." line: crop=Y0:Y1 keeps those rows,
    hold=MS how long the GIF's last frame shows (1500 by default; 0 for a
    seamless loop), stills=A,B the key frames to keep (all by default)."""
    for line in (SCENES / f"{name}.opts").read_text(encoding="utf-8").splitlines():
        if line.startswith("#! media "):
            return dict(w.split("=", 1) for w in line.split()[2:])
    return {}


def big(img, crop: str | None = None):
    from PIL import Image

    if crop:
        y0, y1 = (int(v) for v in crop.split(":"))
        img = img.crop((0, y0, img.width, y1))
    return img.convert("RGB").resize(
        (img.width * SCALE, img.height * SCALE), Image.Resampling.NEAREST
    )


def pack() -> int:
    from PIL import Image

    OUT.mkdir(parents=True, exist_ok=True)
    for old in OUT.glob("*.png"):
        old.unlink()
    for old in OUT.glob("*.gif"):
        old.unlink()
    for out in sorted((WORK / "run").iterdir()):
        name = out.name
        opts = options(name)
        crop = opts.get("crop")
        keep = opts.get("stills")
        for png in sorted(out.glob("*.png")):
            if keep and png.stem not in keep.split(","):
                continue
            big(Image.open(png)).save(OUT / f"{name}-{png.stem}.png", optimize=True)
        lines = (out / "frames.txt").read_text().split()
        hashes = lines[1::2]
        if not hashes:
            continue
        # Every frame is 1/60 s. GIF delays are in hundredths and browsers
        # slow anything under 2/100 down, so merge runs of a frame and hand
        # out the time in hundredths, keeping the error from adding up.
        runs: list[list] = []
        for h in hashes:
            if runs and runs[-1][0] == h:
                runs[-1][1] += 1
            else:
                runs.append([h, 1])
        frames, delays, owed = [], [], 0.0
        for h, n in runs:
            owed += n * 100 / 60
            cs = int(owed)
            if cs < 2:  # too short to show: fold it into the next frame
                continue
            owed -= cs
            frames.append(big(Image.open(WORK / "frames" / f"{h}.png"), crop))
            delays.append(cs * 10)
        # hold the last frame before looping
        delays[-1] = max(delays[-1], int(opts.get("hold", 1500)))
        pal = [f.quantize(colors=255, method=Image.Quantize.MEDIANCUT) for f in frames]
        pal[0].save(
            OUT / f"{name}.gif",
            save_all=True,
            append_images=pal[1:],
            duration=delays,
            loop=0,
            optimize=True,
            disposal=1,
        )
        size = (OUT / f"{name}.gif").stat().st_size
        print(f"media: {name}.gif, {len(frames)} frames, {size // 1024} KB")
    return 0


def main(argv: list[str]) -> int:
    if not argv or argv[0] not in ("art", "run", "pack"):
        sys.exit(__doc__)
    if argv[0] == "art":
        return art()
    if argv[0] == "run":
        return run(argv[1:])
    return pack()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
