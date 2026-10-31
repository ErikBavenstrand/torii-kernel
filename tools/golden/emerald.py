#!/usr/bin/env python3
"""Torii's golden cheat file for Pokémon Emerald (USA, Europe), CRC32 1F1C08FB.

    python3 tools/golden/emerald.py > "cheats/gba/Pokemon - Emerald Version (USA, Europe).cht"

Every cheat here was chosen with the owner (docs/research/cheats-emerald-curation.md)
and is built from what it does, not copied: the addresses come from pret's
pokeemerald decompilation and its symbol file for this ROM, the item and
species tables from tools/golden/emerald.json (made from the same source).

Formats (decision D1, D7): CodeBreaker for plain RAM writes, Action Replay v3
raw for ROM patches and save-block pointer writes, and Torii's raw lines
(AnyGBA's syntax) only where a value must be XORed with the save's key or
bits ORed in: money, coins, badges, symbols, the National Pokédex flag.

The two menus (L+R+B items, L+R+A field) are game scripts, strings and menu
tables written into the ROM's free space at launch, opened by a short RAM
trigger that starts the script when the player is free to move. They borrow
Emerald's unused multichoice menus (sMultichoiceLists) for their lists.
"""

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from cheat_encode import CB, VBA, Code
from cheat_encode import PARRaw as AR

DATA = json.loads(
    (Path(__file__).with_name("emerald.json")).read_text(encoding="utf-8")
)
ITEM = {k: v[0] for k, v in DATA["items"].items()}
ITEM_NAME = {v[0]: v[1] for v in DATA["items"].values()}

# --- Emerald's memory (pokeemerald.sym for this ROM) ----------------------

SB1, SB2 = 0x03005D8C, 0x03005D90  # gSaveBlock1Ptr, gSaveBlock2Ptr
KEY = "[03005D90]+00AC"  # the save's encryption key, SaveBlock2 + 0xAC
FLAGS = 0x1270  # SaveBlock1's event flags
VARS = 0x139C  # SaveBlock1's vars, from VARS_START 0x4000
KEYINPUT = 0x04000130
STATUS = 0x03000E38  # sGlobalScriptContextStatus: 2 = no script
CONTEXT = 0x03000E40  # sGlobalScriptContext: stackDepth, mode, ..., scriptPtr at +8
LOCK = 0x03000F2C  # sLockFieldControls
MULTICHOICE = 0x0858B760  # sMultichoiceLists: {list, count} by multichoice id
UNUSED_MENUS = [9, 10, 15, 19, 21, 22, 40, 41, 47, 51, 89, 90, 91, 92]  # MULTI_UNUSED_*
TRIGGER = 0x0203FF10  # free EWRAM past the game's last variable (0x0203CF64)


def flag_byte(flag):
    return FLAGS + (flag >> 3), 1 << (flag & 7)


def var_offset(var):
    return VARS + (var - 0x4000) * 2


# --- Gen 3 text --------------------------------------------------------------

CHARS = {
    " ": 0x00,
    "é": 0x1B,
    "&": 0x2D,
    "+": 0x2E,
    "!": 0xAB,
    "?": 0xAC,
    ".": 0xAD,
    "-": 0xAE,
}
CHARS.update({"'": 0xB4, ",": 0xB8, "/": 0xBA, ":": 0xF0, "\n": 0xFE})
CHARS.update({chr(65 + k): 0xBB + k for k in range(26)})
CHARS.update({chr(97 + k): 0xD5 + k for k in range(26)})
CHARS.update({str(k): 0xA1 + k for k in range(10)})
PLACEHOLDER = {"{PLAYER}": b"\xfd\x01", "{STR1}": b"\xfd\x02"}


def text(s):
    out = b""
    while s:
        for k, v in PLACEHOLDER.items():
            if s.startswith(k):
                out, s = out + v, s[len(k) :]
                break
        else:
            out, s = out + bytes([CHARS[s[0]]]), s[1:]
    return out + b"\xff"


# --- Script assembler ---------------------------------------------------------

OP = {
    "end": 0x02,
    "goto": 0x05,
    "goto_if": 0x06,
    "callstd": 0x09,
    "setvar": 0x16,
    "setorcopyvar": 0x1A,
    "compare": 0x21,
    "callnative": 0x23,
    "special": 0x25,
    "waitstate": 0x27,
    "setflag": 0x29,
    "clearflag": 0x2A,
    "warp": 0x39,
    "waitmessage": 0x66,
    "message": 0x67,
    "closemessage": 0x68,
    "lockall": 0x69,
    "releaseall": 0x6B,
    "waitbuttonpress": 0x6D,
    "checkitem": 0x47,
    "multichoice": 0x6F,
    "givemon": 0x79,
    "giveegg": 0x7A,
    "bufferspeciesname": 0x7D,
    "pokemart": 0x86,
}
VAR_RESULT, VAR_8000, VAR_8001, VAR_8004, VAR_8005 = (
    0x800D,
    0x8000,
    0x8001,
    0x8004,
    0x8005,
)
SPECIAL = {
    "HealPlayerParty": 1,
    "ChooseMonForMoveRelearner": 223,
    "TeachMoveRelearnerMove": 228,
    "IsSelectedMonEgg": 331,
}
PC_SCRIPT = (
    0x08271D9B  # EventScript_PC past its turn-on effect (GameHacking's "Access PC")
)


class Blob:
    """Bytes for the ROM at base, with labels resolved when laid out"""

    def __init__(self, base):
        self.base, self.parts, self.labels = base, [], {}

    def size(self):
        return sum(len(p) if isinstance(p, bytes) else 4 for p in self.parts)

    def label(self, name):
        assert name not in self.labels, name
        self.labels[name] = self.size()

    def b(self, *xs):
        self.parts.append(bytes(xs))

    def h(self, v):
        self.parts.append((v & 0xFFFF).to_bytes(2, "little"))

    def w(self, v):
        self.parts.append(v.to_bytes(4, "little"))

    def ptr(self, name):
        self.parts.append(name)

    def raw(self, data):
        self.parts.append(bytes(data))

    def align(self, n=4):
        while self.size() % n:
            self.b(0)

    def addr(self, name):
        if isinstance(name, int):
            return name
        name, _, plus = name.partition("+")
        return self.base + self.labels[name] + int(plus or 0)

    def bytes(self):
        out = b"".join(
            p if isinstance(p, bytes) else self.addr(p).to_bytes(4, "little")
            for p in self.parts
        )
        return out + (b"\0" if len(out) % 2 else b"")

    # script commands
    def cmd(self, name, *args):
        self.b(OP[name])
        for kind, v in args:
            {"b": self.b, "h": self.h, "w": self.w, "p": self.ptr}[kind](v)

    def goto(self, target):
        self.cmd("goto", ("p", target))

    def when(self, var, value, target):  # compare_var_to_value; goto_if ==
        self.cmd("compare", ("h", var), ("h", value))
        self.cmd("goto_if", ("b", 1), ("p", target))

    def say(self, label):  # message, wait for A
        self.cmd("message", ("p", label))
        self.cmd("waitmessage")
        self.cmd("waitbuttonpress")
        self.cmd("closemessage")


class Menus:
    """Scripts plus the multichoice lists they show, in one ROM blob"""

    def __init__(self, base, menu_ids):
        self.s = Blob(base)
        self.ids = list(menu_ids)
        self.lists = []  # (menu id, [label of each entry's text])
        self.texts = {}  # text -> label

    def t(self, s):
        if s not in self.texts:
            self.texts[s] = f"t{len(self.texts)}"
        return self.texts[s]

    def menu(self, name, entries, back):
        """A paged menu: entries are (title, target label); 9 a page, the
        last row MORE when more follow. B goes to back."""
        pages = []
        rest = list(entries)
        while rest:
            n = 9 if len(rest) <= 9 else 8
            pages.append(rest[:n])
            rest = rest[n:]
        for p, page in enumerate(pages):
            more = p + 1 < len(pages)
            rows = page + ([("MORE", f"{name}{p + 1}")] if more else [])
            mid = self.ids.pop(0)
            self.lists.append((mid, [self.t(title) for title, _ in rows]))
            s = self.s
            s.label(name if p == 0 else f"{name}{p}")
            s.cmd("multichoice", ("b", 0), ("b", 0), ("b", mid), ("b", 0))
            for i, (_, target) in enumerate(rows):
                s.when(VAR_RESULT, i, target)
            s.goto(back)

    def blob(self):
        s = self.s
        for txt, lbl in self.texts.items():
            s.label(lbl)
            s.raw(text(txt))
        s.align()
        tables = []
        for mid, labels in self.lists:
            s.label(f"m{mid}")
            for lbl in labels:
                s.ptr(lbl)
                s.w(0)
            tables.append((mid, f"m{mid}", len(labels)))
        data = s.bytes()
        patches = [
            (s.base + i, int.from_bytes(data[i : i + 2], "little"))
            for i in range(0, len(data), 2)
        ]
        for mid, lbl, n in tables:
            a = MULTICHOICE + 8 * mid
            patches += [
                (a, s.addr(lbl) & 0xFFFF),
                (a + 2, s.addr(lbl) >> 16),
                (a + 4, n),
            ]
        return patches


def trigger(code, blocks):
    """Starts a script while keys are held, once, when the player is free:
    blocks are (KEYINPUT value, script address). A trigger byte in free EWRAM
    carries the AND of the conditions, as each conditional covers one line."""
    code += AR.write(1, TRIGGER, 0)
    for k, (keys, _) in enumerate(blocks, 1):
        code += AR.cond("EQ", 2, KEYINPUT, keys)
        code += AR.write(1, TRIGGER, k)
    code += AR.cond("NE", 1, LOCK, 0)
    code += AR.write(1, TRIGGER, 0)
    code += AR.cond("NE", 1, STATUS, 2)
    code += AR.write(1, TRIGGER, 0)
    for k, (_, script) in enumerate(blocks, 1):
        code += AR.cond("EQ", 1, TRIGGER, k, "block")
        code += AR.write(1, STATUS, 0)
        code += AR.write(2, CONTEXT, 0x0100)  # stackDepth 0, mode: bytecode
        code += AR.write(4, CONTEXT + 8, script)
        code += AR.endif()


def keys(*names):
    bit = {
        "A": 0,
        "B": 1,
        "SELECT": 2,
        "START": 3,
        "RIGHT": 4,
        "LEFT": 5,
        "UP": 6,
        "DOWN": 7,
        "R": 8,
        "L": 9,
    }
    v = 0x3FF
    for n in names:
        v &= ~(1 << bit[n])
    return v


def rom_code(patches, code=None):
    code = code or Code(AR)
    for k, (a, v) in enumerate(patches):
        code += AR.rom_patch(k % 4 + 1, a, v)
    return code


class Mixed:
    """A cheat whose ROM data is VBA lines (a word a line) and whose trigger is
    Action Replay v3: no cheatN_format line, as each line's shape says its format"""

    fmt = None

    def __init__(self, patches, trigger_code):
        lines, have = [], dict(patches)
        for a in sorted(have):
            if a not in have:
                continue
            if a % 4 == 0 and a + 2 in have:
                lines.append(f"{a:08X}:{have.pop(a + 2) << 16 | have.pop(a):08X}")
            else:
                lines.append(f"{a:08X}:{have.pop(a):04X}")
        self.lines, self.trigger = lines, trigger_code

    def text(self):
        return "+".join(self.lines) + "+" + self.trigger.text()


def words(pairs):
    """ROM patches from (address, 16- or 32-bit value) pairs"""
    out = []
    for a, v in pairs:
        out.append((a, v & 0xFFFF))
        if v > 0xFFFF:
            out.append((a + 2, v >> 16))
    return out


# --- The menus ---------------------------------------------------------------

MART = {
    "MEDICINE": [
        "POTION",
        "SUPER_POTION",
        "HYPER_POTION",
        "MAX_POTION",
        "FULL_RESTORE",
        "REVIVE",
        "MAX_REVIVE",
        "ANTIDOTE",
        "PARALYZE_HEAL",
        "AWAKENING",
        "BURN_HEAL",
        "ICE_HEAL",
        "FULL_HEAL",
        "ETHER",
        "MAX_ETHER",
        "ELIXIR",
        "MAX_ELIXIR",
        "SACRED_ASH",
        "BERRY_JUICE",
        "FRESH_WATER",
        "SODA_POP",
        "LEMONADE",
        "MOOMOO_MILK",
        "ENERGY_POWDER",
        "ENERGY_ROOT",
        "HEAL_POWDER",
        "REVIVAL_HERB",
        "LAVA_COOKIE",
        "GUARD_SPEC",
        "DIRE_HIT",
        "X_ATTACK",
        "X_DEFEND",
        "X_SPEED",
        "X_ACCURACY",
        "X_SPECIAL",
        "REPEL",
        "SUPER_REPEL",
        "MAX_REPEL",
        "ESCAPE_ROPE",
        "POKE_DOLL",
        "FLUFFY_TAIL",
    ],
    "BALLS": [
        "POKE_BALL",
        "GREAT_BALL",
        "ULTRA_BALL",
        "MASTER_BALL",
        "SAFARI_BALL",
        "NET_BALL",
        "DIVE_BALL",
        "NEST_BALL",
        "REPEAT_BALL",
        "TIMER_BALL",
        "LUXURY_BALL",
        "PREMIER_BALL",
    ],
    "TRAINING": [
        "RARE_CANDY",
        "PP_UP",
        "PP_MAX",
        "HP_UP",
        "PROTEIN",
        "IRON",
        "CARBOS",
        "CALCIUM",
        "ZINC",
    ],
    "TMS & HMS": [
        k
        for k, v in sorted(DATA["items"].items(), key=lambda x: x[1][0])
        if v[3] == "TM_HM"
    ],
    "HELD ITEMS": [
        "SUN_STONE",
        "MOON_STONE",
        "FIRE_STONE",
        "THUNDER_STONE",
        "WATER_STONE",
        "LEAF_STONE",
    ]
    + [
        k
        for k, v in sorted(DATA["items"].items(), key=lambda x: x[1][0])
        if 179 <= v[0] <= 225 or 254 <= v[0] <= 258
    ],
    "BERRIES": [
        k
        for k, v in sorted(DATA["items"].items(), key=lambda x: x[1][0])
        if v[3] == "BERRIES"
    ],
    "VALUABLES": [
        "TINY_MUSHROOM",
        "BIG_MUSHROOM",
        "PEARL",
        "BIG_PEARL",
        "STARDUST",
        "STAR_PIECE",
        "NUGGET",
        "HEART_SCALE",
        "RED_SHARD",
        "BLUE_SHARD",
        "YELLOW_SHARD",
        "GREEN_SHARD",
        "SHOAL_SALT",
        "SHOAL_SHELL",
    ],
}
TICKETS = [  # name, item, the ferry flag its event sets (FLAG_ENABLE_SHIP_*)
    ("EON TICKET", "EON_TICKET", 0x8B3),
    ("AURORA TICKET", "AURORA_TICKET", 0x8D5),
    ("OLD SEA MAP", "OLD_SEA_MAP", 0x8D6),
    ("MYSTIC TICKET", "MYSTIC_TICKET", 0x8E0),
]


def item_menu():
    m = Menus(0x08F00000, UNUSED_MENUS[:2])
    s = m.s
    s.label("main")
    s.cmd("lockall")
    m.menu(
        "cats",
        [(c, "mart" + str(i)) for i, c in enumerate(MART)] + [("TICKETS", "tickets")],
        "done",
    )
    s.label("done")
    s.cmd("releaseall")
    s.cmd("end")
    for i in range(len(MART)):
        s.label("mart" + str(i))
        s.cmd("pokemart", ("p", "list" + str(i)))
        s.goto("cats")
    m.menu(
        "tickets", [(n, "tk" + str(i)) for i, (n, _, _) in enumerate(TICKETS)], "cats"
    )
    for i, (_, item, flag) in enumerate(TICKETS):
        s.label("tk" + str(i))
        s.cmd("setflag", ("h", flag))
        s.cmd("checkitem", ("h", ITEM[item]), ("h", 1))
        s.when(VAR_RESULT, 1, "have")  # one is enough: the flag is what opens the ferry
        s.cmd("setorcopyvar", ("h", VAR_8000), ("h", ITEM[item]))
        s.cmd("setorcopyvar", ("h", VAR_8001), ("h", 1))
        s.cmd("callstd", ("b", 0))  # STD_OBTAIN_ITEM: "Obtained the EON TICKET!"
        s.goto("tickets")
    s.label("have")
    s.say(m.t("You have it, and the ferry will take you."))
    s.goto("tickets")
    s.align(2)
    for i, items in enumerate(MART.values()):
        s.label("list" + str(i))
        for it in items:
            s.h(ITEM[it])
        s.h(0)
    return m.blob(), s.addr("main")


TOWNS = [  # name, map group, map number, x, y: the heal locations (outside each Pokémon Center)
    ("LITTLEROOT", 0, 9, 5, 9),
    ("OLDALE", 0, 10, 6, 17),
    ("PETALBURG", 0, 0, 20, 17),
    ("RUSTBORO", 0, 3, 16, 39),
    ("DEWFORD", 0, 11, 2, 11),
    ("SLATEPORT", 0, 1, 19, 20),
    ("MAUVILLE", 0, 2, 22, 6),
    ("VERDANTURF", 0, 14, 16, 4),
    ("FALLARBOR", 0, 13, 14, 8),
    ("LAVARIDGE", 0, 12, 9, 7),
    ("FORTREE", 0, 4, 5, 7),
    ("LILYCOVE", 0, 5, 24, 15),
    ("MOSSDEEP", 0, 6, 28, 17),
    ("SOOTOPOLIS", 0, 7, 43, 32),
    ("PACIFIDLOG", 0, 15, 8, 16),
    ("EVER GRANDE", 0, 8, 27, 49),
]
DUNGEONS = [  # name, map group, map number: arriving by the map's first warp
    ("METEOR FALLS", 24, 0),
    ("GRANITE CAVE", 24, 7),
    ("FIERY PATH", 24, 14),
    ("MT. CHIMNEY", 24, 12),
    ("MT. PYRE", 24, 22),
    ("NEW MAUVILLE", 24, 52),
    ("ABANDONED SHIP", 24, 54),
    ("SAFARI ZONE", 26, 3),
    ("SHOAL CAVE", 24, 46),
    ("SEAFLOOR CAVERN", 24, 27),
    ("CAVE OF ORIGIN", 24, 37),
    ("SKY PILLAR", 24, 85),
    ("VICTORY ROAD", 24, 43),
    ("DESERT RUINS", 24, 6),
    ("ISLAND CAVE", 24, 67),
    ("ANCIENT TOMB", 24, 68),
    (
        "ARTISAN CAVE",
        24,
        99,
    ),  # Terra and Marine Cave exist only while their weather lasts
]
ISLANDS = [  # name, map group, map number, x, y: where the ferry leaves you
    ("BATTLE FRONTIER", 26, 4, 19, 67),
    ("SOUTHERN ISLAND", 26, 9, 13, 22),
    ("BIRTH ISLAND", 26, 59, 8, 4),
    ("FARAWAY ISLAND", 26, 56, 13, 38),
    ("NAVEL ROCK", 26, 67, 8, 4),
]
LEGENDS = [  # name, flags to clear: defeated, caught, hidden (the map shows it again when not defeated)
    ("RAYQUAZA", [0x1C0]),
    ("GROUDON", [0x1BF]),
    ("KYOGRE", [0x1BE]),
    ("REGIROCK", [0x1BB, 0x3A7]),
    ("REGICE", [0x1BC, 0x3A8]),
    ("REGISTEEL", [0x1BD, 0x3A9]),
    ("LATIAS & LATIOS", [0x1C8, 0x1C9]),
    ("MEW", [0x1C7, 0x1CA]),
    ("DEOXYS", [0x1AC]),
    ("LUGIA", [0x1DD, 0x91, 0x320]),
    ("HO-OH", [0x1DC, 0x92, 0x321]),
]


def species(name):
    for sid, n in DATA["species"]:
        if n == name:
            return sid
    raise KeyError(name)


GIFTS = [  # name, species, level (0: an egg)
    ("TREECKO", "TREECKO", 5),
    ("TORCHIC", "TORCHIC", 5),
    ("MUDKIP", "MUDKIP", 5),
    ("CHIKORITA", "CHIKORITA", 5),
    ("CYNDAQUIL", "CYNDAQUIL", 5),
    ("TOTODILE", "TOTODILE", 5),
    ("BELDUM", "BELDUM", 5),
    ("CASTFORM", "CASTFORM", 25),
    ("LILEEP", "LILEEP", 20),
    ("ANORITH", "ANORITH", 20),
    ("WYNAUT EGG", "WYNAUT", 0),
]


def field_menu():
    m = Menus(0x08F08000, UNUSED_MENUS[2:])
    s = m.s
    s.label("main")
    s.cmd("lockall")
    m.menu(
        "field",
        [
            ("PC", "pc"),
            ("HEAL", "heal"),
            ("WARP", "warp"),
            ("LEGENDS", "legends"),
            ("GIFTS", "gifts"),
            ("MOVES", "moves"),
            ("UNLOCK", "unlock"),
        ],
        "done",
    )
    s.label("done")
    s.cmd("releaseall")
    s.cmd("end")
    s.label("pc")
    s.goto(PC_SCRIPT)
    s.label("heal")
    s.cmd("lockall")
    s.cmd("special", ("h", SPECIAL["HealPlayerParty"]))
    s.say(m.t("Your POKéMON are fully healed."))
    s.goto("done")
    m.menu(
        "warp",
        [("TOWNS", "towns"), ("DUNGEONS", "dungeons"), ("ISLANDS", "islands")],
        "field",
    )
    m.menu("towns", [(n, f"wt{i}") for i, (n, *_) in enumerate(TOWNS)], "warp")
    m.menu("dungeons", [(n, f"wd{i}") for i, (n, *_) in enumerate(DUNGEONS)], "warp")
    m.menu("islands", [(n, f"wi{i}") for i, (n, *_) in enumerate(ISLANDS)], "warp")
    for tag, rows in (("wt", TOWNS), ("wi", ISLANDS)):
        for i, (_, group, num, x, y) in enumerate(rows):
            s.label(f"{tag}{i}")
            s.cmd("warp", ("b", group), ("b", num), ("b", 0xFF), ("h", x), ("h", y))
            s.cmd("waitstate")
    for i, (_, group, num) in enumerate(DUNGEONS):
        s.label(f"wd{i}")
        s.cmd("warp", ("b", group), ("b", num), ("b", 0), ("h", 0xFFFF), ("h", 0xFFFF))
        s.cmd("waitstate")
    m.menu("legends", [(n, f"lg{i}") for i, (n, _) in enumerate(LEGENDS)], "field")
    for i, (_, flags) in enumerate(LEGENDS):
        s.label(f"lg{i}")
        for f in flags:
            s.cmd("clearflag", ("h", f))
        s.say(m.t("It waits in its lair again."))
        s.goto("legends")
    m.menu("gifts", [(n, f"gf{i}") for i, (n, *_) in enumerate(GIFTS)], "field")
    for i, (_, sp, level) in enumerate(GIFTS):
        s.label(f"gf{i}")
        if level:
            s.cmd(
                "givemon",
                ("h", species(sp)),
                ("b", level),
                ("h", 0),
                ("w", 0),
                ("w", 0),
                ("b", 0),
            )
        else:
            s.cmd("giveegg", ("h", species(sp)))
        s.when(VAR_RESULT, 2, "full")
        s.cmd("bufferspeciesname", ("b", 0), ("h", species(sp)))
        s.say(
            m.t(
                "{PLAYER} received an EGG!"
                if not level
                else "{PLAYER} received {STR1}!"
            )
        )
        s.goto("gifts")
    s.label("full")
    s.say(m.t("There is no room for it."))
    s.goto("gifts")
    m.menu(
        "unlock",
        [("ALL BADGES", "ub"), ("ALL SYMBOLS", "us"), ("POKéDEX", "ud")],
        "field",
    )
    for tag, flags, msg in (
        ("ub", range(0x867, 0x86F), "All 8 badges are yours."),  # FLAG_BADGE01-08_GET
        ("us", range(0x8C4, 0x8D2), "All 14 Frontier symbols are yours."),
    ):  # FLAG_SYS_*_SILVER/GOLD
        s.label(tag)
        for f in flags:
            s.cmd("setflag", ("h", f))
        s.say(m.t(msg))
        s.goto("unlock")
    s.label(
        "ud"
    )  # EnableNationalPokedex's var and flag, then tools/golden/dex.s for the bits
    s.cmd("setvar", ("h", 0x4046), ("h", 0x302))  # VAR_NATIONAL_DEX
    s.cmd("setflag", ("h", 0x896))  # FLAG_SYS_NATIONAL_DEX
    s.cmd("callnative", ("p", "dex+1"))
    s.say(m.t("All 386 POKéMON are seen and caught."))
    s.goto("unlock")
    s.label("moves")  # FallarborTown_MoveRelearnersHouse, without the Heart Scale
    s.cmd("special", ("h", SPECIAL["ChooseMonForMoveRelearner"]))
    s.cmd("waitstate")
    s.when(VAR_8004, 0xFF, "done")
    s.cmd("special", ("h", SPECIAL["IsSelectedMonEgg"]))
    s.when(VAR_RESULT, 1, "done")
    s.when(VAR_8005, 0, "nomoves")
    s.cmd("special", ("h", SPECIAL["TeachMoveRelearnerMove"]))
    s.cmd("waitstate")
    s.goto("done")
    s.label("nomoves")
    s.say(m.t("It has no move to remember."))
    s.goto("done")
    s.align()
    s.label("dex")
    s.raw((Path(__file__).with_name("dex.bin")).read_bytes())
    return m.blob(), {k: s.addr(k) for k in ("main", "heal", "pc", "warp", "moves")}


# --- The cheats --------------------------------------------------------------

NATURES = [
    "Hardy",
    "Lonely",
    "Brave",
    "Adamant",
    "Naughty",
    "Bold",
    "Docile",
    "Relaxed",
    "Impish",
    "Lax",
    "Timid",
    "Hasty",
    "Serious",
    "Jolly",
    "Naive",
    "Modest",
    "Mild",
    "Quiet",
    "Bashful",
    "Rash",
    "Calm",
    "Gentle",
    "Sassy",
    "Careful",
    "Quirky",
]
LAUNCH = "Set when the game starts: pick it on the Cheats screen before you play."


def title(name):
    special = {
        "HO-OH": "Ho-Oh",
        "MR. MIME": "Mr. Mime",
        "FARFETCH'D": "Farfetch'd",
        "PORYGON2": "Porygon2",
    }
    if name in special:
        return special[name]
    return name.replace("♀", " F").replace("♂", " M").title()


class Cheat:
    def __init__(self, folder, name, code, guide, keys=None):
        assert len(guide.encode()) <= 250, (name, len(guide.encode()))
        self.folder, self.name, self.code, self.guide, self.keys = (
            folder,
            name,
            code,
            guide,
            keys,
        )


def cb(*parts):
    code = Code(CB)
    for p in parts:
        code += p
    return code


def vba(*lines):
    code = Code(VBA)
    for line in lines:
        code += [line]
    return code


def ar(*parts):
    code = Code(AR)
    for p in parts:
        code += p
    return code


def cheats():
    out = []

    def add(folder, name, code, guide, keys="always"):
        out.append(Cheat(folder, f"{folder}: {name}", code, guide, keys))

    # Money (D8)
    add(
        "Money",
        "Max money",
        vba(f"[03005D8C]+0490:000F423F^{KEY}"),
        "Keeps your money at 999,999. Written with your save's own key, so nothing else changes. Torii only.",
    )
    add(
        "Money",
        "Max coins",
        vba(f"[03005D8C]+0494:270F^{KEY}"),
        "Keeps the Game Corner coins at 9,999 (you need the Coin Case). Torii only.",
    )
    add(
        "Money",
        "Max Battle Points",
        ar(AR.write_ptr(2, SB2, 9999, 0xEB8)),
        "Keeps your Battle Frontier points at 9,999.",
    )
    add(
        "Money",
        "Everything costs 1",
        ar(
            AR.cond("NE", 4, 0x02039F70, 0, "next2"),  # sShopData, while a shop is open
            AR.write_ptr(2, 0x02039F70, 1, 0x2000),
            AR.write_ptr(2, 0x02039F70, 0, 0x2002),
        ),
        "Any purchase in a shop costs 1, whatever the list shows: one item or 99.",
    )

    # The menus (D9, D15-D23)
    items, items_at = item_menu()
    code = Code(AR)
    trigger(code, [(keys("L", "R", "B"), items_at)])
    code = Mixed(items, code)
    out.append(
        Cheat(
            "Menus",
            "Menus: Item shop (L+R+B)",
            code,
            "Press L+R+B where you can walk: a shop of every item opens (Marts at game prices, "
            "tickets free). B goes back. Pick it before you play.",
            None,
        )
    )
    field, at = field_menu()
    code = Code(AR)
    trigger(
        code,
        [
            (keys("L", "R", "A"), at["main"]),
            (keys("L", "R", "UP"), at["heal"]),
            (keys("L", "R", "DOWN"), at["pc"]),
            (keys("L", "R", "LEFT"), at["warp"]),
            (keys("L", "R", "RIGHT"), at["moves"]),
        ],
    )
    code = Mixed(field, code)
    out.append(
        Cheat(
            "Menus",
            "Menus: Field menu (L+R+A)",
            code,
            "L+R+A facing nothing: PC, heal, warp, legendaries again, gifts, move relearner, badges, symbols, Pokédex. "
            "L+R+Up heals, +Down opens the PC, +Left warps, +Right relearns moves. Pick it before you play.",
            None,
        )
    )

    # Pokémon (D24-D26)
    add(
        "Pokémon",
        "All new Pokémon shiny",
        rom_code(
            words(
                [
                    (0x08067C74, 0x89908951),
                    (0x08067C78, 0x883B4048),
                    (0x08067C7C, 0x80784058),
                    (0x08067C80, 0x04008990),
                    (0x08067C84, 0x46C04301),
                ]
            )
        ),
        "Every Pokémon you get is shiny: wild, gifts, eggs. Caught ones stay shiny. "
        + LAUNCH,
        "launch",
    )
    add(
        "Pokémon",
        "Perfect IVs",
        rom_code(
            words(
                [(0x08067DC8, 0x43C02000), (0x08067E12, 0x2000), (0x08067E14, 0x43C0)]
            )
        ),
        "Wild, gift and egg Pokémon get 31 in every IV. Trainers' and yours don't change. "
        + LAUNCH,
        "launch",
    )
    add(
        "Pokémon",
        "Max friendship",
        rom_code([(0x0806DAB0, 0x46C0)]),
        "Friendship jumps to 255 at its next change (a level up, a walk, a vitamin). "
        + LAUNCH,
        "launch",
    )
    add(
        "Pokémon",
        "Max EVs from battles",
        rom_code([(0x0806DBC6, 0x24FF)]),
        "A Pokémon that earns EVs gets the maximum, all in HP and Attack. Stats follow at its next level. "
        + LAUNCH,
        "launch",
    )
    for n, nature in enumerate(NATURES):
        add(
            "Pokémon",
            f"Nature: {nature}",
            rom_code([(0x08067EAE, 0x2500 | n)]),
            f"New wild, gift and egg Pokémon are {nature}. Shiny and Wild gender pick the personality themselves and win over it. "
            + LAUNCH,
            "launch",
        )
    for name, v in (("Male", 0x21FF), ("Female", 0x2100)):
        add(
            "Pokémon",
            f"Wild gender: {name}",
            rom_code([(0x0806AF78, v)]),
            f"Wild Pokémon that can be either are {name.lower()}. " + LAUNCH,
            "launch",
        )
    add(
        "Pokémon",
        "Pokérus when caught",
        rom_code([(0x0806B0F4, 0x7029)]),
        "Pokémon you catch have Pokérus, which doubles the EVs they earn. " + LAUNCH,
        "launch",
    )
    add(
        "Pokémon",
        "Eggs hatch fast",
        ar(AR.write_ptr(1, SB1, 0xFE, 0x314C)),
        "Each step counts as a whole egg cycle: eggs hatch in a few dozen steps.",
    )

    # Battle (D28-D30)
    add(
        "Battle",
        "One-hit KO",
        cb(
            *(
                op
                for a in (0x02024104, 0x020241B4)
                for op in (CB.op("GT", a, 1), CB.write16(a, 1))
            )
        ),
        "Foes have 1 HP, so any hit that lands knocks them out (both in double battles).",
    )
    add(
        "Battle",
        "Infinite HP",
        rom_code(
            words(
                [
                    (0x08047E6A, 0x07D3),
                    (0x08047E6C, 0xD100),
                    (0x08047E6E, 0x6003),
                    (0x08047E70, 0x6800),
                ]
            )
        ),
        "Your Pokémon take no damage in battle. " + LAUNCH,
        "launch",
    )
    add(
        "Battle",
        "Infinite PP",
        rom_code([(0x08046B28, 0x07E9)]),
        "Your moves use no PP in battle. " + LAUNCH,
        "launch",
    )
    add(
        "Battle",
        "Skip trainer battles",
        rom_code([(0x080B3D6C, 0xE038)]),
        "Trainers who see you don't challenge you; talk to one to battle. " + LAUNCH,
        "launch",
    )
    for mult in (2, 3, 5, 10, 255):
        add(
            "Battle",
            f"EXP x{mult}",
            rom_code(
                [
                    (0x0804A67A, 0x464B),
                    (0x0804A67C, 0x6818),
                    (0x0804A67E, 0x2100 | mult),
                    (0x0804A680, 0x4348),
                    (0x0804A682, 0x2138),
                    (0x0804A684, 0x0209),
                    (0x0804A686, 0x4288),
                    (0x0804A688, 0xDB00),
                    (0x0804A68A, 0x1E48),
                    (0x0804A68C, 0x6018),
                ]
            ),
            f"Battle EXP x{mult}, up to 14,335 a Pokémon. It replaces the Lucky Egg's bonus. "
            + LAUNCH,
            "launch",
        )
    add(
        "Battle",
        "EXP for the whole party",
        cb(CB.write8(0x020243FE, 0xFF)),
        "Every Pokémon in your party shares the EXP of a battle, as if all had fought.",
    )
    add(
        "Battle",
        "Every ball catches",
        rom_code([(0x08056566, 0x46C0)]),
        "A thrown ball always catches (trainers' Pokémon still can't be caught). "
        + LAUNCH,
        "launch",
    )

    # Wild (D32-D34)
    for sid, name in DATA["species"]:
        v = (
            (0x3600 << 16 | 0x2600 | sid)
            if sid <= 255
            else (0x3600 | sid - 255) << 16 | 0x26FF
        )
        add(
            "Wild",
            f"Species: {title(name)}",
            rom_code(words([(0x080B4E6C, v)])),
            f"Every wild Pokémon is {title(name)}, at the levels and places of the area's own. "
            + LAUNCH,
            "launch",
        )
    for level in range(5, 101, 5):
        add(
            "Wild",
            f"Level {level}",
            rom_code(words([(0x080B4C74, 0x47702000 | level)])),
            f"Wild Pokémon are level {level} (grass, water, fishing, Rock Smash). "
            + LAUNCH,
            "launch",
        )
    add(
        "Wild",
        "No wild encounters",
        cb(CB.write8(0x020375D4, 0)),
        "No wild Pokémon in grass, caves or water while it's on. Fishing still works.",
    )
    add(
        "Wild",
        "Encounter every step",
        rom_code(words([(0x080B5170, 0x47702001)])),
        "A wild Pokémon on almost every step in grass, caves and water. " + LAUNCH,
        "launch",
    )

    # Overworld (D35-D39, D42, D47)
    add(
        "Overworld",
        "Fly to any town",
        cb(CB.op("EQ", 0x02002DD4, 0x0A74), CB.write8(0x02002DEA, 2)),
        "On the Fly map every town can be picked, visited or not.",
    )
    add(
        "Overworld",
        "Free Day Care",
        cb(CB.write16(0x020375E2, 0)),
        "The Day Care takes no money to give your Pokémon back.",
    )
    add(
        "Overworld",
        "Walk through walls (hold R)",
        vba(*Mixed(walls(), None).lines),
        "Hold R to walk through walls, people and water edges. Let go on normal ground; if you're stuck, hold R "
        "and walk back. " + LAUNCH,
        "launch",
    )
    add(
        "Overworld",
        "Field moves without badges",
        rom_code([(0x081B54EA, 0xE01B)]),
        "Cut, Surf, Fly and the rest work from the POKéMON menu without their badge. "
        + LAUNCH,
        "launch",
    )
    add(
        "Overworld",
        "Gym leaders ready for a rematch",
        ar(*(AR.write_ptr(1, SB1, 1, 0x9CA + i) for i in range(65, 73))),
        "The 8 gym leaders accept a rematch (a double battle with their strongest team): run it once, then visit "
        "them. Needs the game beaten.",
    )
    add(
        "Overworld",
        "No species limit at the Battle Tower",
        rom_code([(0x081A3F44, 0x42A8), (0x081B8648, 0x46C0)]),
        "Legendaries and any other Pokémon may enter the Battle Tower. " + LAUNCH,
        "launch",
    )

    # Minigames (D31, D43)
    add(
        "Minigames",
        "Infinite Safari Zone",
        cb(CB.write8(0x0203A04C, 30), CB.write16(0x0203A04E, 500)),
        "In the Safari Zone you keep 30 Safari Balls and 500 steps.",
    )
    block, slot = (
        SB1,
        0x848 + 39 * 8,
    )  # the Pokéblock case's last slot (40, 8 bytes each)
    add(
        "Minigames",
        "Level 80 Pokéblock",
        ar(
            AR.write_ptr(2, block, 0xFF0E, slot),
            AR.write_ptr(2, block, 0xFFFF, slot + 2),
            AR.write_ptr(2, block, 0xFFFF, slot + 4),
            AR.write_ptr(2, block, 0x0001, slot + 6),
        ),
        "Puts a gold Pokéblock with every flavor at its maximum in your case's last slot. Run it once.",
    )
    add(
        "Minigames",
        "Quick contests",
        cb(CB.op("EQ", 0x02002E30, 0), CB.write8(0x02002E31, 4)),
        "Contest rounds go by at once.",
    )
    add(
        "Minigames",
        "Max contest score",
        cb(CB.op("EQ", 0x02002F62, 0x082D), CB.write8(0x02002EF4, 0xFF)),
        "Your appeal score in a contest is the maximum.",
    )
    add(
        "Minigames",
        "Full Soot Sack",
        ar(AR.write_ptr(2, SB1, 9999, var_offset(0x4048))),
        "Your Soot Sack holds 9,999 steps of ash, for the glass flutes and decorations at Route 113.",
    )
    return out


def walls():
    """Hold R to pass walls: tools/golden/walls.s, over Unused_AdjustBgMosaic,
    called from CheckForObjectEventCollision in place of CheckCollision"""
    stub = (Path(__file__).with_name("walls.bin")).read_bytes()
    hook = bytes.fromhex("77f756f8")  # bl 0x08002170, at 0x0808B0C0
    return [
        (0x08002170 + i, int.from_bytes(stub[i : i + 2], "little"))
        for i in range(0, len(stub), 2)
    ] + [(0x0808B0C0 + i, int.from_bytes(hook[i : i + 2], "little")) for i in (0, 2)]


def main():
    """With --emulators: the copy for mGBA and RetroArch, without the cheats
    only Torii runs (the menus, whose code lines pass mGBA's 512 characters
    and fail the whole file there, and the keyed money and coins)"""
    emulators = "--emulators" in sys.argv[1:]
    cs = cheats()
    if emulators:
        cs = [
            c for c in cs if not (isinstance(c.code, Mixed) or "Torii only" in c.guide)
        ]
    lines = [  # no comments: mGBA reads a file only while every line starts with "cheat"
        f"cheats = {len(cs)}",
        "",
    ]
    fmt = {CB: "CodeBreaker", AR: "PARv3 raw", VBA: "VBA"}
    for i, c in enumerate(cs):
        lines.append(f'cheat{i}_desc = "{c.name}"')
        lines.append(f'cheat{i}_code = "{c.code.text()}"')
        if c.code.fmt:
            lines.append(f'cheat{i}_format = "{fmt[c.code.fmt]}"')
        lines.append(f'cheat{i}_folder = "{c.folder}"')
        if c.keys:
            lines.append(f'cheat{i}_keys = "{c.keys}"')
        lines.append(f'cheat{i}_guide = "{c.guide}"')
        lines.append(f"cheat{i}_enable = false")
        lines.append("")
    sys.stdout.write("\n".join(lines))


if __name__ == "__main__":
    main()
