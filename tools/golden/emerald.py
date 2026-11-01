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

The menu starts nothing from RAM: L+R+B opens it from the game's own field
input (tools/golden/field.c), as a sign's or a person's script opens. Its
lists are the game's scrolling list, shown by tools/golden/menu.c; its
scripts, strings and lists are in the ROM's free space, a base cheat's and
one cheat's for each section, which fills its own row of the main list.
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
    "return": 0x03,
    "goto": 0x05,
    "call": 0x04,
    "goto_if": 0x06,
    "call_if": 0x07,
    "callstd": 0x09,
    "loadword": 0x0F,
    "setvar": 0x16,
    "copyvar": 0x19,
    "setorcopyvar": 0x1A,
    "compare": 0x21,
    "callnative": 0x23,
    "special": 0x25,
    "waitstate": 0x27,
    "setflag": 0x29,
    "checkflag": 0x2B,
    "playse": 0x2F,
    "clearflag": 0x2A,
    "warp": 0x39,
    "waitmessage": 0x66,
    "message": 0x67,
    "closemessage": 0x68,
    "lockall": 0x69,
    "releaseall": 0x6B,
    "waitbuttonpress": 0x6D,
    "checkitem": 0x47,
    "givemon": 0x79,
    "giveegg": 0x7A,
    "setmonmove": 0x7B,
    "setmodernfatefulencounter": 0xCD,
    "setmonmetlocation": 0xD2,
    "specialvar": 0x26,
    "bufferspeciesname": 0x7D,
    "pokemart": 0x86,
    "pokemartdecoration": 0x87,
}
VAR_RESULT, VAR_8000, VAR_8001 = 0x800D, 0x8000, 0x8001
VAR_8004, VAR_8005 = 0x8004, 0x8005
SPECIAL = {  # indexes into gSpecials (0x081DBA64), counted from 0
    "HealPlayerParty": 0,
    "ShowPokemonStorageSystemPC": 0x3F,
    "PlayerPC": 0xFD,
    "ScriptMenu_CreatePCMultichoice": 0x109,
    "AccessHallOfFamePC": 0x10A,
    "CallFrontierUtilFunc": 0xEA,
    "ExitSafariMode": 0xD1,
    "CalculatePlayerPartyCount": 0x86,
    "CreateAbnormalWeatherEvent": 0x1EB,
    "GetAbnormalWeatherMapNameAndType": 0x1EC,
    "InitRoamer": 0x12B,
    "ChoosePartyMon": 0xA2,
    "EnableNationalPokedex": 0x1F3,
}
HOW_MAY_I_SERVE_YOU = 0x08272A2A  # gText_HowMayIServeYou past its "Welcome!" page
# The game's PC (data/scripts/pc.inc), for the menu's copy of it
SE_PC_LOGIN, SE_PC_OFF, SE_PC_ON = 2, 3, 4
MSGBOX_DEFAULT = 4  # callstd's: message, wait for A, the box kept
FLAG_SYS_GAME_CLEAR, FLAG_SYS_PC_LANETTE = 0x864, 0x8AB
TEXT_BOOT_UP_PC = 0x0827265A
TEXT_WHICH_PC = 0x0827266F  # gText_WhichPCShouldBeAccessed
TEXT_STORAGE_OPENED = 0x082726A3
TEXT_PLAYERS_PC = 0x082726C2  # gText_AccessedPlayersPC
SOMEONES_PC = 0x08271E35  # EventScript_AccessSomeonesPC: its message, then return
LANETTES_PC = 0x08271E3E  # EventScript_AccessLanettesPC
# frontier_get FRONTIER_DATA_CHALLENGE_STATUS: CallFrontierUtilFunc's
# FRONTIER_UTIL_FUNC_GET_DATA of data 0, the save's frontier.challengeStatus
FRONTIER_UTIL_FUNC_GET_DATA, FRONTIER_DATA_CHALLENGE_STATUS = 1, 0
FLAG_SYS_SAFARI_MODE = 0x88C


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

    def msgbox(self, text):  # msgbox TEXT, MSGBOX_DEFAULT: the box stays up
        self.cmd("loadword", ("b", 0), ("p", text))
        self.cmd("callstd", ("b", MSGBOX_DEFAULT))

    def say(self, label):  # message, wait for A
        self.cmd("message", ("p", label))
        self.cmd("waitmessage")
        self.cmd("waitbuttonpress")
        self.cmd("closemessage")


MENU = 0x08F00000  # tools/golden/menu.c: the routine that shows every list
MENU_CORE = 0x08F00800  # the base cheat's scripts, then the main list
SECTION_BASE = 0x08F01000  # each section's scripts, strings and lists, 0x1000 apiece
SECTION_SIZE = 0x1000
SLOTS = 12  # the main list's rows, one a section
FIELD_HOOK = 0x08001A4C  # tools/golden/field.c, over Unused_LoadBgPalette (0x88 bytes)
FIELD_CALL = 0x08085DDE  # DoCB1_Overworld's bl ProcessPlayerFieldInput
FIELD_MENU = MENU_CORE  # field.c starts the menu here, the base's first script
LIST_VALUES = 1  # menu.c's: a list whose rows hold values, one script for all


class Menu:
    """Scripts, their strings and the lists menu.c shows, in one ROM blob.
    A list is a script (callnative menu.c, the list's address, waitstate)
    then the list: its back and empty scripts, row count, cursor memory,
    and each row's name and script."""

    memories = iter(range(32))  # each list's cursor memory in menu.c

    def __init__(self, base, size=SECTION_SIZE):
        self.s = Blob(base)
        self.size = size
        self.texts = {}  # text -> label
        self.lists = []  # (label, back, empty, rows)

    def t(self, s):
        if s not in self.texts:
            self.texts[s] = f"t{len(self.texts)}"
        return self.texts[s]

    def list(self, name, rows, back, empty=None, count=None, run=None):
        """rows: (name, script label), a name text or the address of the
        game's own (an int). With run, a list of values (menu.c's
        LIST_VALUES): each row's second item is a value, put in VAR_0x8004
        (low half) and VAR_0x8005 as the script run starts."""
        s = self.s
        s.label(name)
        s.cmd("callnative", ("w", MENU | 1))
        s.ptr(f"{name}.list")
        s.cmd("waitstate")
        rows = [(r if isinstance(r, int) else self.t(r), target) for r, target in rows]
        n = len(rows) if count is None else count
        flags = LIST_VALUES if run else 0
        self.lists.append(
            (name, back, run or empty or back, rows, n, next(Menu.memories), flags)
        )

    def blob(self):
        """The blob as ROM patches: scripts, then strings, then lists"""
        s = self.s
        for txt, lbl in self.texts.items():
            s.label(lbl)
            s.raw(text(txt))
        s.align()
        for name, back, empty, rows, count, memory, flags in self.lists:
            s.label(f"{name}.list")
            s.ptr(back)
            s.ptr(empty)
            s.b(count, memory, flags, 0)
            for title, target in rows:
                s.ptr(title)
                s.ptr(target)
        data = s.bytes()
        assert len(data) <= self.size, (hex(s.base), len(data))
        return [
            (s.base + i, int.from_bytes(data[i : i + 2], "little"))
            for i in range(0, len(data), 2)
        ]


def rom_code(patches):
    """ROM patches, (address, halfword) pairs, as Torii lines: a word a line
    where two halfwords pair up at a word's address"""
    return vba(*Mixed(patches, None).lines)


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
        "BLUE_FLUTE",
        "YELLOW_FLUTE",
        "RED_FLUTE",
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
        "BLACK_FLUTE",
        "WHITE_FLUTE",
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
    "MAIL": [
        "ORANGE_MAIL",
        "HARBOR_MAIL",
        "GLITTER_MAIL",
        "MECH_MAIL",
        "WOOD_MAIL",
        "WAVE_MAIL",
        "BEAD_MAIL",
        "SHADOW_MAIL",
        "TROPIC_MAIL",
        "DREAM_MAIL",
        "FAB_MAIL",
        "RETRO_MAIL",
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
NUM_DECORATIONS = 120  # DECOR_SMALL_DESK (1) to DECOR_REGISTEEL_DOLL
TICKETS = [  # name, item, the flags its Mystery Gift sets: the ferry's (FLAG_ENABLE_SHIP_*),
    # and its FLAG_RECEIVED_* (the Eon Ticket's gift sets none). Lilycove's
    # ferry takes you with the ticket in the bag and its ferry flag set,
    # once the game is clear.
    ("EON TICKET", "EON_TICKET", [0x8B3]),
    ("AURORA TICKET", "AURORA_TICKET", [0x8D5, 0x13A]),
    ("OLD SEA MAP", "OLD_SEA_MAP", [0x8D6, 0x13C]),
    ("MYSTIC TICKET", "MYSTIC_TICKET", [0x8E0, 0x13B]),
]


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
ROUTES = [  # name, map group, map number, x, y: picked from pokeemerald's layouts, walking
    # in from the route's edge toward a town (else a neighbour) at one height: land
    # there, else (and first on ocean routes) water, where the game puts you surfing
    ("ROUTE 101", 0, 16, 8, 2),
    ("ROUTE 102", 0, 17, 2, 8),
    ("ROUTE 103", 0, 18, 11, 19),
    ("ROUTE 104", 0, 19, 17, 2),
    ("ROUTE 105", 0, 20, 17, 2),  # surfing
    ("ROUTE 106", 0, 21, 68, 17),
    ("ROUTE 107", 0, 22, 2, 10),
    ("ROUTE 108", 0, 23, 2, 8),  # surfing
    ("ROUTE 109", 0, 24, 17, 2),
    ("ROUTE 110", 0, 25, 17, 2),
    ("ROUTE 111", 0, 26, 19, 137),
    ("ROUTE 112", 0, 27, 2, 49),
    ("ROUTE 113", 0, 28, 2, 8),
    ("ROUTE 114", 0, 29, 37, 15),
    ("ROUTE 115", 0, 30, 21, 77),
    ("ROUTE 116", 0, 31, 2, 8),
    ("ROUTE 117", 0, 32, 2, 8),
    ("ROUTE 118", 0, 33, 2, 8),
    ("ROUTE 119", 0, 34, 37, 64),
    ("ROUTE 120", 0, 35, 2, 51),
    ("ROUTE 121", 0, 36, 77, 9),
    ("ROUTE 122", 0, 37, 20, 37),
    ("ROUTE 123", 0, 38, 119, 2),
    ("ROUTE 124", 0, 39, 2, 43),  # surfing
    ("ROUTE 125", 0, 40, 41, 37),  # surfing
    ("ROUTE 126", 0, 41, 36, 2),  # surfing
    ("ROUTE 127", 0, 42, 38, 2),  # surfing
    ("ROUTE 128", 0, 43, 117, 20),  # surfing
    ("ROUTE 129", 0, 44, 36, 2),  # surfing
    ("ROUTE 130", 0, 45, 2, 17),  # surfing
    ("ROUTE 131", 0, 46, 2, 17),  # surfing
    ("ROUTE 132", 0, 47, 77, 21),  # surfing
    ("ROUTE 133", 0, 48, 2, 0),  # surfing
    ("ROUTE 134", 0, 49, 2, 18),  # surfing
]
DUNGEONS = [  # name, map group, map number: arriving by the map's first warp
    ("METEOR FALLS", 24, 0),
    ("PETALBURG WOODS", 24, 11),
    ("RUSTURF TUNNEL", 24, 4),
    ("GRANITE CAVE", 24, 7),
    ("FIERY PATH", 24, 14),
    ("JAGGED PASS", 24, 13),
    ("MT. CHIMNEY", 24, 12),
    ("MAGMA HIDEOUT", 24, 86),
    ("MIRAGE TOWER", 24, 94),
    ("DESERT UNDERPASS", 24, 98),
    ("MT. PYRE", 24, 22),
    ("AQUA HIDEOUT", 24, 23),
    ("NEW MAUVILLE", 24, 52),
    ("ABANDONED SHIP", 24, 54),
    ("SAFARI ZONE", 26, 3),
    ("SCORCHED SLAB", 24, 73),
    ("SHOAL CAVE", 24, 46),
    ("SEAFLOOR CAVERN", 24, 27),
    ("CAVE OF ORIGIN", 24, 37),
    ("SKY PILLAR", 24, 85),
    ("SEALED CHAMBER", 24, 71),
    ("VICTORY ROAD", 24, 43),
    ("ALTERING CAVE", 24, 106),
    ("DESERT RUINS", 24, 6),
    ("ISLAND CAVE", 24, 67),
    ("ANCIENT TOMB", 24, 68),
    ("ARTISAN CAVE", 24, 99),
]  # Terra and Marine Cave exist only while their weather lasts, their way out a dynamic warp
GYMS = [  # name, map group, map number: arriving at the door, by the map's first warp
    ("RUSTBORO GYM", 11, 3),
    ("DEWFORD GYM", 3, 3),
    ("MAUVILLE GYM", 10, 0),
    ("LAVARIDGE GYM", 4, 1),
    ("PETALBURG GYM", 8, 1),
    ("FORTREE GYM", 12, 1),
    ("MOSSDEEP GYM", 14, 0),
    ("SOOTOPOLIS GYM", 15, 0),
    ("POKéMON LEAGUE", 16, 10),
]
PLACES = [  # name, map group, map number: arriving at the door, by the map's first warp
    ("BIRCH'S LAB", 1, 4),
    ("DEVON CORP.", 11, 0),
    ("OCEANIC MUSEUM", 9, 7),
    ("BIKE SHOP", 10, 1),
    ("GAME CORNER", 10, 3),
    ("DAY CARE", 22, 0),
    ("FOSSIL MANIAC", 20, 0),
    ("TRICK HOUSE", 29, 0),
    ("WEATHER INSTITUTE", 32, 0),
    ("DEPT. STORE", 13, 16),
    ("CONTEST HALL", 13, 4),
    ("LILYCOVE MUSEUM", 13, 2),
    ("SPACE CENTER", 14, 9),
    ("TRAINER HILL", 26, 60),
    ("BATTLE TOWER", 26, 14),
]
ISLANDS = [  # name, map group, map number, x, y: where the ferry leaves you
    ("BATTLE FRONTIER", 26, 4, 19, 67),
    ("SOUTHERN ISLAND", 26, 9, 13, 22),
    ("BIRTH ISLAND", 26, 59, 8, 4),
    ("FARAWAY ISLAND", 26, 56, 13, 38),
    ("NAVEL ROCK", 26, 67, 8, 4),
]
LEGENDS = [  # name, flags to clear, where it waits: as each lair's map script shows it again
    ("RAYQUAZA", [0x1C0], "atop SKY PILLAR"),  # FLAG_DEFEATED_RAYQUAZA
    (
        "GROUDON",
        [0x1BF],
        None,
    ),  # FLAG_DEFEATED_GROUDON: in Terra Cave, while its sun lasts
    (
        "KYOGRE",
        [0x1BE],
        None,
    ),  # FLAG_DEFEATED_KYOGRE: in Marine Cave, while its rain lasts
    # The Regis' rows also open their way in, as the Sealed Chamber and each
    # braille puzzle do (REGI_OPEN)
    ("REGIROCK", [0x1BB, 0x3A7], "in the DESERT RUINS"),  # defeated, hidden
    ("REGICE", [0x1BC, 0x3A8], "in the ISLAND CAVE"),
    ("REGISTEEL", [0x1BD, 0x3A9], "in the ANCIENT TOMB"),
    ("LATI ON ISLAND", [0x1C8, 0x1C9], "on SOUTHERN ISLAND"),  # defeated, caught
    ("ROAMING LATI", [], None),  # InitRoamer, as Mom's TV news does
    ("MEW", [0x1C7, 0x1CA], "on FARAWAY ISLAND"),  # defeated, caught
    (
        "DEOXYS",
        [0x1AC, 0x1AD],
        "on BIRTH ISLAND",
    ),  # defeated, battled: its puzzle shows again
    ("LUGIA", [0x1DD, 0x91, 0x320], "deep in NAVEL ROCK"),  # defeated, caught, hidden
    ("HO-OH", [0x1DC, 0x92, 0x321], "atop NAVEL ROCK"),
]
# FLAG_REGI_DOORS_OPENED (the routes' entrances) and the chamber's own
# FLAG_SYS_*_PUZZLE_COMPLETED (its inner wall)
REGI_OPEN = {
    "REGIROCK": [0xE4, 0x8B0],
    "REGICE": [0xE4, 0x8B1],
    "REGISTEEL": [0xE4, 0x8B2],
}
VAR_ROAMER_POKEMON = (
    0x40D5  # the roaming one: 0 Latias, 1 Latios (Southern Island has the other)
)
FLAG_LATIOS_OR_LATIAS_ROAMING = 0xFF
FLAG_ENABLE_SHIP_SOUTHERN_ISLAND = 0x8B3
FLAG_DEFEATED_GROUDON, FLAG_DEFEATED_KYOGRE = 0x1BF, 0x1BE


def species(name):
    for sid, n in DATA["species"]:
        if n == name:
            return sid
    raise KeyError(name)


GIFTS = [  # name, species, level (0: an egg), held item: as the game's own gift scripts give them
    ("TREECKO", "TREECKO", 5, None),
    ("TORCHIC", "TORCHIC", 5, None),
    ("MUDKIP", "MUDKIP", 5, None),
    ("CHIKORITA", "CHIKORITA", 5, None),
    ("CYNDAQUIL", "CYNDAQUIL", 5, None),
    ("TOTODILE", "TOTODILE", 5, None),
    ("BELDUM", "BELDUM", 5, None),
    ("CASTFORM", "CASTFORM", 25, "MYSTIC_WATER"),
    ("LILEEP", "LILEEP", 20, None),
    ("ANORITH", "ANORITH", 20, None),
    ("WYNAUT EGG", "WYNAUT", 0, None),
    # The Pokémon Center's Mystery Gift: Surf, a fateful encounter
    ("PICHU EGG", "PICHU", 0, None),
    # Events of other games, as they came to the GBA games, each a fateful
    # encounter: the US Colosseum bonus disc's WISHMKR Jirachi (level 5:
    # Wish, Confusion, Rest) and the Japanese one's Ageto Celebi (level 10:
    # Confusion, Recover, Heal Bell, Safeguard), the moves givemon gives
    ("JIRACHI", "JIRACHI", 5, None),
    ("CELEBI", "CELEBI", 10, None),
]
EVENT_GIFTS = {
    "PICHU",
    "JIRACHI",
    "CELEBI",
}  # into the party only, then marked as an event's
MOVE_SURF = 57
# The Mystery Gift's other news: which of the cave's 9 wild sets lives in it
VAR_ALTERING_CAVE_WILD_SET = 0x403E
ALTERING_CAVE = [  # its wild sets in order (gAlteringCave1 to 9)
    "ZUBAT",
    "MAREEP",
    "PINECO",
    "HOUNDOUR",
    "TEDDIURSA",
    "AIPOM",
    "SHUCKLE",
    "STANTLER",
    "SMEARGLE",
]
METLOC_FATEFUL_ENCOUNTER = 0xFF


# The game's own names, which the menus' lists point at instead of copies
# (pokeemerald.sym): each place's region-map name, a species' and an item's
SPECIES_NAMES = 0x083185C8  # gSpeciesNames, 11 bytes each
ITEMS = 0x085839A0  # gItems, 44 bytes each, the name first
MAP_NAMES = {
    "LITTLEROOT": 0x085A0B10,  # sMapName_LITTLEROOT_TOWN
    "OLDALE": 0x085A0B20,  # sMapName_OLDALE_TOWN
    "PETALBURG": 0x085A0B77,  # sMapName_PETALBURG_CITY
    "RUSTBORO": 0x085A0BA3,  # sMapName_RUSTBORO_CITY
    "DEWFORD": 0x085A0B2C,  # sMapName_DEWFORD_TOWN
    "SLATEPORT": 0x085A0B86,  # sMapName_SLATEPORT_CITY
    "MAUVILLE": 0x085A0B95,  # sMapName_MAUVILLE_CITY
    "VERDANTURF": 0x085A0B57,  # sMapName_VERDANTURF_TOWN
    "FALLARBOR": 0x085A0B48,  # sMapName_FALLARBOR_TOWN
    "LAVARIDGE": 0x085A0B39,  # sMapName_LAVARIDGE_TOWN
    "FORTREE": 0x085A0BB1,  # sMapName_FORTREE_CITY
    "LILYCOVE": 0x085A0BBE,  # sMapName_LILYCOVE_CITY
    "MOSSDEEP": 0x085A0BCC,  # sMapName_MOSSDEEP_CITY
    "SOOTOPOLIS": 0x085A0BDA,  # sMapName_SOOTOPOLIS_CITY
    "PACIFIDLOG": 0x085A0B67,  # sMapName_PACIFIDLOG_TOWN
    "EVER GRANDE": 0x085A0BEA,  # sMapName_EVER_GRANDE_CITY
    "METEOR FALLS": 0x085A0DCA,  # sMapName_METEOR_FALLS
    "GRANITE CAVE": 0x085A0D5A,  # sMapName_GRANITE_CAVE
    "FIERY PATH": 0x085A0E40,  # sMapName_FIERY_PATH
    "MT. CHIMNEY": 0x085A0D67,  # sMapName_MT__CHIMNEY
    "MT. PYRE": 0x085A0DD7,  # sMapName_MT__PYRE
    "NEW MAUVILLE": 0x085A0DBD,  # sMapName_NEW_MAUVILLE
    "ABANDONED SHIP": 0x085A0DAE,  # sMapName_ABANDONED_SHIP
    "SAFARI ZONE": 0x085A0D73,  # sMapName_SAFARI_ZONE
    "SHOAL CAVE": 0x085A0DEB,  # sMapName_SHOAL_CAVE
    "SEAFLOOR CAVERN": 0x085A0DF6,  # sMapName_SEAFLOOR_CAVERN
    "CAVE OF ORIGIN": 0x085A0E21,  # sMapName_CAVE_OF_ORIGIN
    "SKY PILLAR": 0x085A0EAA,  # sMapName_SKY_PILLAR
    "VICTORY ROAD": 0x085A0E06,  # sMapName_VICTORY_ROAD
    "DESERT RUINS": 0x085A0E80,  # sMapName_DESERT_RUINS
    "ISLAND CAVE": 0x085A0E74,  # sMapName_ISLAND_CAVE
    "ANCIENT TOMB": 0x085A0E8D,  # sMapName_ANCIENT_TOMB
    "ARTISAN CAVE": 0x085A1438,  # sMapName_ARTISAN_CAVE
    "BATTLE FRONTIER": 0x085A0D7F,  # sMapName_BATTLE_FRONTIER
    "SOUTHERN ISLAND": 0x085A0E30,  # sMapName_SOUTHERN_ISLAND
    "BIRTH ISLAND": 0x085A1374,  # sMapName_BIRTH_ISLAND
    "FARAWAY ISLAND": 0x085A1429,  # sMapName_FARAWAY_ISLAND
    "NAVEL ROCK": 0x085A12CA,  # sMapName_NAVEL_ROCK
    "ROUTE 101": 0x085A0BFB,  # sMapName_ROUTE_101
    "ROUTE 102": 0x085A0C05,  # sMapName_ROUTE_102
    "ROUTE 103": 0x085A0C0F,  # sMapName_ROUTE_103
    "ROUTE 104": 0x085A0C19,  # sMapName_ROUTE_104
    "ROUTE 105": 0x085A0C23,  # sMapName_ROUTE_105
    "ROUTE 106": 0x085A0C2D,  # sMapName_ROUTE_106
    "ROUTE 107": 0x085A0C37,  # sMapName_ROUTE_107
    "ROUTE 108": 0x085A0C41,  # sMapName_ROUTE_108
    "ROUTE 109": 0x085A0C4B,  # sMapName_ROUTE_109
    "ROUTE 110": 0x085A0C55,  # sMapName_ROUTE_110
    "ROUTE 111": 0x085A0C5F,  # sMapName_ROUTE_111
    "ROUTE 112": 0x085A0C69,  # sMapName_ROUTE_112
    "ROUTE 113": 0x085A0C73,  # sMapName_ROUTE_113
    "ROUTE 114": 0x085A0C7D,  # sMapName_ROUTE_114
    "ROUTE 115": 0x085A0C87,  # sMapName_ROUTE_115
    "ROUTE 116": 0x085A0C91,  # sMapName_ROUTE_116
    "ROUTE 117": 0x085A0C9B,  # sMapName_ROUTE_117
    "ROUTE 118": 0x085A0CA5,  # sMapName_ROUTE_118
    "ROUTE 119": 0x085A0CAF,  # sMapName_ROUTE_119
    "ROUTE 120": 0x085A0CB9,  # sMapName_ROUTE_120
    "ROUTE 121": 0x085A0CC3,  # sMapName_ROUTE_121
    "ROUTE 122": 0x085A0CCD,  # sMapName_ROUTE_122
    "ROUTE 123": 0x085A0CD7,  # sMapName_ROUTE_123
    "ROUTE 124": 0x085A0CE1,  # sMapName_ROUTE_124
    "ROUTE 125": 0x085A0CEB,  # sMapName_ROUTE_125
    "ROUTE 126": 0x085A0CF5,  # sMapName_ROUTE_126
    "ROUTE 127": 0x085A0CFF,  # sMapName_ROUTE_127
    "ROUTE 128": 0x085A0D09,  # sMapName_ROUTE_128
    "ROUTE 129": 0x085A0D13,  # sMapName_ROUTE_129
    "ROUTE 130": 0x085A0D1D,  # sMapName_ROUTE_130
    "ROUTE 131": 0x085A0D27,  # sMapName_ROUTE_131
    "ROUTE 132": 0x085A0D31,  # sMapName_ROUTE_132
    "ROUTE 133": 0x085A0D3B,  # sMapName_ROUTE_133
    "ROUTE 134": 0x085A0D45,  # sMapName_ROUTE_134
    "PETALBURG WOODS": 0x085A0D8F,  # sMapName_PETALBURG_WOODS
    "RUSTURF TUNNEL": 0x085A0D9F,  # sMapName_RUSTURF_TUNNEL
    "JAGGED PASS": 0x085A0E4B,  # sMapName_JAGGED_PASS
    "MAGMA HIDEOUT": 0x085A140E,  # sMapName_MAGMA_HIDEOUT
    "MIRAGE TOWER": 0x085A141C,  # sMapName_MIRAGE_TOWER
    "DESERT UNDERPASS": 0x085A145C,  # sMapName_DESERT_UNDERPASS
    "AQUA HIDEOUT": 0x085A1401,  # sMapName_AQUA_HIDEOUT
    "SCORCHED SLAB": 0x085A0E66,  # sMapName_SCORCHED_SLAB
    "SEALED CHAMBER": 0x085A0E57,  # sMapName_SEALED_CHAMBER
    "ALTERING CAVE": 0x085A133B,  # sMapName_ALTERING_CAVE
}


def species_name(name):
    return SPECIES_NAMES + 11 * species(name)


def item_name(item):
    return ITEMS + 44 * ITEM[item]


def field_hook():
    """L+R+B opens the menu where the game opens its own scripts
    (tools/golden/field.c): the routine, and DoCB1_Overworld's call to
    ProcessPlayerFieldInput sent through it"""
    code = Path(__file__).with_name("field.bin").read_bytes()
    assert len(code) <= 0x88, "field.bin runs past Unused_LoadBgPalette"
    code += b"\0" * (len(code) & 1)
    off = FIELD_HOOK - (FIELD_CALL + 4)
    bl = (0xF000 | (off >> 12) & 0x7FF, 0xF800 | (off >> 1) & 0x7FF)
    return [
        (FIELD_HOOK + i, int.from_bytes(code[i : i + 2], "little"))
        for i in range(0, len(code), 2)
    ] + [(FIELD_CALL, bl[0]), (FIELD_CALL + 2, bl[1])]


def menu_base():
    """The menu's base: L+R+B, the routine that shows its lists and the main
    list, laid out last: its rows are slots past the base's end that each
    section's cheat fills (the ROM's free space, 0xFF, is unset and left
    out). Returns the patches, the main list's script (a section's way back)
    and the slots' address."""
    code = Path(__file__).with_name("menu.bin").read_bytes()
    assert len(code) <= MENU_CORE - MENU, "menu.bin runs into the base's scripts"
    code += b"\0" * (len(code) & 1)
    m = Menu(MENU_CORE, SECTION_BASE - MENU_CORE - 8 * SLOTS)
    s = m.s
    s.label("open")
    s.cmd("lockall")
    # Never during a Battle Frontier or Battle Tent challenge: the party is
    # then the challenge's (picked, or rented), the player's own kept in the
    # save until it ends (frontier.challengeStatus is set from before the
    # party changes until after it is back). A warp out, then a save, would
    # write the challenge's party over the player's; a Pokémon boxed or
    # given then would be lost or doubled when the party comes back.
    s.cmd("setvar", ("h", VAR_8004), ("h", FRONTIER_UTIL_FUNC_GET_DATA))
    s.cmd("setvar", ("h", VAR_8005), ("h", FRONTIER_DATA_CHALLENGE_STATUS))
    s.cmd("special", ("h", SPECIAL["CallFrontierUtilFunc"]))
    s.when(VAR_RESULT, 0, "main")
    s.say(m.t("The menu can't be used\nduring a challenge."))
    s.goto("done")
    m.list("main", [], "done", "empty", count=SLOTS)
    s.label("done")
    s.cmd("releaseall")
    s.cmd("end")
    s.label("empty")
    s.say(m.t("No menu sections are picked.\nPick them in Torii's Menu cheats."))
    s.goto("done")
    patches = m.blob()
    assert s.addr("open") == FIELD_MENU, (
        "tools/golden/field.c's MENU starts the menu here"
    )
    patches += [
        (MENU + i, int.from_bytes(code[i : i + 2], "little"))
        for i in range(0, len(code), 2)
    ]
    return patches + field_hook(), s.addr("main"), s.base + s.size()


SECTIONS = []  # (slot, cheat name, guide, build(menu, back) -> entry label)


def section(name, guide):
    def add(build):
        SECTIONS.append((len(SECTIONS), name, guide, build))
        return build

    return add


@section("Heal", "Heals your party, as a Pokémon Center does.")
def heal_section(m, back):
    s = m.s
    s.label("heal")
    s.cmd("special", ("h", SPECIAL["HealPlayerParty"]))
    s.say(m.t("Your POKéMON are fully healed."))
    s.goto(back)
    return "HEAL", "heal"


@section("PC", "Opens the PC: your boxes, your items, Hall of Fame.")
def pc_section(m, back):
    """The game's PC script (EventScript_PC on), command for command, but for
    its turn-on and turn-off effects: those draw the PC's screen on the tile
    the player faces (DoPCTurnOffEffect writes an impassable PC there, or
    under the player facing down), and the menu opens away from any PC.
    Logging off goes back to the main list. The storage, the item PC and
    the Hall of Fame come back to the field into this copy, as into the
    game's. (The menu isn't opened during a Frontier challenge.)"""
    s = m.s
    s.label("pc")
    s.cmd("playse", ("h", SE_PC_ON))
    s.msgbox(TEXT_BOOT_UP_PC)
    s.label("pc_menu")  # EventScript_PCMainMenu
    s.cmd("message", ("p", TEXT_WHICH_PC))
    s.cmd("waitmessage")
    s.cmd("special", ("h", SPECIAL["ScriptMenu_CreatePCMultichoice"]))
    s.cmd("waitstate")
    s.label("pc_access")  # EventScript_AccessPC
    s.cmd("copyvar", ("h", VAR_8000), ("h", VAR_RESULT))
    s.when(VAR_8000, 0, "pc_storage")
    s.when(VAR_8000, 1, "pc_items")
    s.when(VAR_8000, 2, "pc_hof")
    s.goto("pc_off")  # 3, LOG OFF, and B (MULTI_B_PRESSED)
    s.label("pc_items")  # EventScript_AccessPlayersPC
    s.cmd("playse", ("h", SE_PC_LOGIN))
    s.msgbox(TEXT_PLAYERS_PC)
    s.cmd("special", ("h", SPECIAL["PlayerPC"]))
    s.cmd("waitstate")
    s.goto("pc_menu")
    s.label("pc_storage")  # EventScript_AccessPokemonStorage
    s.cmd("playse", ("h", SE_PC_LOGIN))
    s.cmd("checkflag", ("h", FLAG_SYS_PC_LANETTE))
    s.cmd("call_if", ("b", 0), ("p", SOMEONES_PC))
    s.cmd("checkflag", ("h", FLAG_SYS_PC_LANETTE))
    s.cmd("call_if", ("b", 1), ("p", LANETTES_PC))
    s.msgbox(TEXT_STORAGE_OPENED)
    s.cmd("special", ("h", SPECIAL["ShowPokemonStorageSystemPC"]))
    s.cmd("waitstate")
    s.goto("pc_menu")
    s.label("pc_hof")  # EventScript_AccessHallOfFame
    s.cmd("checkflag", ("h", FLAG_SYS_GAME_CLEAR))
    s.cmd("goto_if", ("b", 0), ("p", "pc_off"))
    s.cmd("playse", ("h", SE_PC_LOGIN))
    s.cmd("special", ("h", SPECIAL["AccessHallOfFamePC"]))
    s.cmd("waitstate")
    s.goto("pc_access")
    s.label("pc_off")  # EventScript_TurnOffPC
    s.cmd("playse", ("h", SE_PC_OFF))
    s.cmd("closemessage")
    s.goto(back)
    return "PC", "pc"


@section(
    "Shop",
    "A Poké Mart for each kind of item (Medicine, Balls, Training, TMs & HMs, Held items, Mail, "
    "Berries, Valuables) and one for every decoration. Game prices; sell there too.",
)
def shop_section(m, back):
    s = m.s
    rows = [(name, f"mart{i}") for i, name in enumerate(MART)] + [
        ("DECORATIONS", "decor")
    ]
    m.list("shop", rows, back)
    for i in range(len(MART)):
        s.label(f"mart{i}")
        s.cmd("message", ("p", HOW_MAY_I_SERVE_YOU))
        s.cmd("waitmessage")
        s.cmd("pokemart", ("p", f"items{i}"))
        s.cmd("closemessage")
        s.goto("shop")
    s.label("decor")
    s.cmd("message", ("p", HOW_MAY_I_SERVE_YOU))
    s.cmd("waitmessage")
    s.cmd("pokemartdecoration", ("p", "decorations"))
    s.cmd("closemessage")
    s.goto("shop")
    s.align(2)
    for i, items in enumerate(MART.values()):
        s.label(f"items{i}")
        for it in items:
            s.h(ITEM[it])
        s.h(0)
    s.label("decorations")
    for d in range(1, NUM_DECORATIONS + 1):
        s.h(d)
    s.h(0)
    return "SHOP", "shop"


@section(
    "Warp",
    "Takes you to any town (outside its Pokémon Center), route, dungeon, gym, landmark or island you choose.",
)
def warp_section(m, back):
    s = m.s
    m.list(
        "warp",
        [
            ("TOWNS", "towns"),
            ("ROUTES", "routes"),
            ("DUNGEONS", "dungeons"),
            ("GYMS", "gyms"),
            ("PLACES", "places"),
            ("ISLANDS", "islands"),
        ],
        back,
    )
    # Each list's rows are values for tools/golden/warp.c (menu.c's
    # LIST_VALUES): the map's group and number, then x and y, or x 0xFF for
    # the map's first warp (its door), so a place costs its row alone
    kinds = (
        ("towns", TOWNS, True),
        ("routes", ROUTES, True),
        ("dungeons", DUNGEONS, False),
        ("gyms", GYMS, False),
        ("places", PLACES, False),
        ("islands", ISLANDS, True),
    )
    for name, rows, at in kinds:
        values = []
        for n, group, num, *xy in rows:
            x, y = xy if at else (0xFF, 0xFF)
            assert x < 0xFF and y < 0x100 if at else True, (n, x, y)
            values.append(
                (
                    MAP_NAMES[n] if n in MAP_NAMES else m.t(n),
                    group | num << 8 | x << 16 | y << 24,
                )
            )
        m.list(name, values, "warp", run="go")
    # Out of the Safari Zone as its gate does (ExitSafariMode): its flag
    # left on would make battles outside Safari battles until its steps run
    # out. Then the warp, as the game's warp command does it.
    s.label("go")
    s.cmd("checkflag", ("h", FLAG_SYS_SAFARI_MODE))
    s.cmd("goto_if", ("b", 0), ("p", "left"))
    s.cmd("special", ("h", SPECIAL["ExitSafariMode"]))
    s.label("left")
    s.cmd("callnative", ("p", "warp_to+1"))
    s.cmd("waitstate")
    s.align()
    s.label("warp_to")
    s.raw(Path(__file__).with_name("warp.bin").read_bytes())
    return "WARP", "warp"


@section(
    "Gifts",
    "The game's gift Pokémon and eggs, again: the starters, fossils, Castform, Beldum, the Wynaut egg, and the events' Surfing Pichu egg, Jirachi and Celebi. ALTERING CAVE picks which Pokémon live in the cave on Route 103.",
)
def gifts_section(m, back):
    s = m.s
    m.list(
        "gifts",
        [
            (species_name(sp) if level else n, f"gf{i}")
            for i, (n, sp, level, _) in enumerate(GIFTS)
        ]
        + [("ALTERING CAVE", "cave")],
        back,
    )
    # MysteryGiftScript_AlteringCave steps the cave to its next set; here
    # its set is picked by the species that lives there
    m.list(
        "cave",
        [(species_name(sp), f"cave{k}") for k, sp in enumerate(ALTERING_CAVE)],
        "gifts",
    )
    for k, sp in enumerate(ALTERING_CAVE):
        s.label(f"cave{k}")
        s.cmd("setvar", ("h", VAR_ALTERING_CAVE_WILD_SET), ("h", k))
        s.cmd("bufferspeciesname", ("b", 0), ("h", species(sp)))
        s.say(m.t("{STR1} now lives in\nthe ALTERING CAVE."))
        s.goto("gifts")
    for i, (_, sp, level, item) in enumerate(GIFTS):
        s.label(f"gf{i}")
        if sp in EVENT_GIFTS:
            # As MysteryGiftScript_SurfPichu gives its egg: into the party
            # only (the new slot, VAR_0x8000, is then marked a fateful
            # encounter met at one), as the gift asks for a free place
            s.cmd(
                "specialvar",
                ("h", VAR_8000),
                ("h", SPECIAL["CalculatePlayerPartyCount"]),
            )
            s.when(VAR_8000, 6, "party_full")
        if sp == "PICHU":
            s.cmd("giveegg", ("h", species(sp)))
            s.cmd(
                "setmonmove", ("b", 7), ("b", 2), ("h", MOVE_SURF)
            )  # past 6: the party's last
        elif level:
            s.cmd(
                "givemon",
                ("h", species(sp)),
                ("b", level),
                ("h", ITEM[item] if item else 0),
                ("w", 0),
                ("w", 0),
                ("b", 0),
            )
        else:
            s.cmd("giveegg", ("h", species(sp)))
        if sp in EVENT_GIFTS:
            s.cmd("setmodernfatefulencounter", ("h", VAR_8000))
            s.cmd("setmonmetlocation", ("h", VAR_8000), ("b", METLOC_FATEFUL_ENCOUNTER))
        s.when(VAR_RESULT, 2, "full")
        s.cmd("bufferspeciesname", ("b", 0), ("h", species(sp)))
        s.when(VAR_RESULT, 1, "pc_egg" if not level else "pc_mon")
        s.say(
            m.t(
                "{PLAYER} received an EGG!"
                if not level
                else "{PLAYER} received {STR1}!"
            )
        )
        s.goto("gifts")
    s.label("pc_mon")
    s.say(m.t("{STR1} was sent to the PC."))
    s.goto("gifts")
    s.label("pc_egg")
    s.say(m.t("The EGG was sent to the PC."))
    s.goto("gifts")
    s.label("full")
    s.say(m.t("There is no room for it."))
    s.goto("gifts")
    s.label("party_full")
    s.say(m.t("It needs a place\nin your party."))
    s.goto("gifts")
    return "GIFTS", "gifts"


@section(
    "Trade evolution",
    "Evolves a party POKéMON as a trade would: KADABRA, MACHOKE, GRAVELER, HAUNTER, and those "
    "holding their item (KING'S ROCK, METAL COAT, DRAGON SCALE, UP-GRADE, DEEPSEATOOTH or "
    "DEEPSEASCALE), which it uses up.",
)
def evolve_section(m, back):
    """The game's party screen (ChoosePartyMon) picks the POKéMON, then
    tools/golden/evolve.c evolves it as a trade would and runs the
    evolution scene, which comes back to the field with the script going
    on (VAR_RESULT 1); with no trade evolution it says so (VAR_RESULT 0)."""
    s = m.s
    s.label("evolve")
    s.cmd("special", ("h", SPECIAL["ChoosePartyMon"]))
    s.cmd("waitstate")
    s.cmd("callnative", ("p", "evolve_mon+1"))  # nothing chosen: VAR_RESULT 0 too
    s.when(VAR_RESULT, 0, "evolve_no")
    s.cmd("waitstate")
    s.goto(back)
    s.label("evolve_no")
    s.cmd("compare", ("h", VAR_8004), ("h", 6))
    s.cmd("goto_if", ("b", 4), ("p", back))  # >= 6: nothing chosen
    s.say(m.t("It doesn't evolve by trading."))
    s.goto(back)
    s.align()
    s.label("evolve_mon")
    s.raw(Path(__file__).with_name("evolve.bin").read_bytes())
    return "TRADE EVOLVE", "evolve"


@section(
    "Legends",
    "Brings back a legendary Pokémon you defeated or caught, in its lair (a REGI's opened). "
    "GROUDON and KYOGRE wait in a cave on the route named, while its weather lasts. The roaming "
    "LATIAS or LATIOS roams again.",
)
def legends_section(m, back):
    s = m.s
    m.list(
        "legends",
        [
            (n if " " in n else species_name(n), f"lg{i}")
            for i, (n, *_) in enumerate(LEGENDS)
        ],
        back,
    )
    for i, (name, flags, where) in enumerate(LEGENDS):
        s.label(f"lg{i}")
        for f in flags:
            s.cmd("clearflag", ("h", f))
        for f in REGI_OPEN.get(name, []):
            s.cmd("setflag", ("h", f))
        if name in ("GROUDON", "KYOGRE"):
            # As the Weather Institute's scientist does after the credits
            # (CreateAbnormalWeatherEvent): it picks Groudon's sun while
            # Kyogre counts as defeated, Kyogre's rain while Groudon does,
            # so the other's flag is set for the call and put back after
            other = FLAG_DEFEATED_KYOGRE if name == "GROUDON" else FLAG_DEFEATED_GROUDON
            s.cmd("checkflag", ("h", other))
            s.cmd("goto_if", ("b", 1), ("p", f"lg{i}_set"))
            s.cmd("setflag", ("h", other))
            s.cmd("special", ("h", SPECIAL["CreateAbnormalWeatherEvent"]))
            s.cmd("clearflag", ("h", other))
            s.goto(f"lg{i}_say")
            s.label(f"lg{i}_set")
            s.cmd("special", ("h", SPECIAL["CreateAbnormalWeatherEvent"]))
            s.label(f"lg{i}_say")
            s.cmd(
                "specialvar",
                ("h", VAR_RESULT),
                ("h", SPECIAL["GetAbnormalWeatherMapNameAndType"]),
            )  # the route into {STR1}
            s.say(
                m.t(
                    "Harsh sun beats down on\n{STR1}: GROUDON is there."
                    if name == "GROUDON"
                    else "Heavy rain falls on\n{STR1}: KYOGRE is there."
                )
            )
        elif name == "ROAMING LATI":
            s.cmd("copyvar", ("h", VAR_8004), ("h", VAR_ROAMER_POKEMON))
            s.cmd("special", ("h", SPECIAL["InitRoamer"]))
            s.cmd("setflag", ("h", FLAG_LATIOS_OR_LATIAS_ROAMING))
            s.cmd("bufferspeciesname", ("b", 0), ("h", species("LATIAS")))
            s.when(VAR_ROAMER_POKEMON, 0, f"lg{i}_say")
            s.cmd("bufferspeciesname", ("b", 0), ("h", species("LATIOS")))
            s.label(f"lg{i}_say")
            s.say(m.t("{STR1} roams HOENN again."))
        else:
            if name == "LATI ON ISLAND":
                # The other one of the pair, met only with the Eon Ticket's ferry open
                s.cmd("setflag", ("h", FLAG_ENABLE_SHIP_SOUTHERN_ISLAND))
                s.cmd("bufferspeciesname", ("b", 0), ("h", species("LATIOS")))
                s.when(VAR_ROAMER_POKEMON, 0, f"lg{i}_say")
                s.cmd("bufferspeciesname", ("b", 0), ("h", species("LATIAS")))
                s.label(f"lg{i}_say")
                s.say(m.t("{STR1} waits " + where + "\nagain."))
            else:
                s.say(m.t(name + " waits " + where + "\nagain."))
        s.goto("legends")
    return "LEGENDS", "legends"


@section(
    "Tickets",
    "The event tickets: Eon, Aurora, Mystic and Old Sea Map. The ferry in Lilycove takes you once "
    "you are the Champion; before that, Menu: Warp's ISLANDS does.",
)
def tickets_section(m, back):
    s = m.s
    m.list(
        "tickets",
        [(item_name(it), f"tk{i}") for i, (_, it, _) in enumerate(TICKETS)],
        back,
    )
    for i, (_, item, flags) in enumerate(TICKETS):
        s.label(f"tk{i}")
        for flag in flags:
            s.cmd("setflag", ("h", flag))
        s.cmd("checkitem", ("h", ITEM[item]), ("h", 1))
        s.when(VAR_RESULT, 1, "have")  # one is enough
        s.cmd("setorcopyvar", ("h", VAR_8000), ("h", ITEM[item]))
        s.cmd("setorcopyvar", ("h", VAR_8001), ("h", 1))
        s.cmd("callstd", ("b", 0))  # STD_OBTAIN_ITEM: "Obtained the EON TICKET!"
        s.goto("tickets")
    s.label("have")
    s.say(m.t("You have it: the ferry in\nLILYCOVE will take you."))
    s.goto("tickets")
    return "TICKETS", "tickets"


@section(
    "Unlock",
    "All 8 badges, all 14 Frontier symbols, every town on the FLY map (and the Battle Frontier), "
    "or a complete National Pokédex.",
)
def unlock_section(m, back):
    s = m.s
    m.list(
        "unlock",
        [
            ("BADGES", "ub"),
            ("FRONTIER SYMBOLS", "us"),
            ("FLY SPOTS", "uf"),
            ("POKéDEX", "ud"),
        ],
        back,
    )
    for tag, flags, msg in (
        ("ub", range(0x867, 0x86F), "All 8 badges are yours."),  # FLAG_BADGE01-08_GET
        (
            "us",
            range(0x8C4, 0x8D2),
            "All 14 Frontier symbols are yours.",
        ),  # FLAG_SYS_*_SILVER/GOLD
        (
            "uf",
            [*range(0x86F, 0x87F), 0x8A8],
            "FLY takes you to every town\nand the BATTLE FRONTIER.",
        ),  # FLAG_VISITED_* (the region map's), FLAG_LANDMARK_BATTLE_FRONTIER
    ):
        s.label(tag)
        for f in flags:
            s.cmd("setflag", ("h", f))
        s.say(m.t(msg))
        s.goto("unlock")
    # The game's own switch (its var, flag and magic byte, and the dex in
    # national mode), then tools/golden/dex.s for the seen and caught bits
    s.label("ud")
    s.cmd("special", ("h", SPECIAL["EnableNationalPokedex"]))
    s.cmd("callnative", ("p", "dex+1"))
    s.say(m.t("All 386 POKéMON are seen and caught."))
    s.goto("unlock")
    s.align()
    s.label("dex")
    s.raw((Path(__file__).with_name("dex.bin")).read_bytes())
    return "UNLOCK", "unlock"


def menu_sections(back, slots):
    """Each section's cheat: (name, guide, patches), its blob and its row in
    the main list"""
    out = []
    for slot, name, guide, build in SECTIONS:
        m = Menu(SECTION_BASE + slot * SECTION_SIZE)
        title, entry = build(m, back)
        label = m.t(title)
        patches = m.blob()
        row = slots + 8 * slot
        for k, v in enumerate((m.s.addr(label), m.s.addr(entry))):
            patches += [(row + 4 * k, v & 0xFFFF), (row + 4 * k + 2, v >> 16)]
        out.append((name, guide, patches))
    return out


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

    # The menu (D9, D20-D22): a base that opens it, and a cheat for each of
    # its sections, so only the picked ones are listed
    base, back, slots = menu_base()
    out.append(
        Cheat(
            "Menu",
            "Menu: Open with L+R+B",
            vba(*Mixed(base, None).lines),
            "Hold L and R, then press B. Lists the Menu sections you picked: "
            "pick this and any of them. UP/DOWN move, LEFT/RIGHT a page, A chooses, B goes back.",
            "hold L+R+B",  # field.c's (ROM patches only): shown, never a gate
        )
    )
    for name, guide, patches in menu_sections(back, slots):
        out.append(
            Cheat(
                "Menu",
                f"Menu: {name}",
                vba(*Mixed(patches, None).lines),
                guide + " Needs Menu: Open with L+R+B.",
                "hold L+R+B",
            )
        )

    # Money (D8)
    add(
        "Money",
        "Max money",
        vba(f"[03005D8C]+0490:000F423F^{KEY}"),
        "Keeps your money at 999,999.",
    )
    add(
        "Money",
        "Max coins",
        vba(f"[03005D8C]+0494:270F^{KEY}"),
        "Keeps your Game Corner coins at 9,999. Needs the Coin Case.",
    )
    add(
        "Money",
        "Max Battle Points",
        vba("[03005D90]+0EB8:270F"),  # SaveBlock2's frontier.battlePoints
        "Keeps your Battle Points (BP), the Battle Frontier's prize money, at 9,999.",
    )
    add(
        "Money",
        "Shopping costs 1",
        vba(*Mixed(shop_costs_1(), None).lines),
        "Every item and decoration in a shop costs 1 apiece, as the list shows. Selling pays as "
        "before.",
        "launch",
    )

    # Pokémon (D24-D26)
    add(
        "Pokémon",
        "Shiny",
        vba(*Mixed(personality(0, 1), None).lines),
        "Every new Pokémon is shiny: wild, gift and egg. Works with Nature and Gender.",
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
        "Every new Pokémon (wild, gift, egg) has 31 in every IV.",
        "launch",
    )
    add(
        "Pokémon",
        "Max friendship",
        rom_code([(0x0806DAB0, 0x46C0)]),
        "Friendship goes to 255 at its next change: a level up, a walk or a vitamin.",
        "launch",
    )
    add(
        "Pokémon",
        "Max EVs from battles",
        # MonGainEVs' multiplier (1, or 2 with Pokérus) is 255: each stat the
        # foe yields fills to 255, within the 510 total
        rom_code([(0x0806DB1E, 0x22FF), (0x0806DB24, 0x22FF)]),
        "Each battle maxes the EVs its foe gives (a ZUBAT: Speed): 255 a stat, 510 in all. Stats update "
        "at the next level.",
        "launch",
    )
    for n, nature in enumerate(NATURES):
        add(
            "Pokémon",
            f"Nature: {nature}",
            vba(*Mixed(personality(2, n), None).lines),
            f"Every new Pokémon (wild, gift, egg) is {nature}. Works with Shiny and Gender.",
            "launch",
        )
    for name, v in (("Male", 0xFF), ("Female", 0x00)):
        add(
            "Pokémon",
            f"Gender: {name}",
            vba(*Mixed(personality(4, v), None).lines),
            f"Every new Pokémon that can be either is {name.lower()}. Works with Shiny and Nature.",
            "launch",
        )
    add(  # PokemonUseItemEffects' two EV checks: 100 a stat becomes 255; the 510 total stays
        "Pokémon",
        "Vitamins up to 255",
        rom_code(
            [
                (a + d, v)
                for a in (0x0806C468, 0x0806CA54)
                for d, v in ((0, 0x2BFE), (8, 0x29FF), (14, 0x30FF))
            ]
        ),
        "Vitamins (HP Up, Protein...) raise a stat up to 255 EVs instead of 100, within the 510 total. "
        "EV berries lower them.",
        "launch",
    )
    add(
        "Pokémon",
        "Pokérus when caught",
        rom_code(pokerus()),
        "Pokémon you catch have Pokérus: they earn double EVs.",
        "launch",
    )
    add(
        "Pokémon",
        "Eggs hatch fast",
        vba(
            "[03005D8C]+314C:FE"
        ),  # the Day Care's stepCounter: every step counts as its 255th
        "Eggs hatch after a few dozen steps.",
    )
    add(
        "Pokémon",
        "Everstone always passes nature",
        rom_code(
            [(0x080700FC, 0xE004)]
        ),  # GetParentToInheritNature: skip the coin flip
        "A Day Care parent holding an Everstone always passes its nature to the egg.",
        "launch",
    )
    add(
        "Pokémon",
        "Day Care makes eggs fast",
        rom_code(
            [(0x08070B0C, 0x46C0)]
        ),  # TryProduceOrHatchEgg: roll every step, not every 256th
        "The Day Care rolls for an egg on every step instead of every 256th. Parents must still get along.",
        "launch",
    )

    # Battle (D28-D30)
    add(
        "Battle",
        "One-hit KO",
        # gBattleMons[1] and [3].hp, the foes: above 1, set to 1
        vba(
            *(
                f"{a:08X}{op}"
                for a in (0x02024104, 0x020241B4)
                for op in (">0001", ":0001")
            )
        ),
        "Foes have 1 HP: any hit that lands knocks them out.",
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
        "Your Pokémon take no damage in battle.",
        "launch",
    )
    add(
        "Battle",
        "Infinite PP",
        rom_code([(0x08046B28, 0x07E9)]),
        "Your moves use no PP in battle.",
        "launch",
    )
    add(
        "Battle",
        "No trainer challenges",
        rom_code([(0x080B3D6C, 0xE038)]),
        "Trainers who see you don't challenge you. Talk to one to battle.",
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
            f"Battles give x{mult} EXP, at most 14,335 a Pokémon before the trainer and trade bonuses. Replaces "
            "the Lucky Egg's bonus.",
            "launch",
        )
    add(
        "Battle",
        "EXP shared with the party",
        # gSentPokesToOpponent, a byte a foe: every party slot fought each one
        vba("020243FE:FFFF"),
        "Every Pokémon in your party gets a share of each battle's EXP and the foe's EVs, as if all had fought.",
    )
    add(
        "Battle",
        "Every ball catches",
        rom_code([(0x08056566, 0x46C0)]),
        "Every ball you throw at a wild Pokémon catches it.",
        "launch",
    )
    add(
        "Battle",
        "Pokémon always obey",
        rom_code(words([(0x08045CBC, 0x47702000)])),  # IsMonDisobedient returns 0
        "Traded and event Pokémon obey you whatever your badges.",
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
            f"Every wild Pokémon is {title(name)}, at the area's usual levels. Roaming Pokémon stay as they are.",
            "launch",
        )
    for level in range(5, 101, 5):
        add(
            "Wild",
            f"Level {level}",
            rom_code(words([(0x080B4C74, 0x47702000 | level)])),
            f"Every wild Pokémon is level {level}. Roaming and outbreak Pokémon keep theirs.",
            "launch",
        )
    add(
        "Wild",
        "No wild encounters",
        # sWildEncounterImmunitySteps: the 4 steps after a battle with no
        # encounter, never counted past the first
        vba("020375D4:00"),
        "No wild Pokémon in grass, caves or water. Fishing, Rock Smash and Sweet Scent still find them.",
    )
    add(
        "Wild",
        "Encounter every step",
        rom_code(words([(0x080B5170, 0x47702001)])),
        "A wild Pokémon on almost every step in grass, caves and water.",
        "launch",
    )

    # Overworld (D35-D39, D42, D47, D52-D54, D60)
    add(
        "Overworld",
        "Instant text",
        rom_code(instant_text()),
        "Messages appear at once instead of a letter at a time, at every text speed.",
        "launch",
    )
    add(
        "Overworld",
        "Run and bike anywhere",
        rom_code(
            [(0x0811A1EC, 0x46C0)]  # IsRunningDisallowed: the map's running rule
            + words([(0x08085474, 0x47702001)])  # Overworld_IsBikingAllowed returns 1
        ),
        "Run and ride your bike indoors and in caves. Fortree's bridges still need walking.",
        "launch",
    )
    add(
        "Overworld",
        "Mirage Island always there",
        rom_code(words([(0x0813793C, 0x47702001)])),  # IsMirageIslandPresent returns 1
        "Mirage Island shows up off Route 130 every day, whatever your party.",
        "launch",
    )
    add(
        "Overworld",
        "Fly to any town",
        rom_code(
            [(0x08123E50, 0x2102)]
        ),  # GetMapsecType: a town not visited is CITY_CANFLY too
        "Fly to any town, even ones you haven't visited.",
        "launch",
    )
    add(
        "Overworld",
        "Free Day Care",
        # PrepareDaycareCostStringForSelectedMon: cost = 0, not 100 + 100 a level
        rom_code([(0x0806FE9E, 0x2400), (0x0806FEA0, 0x46C0), (0x0806FEA2, 0x46C0)]),
        "Taking a Pokémon back from the Day Care costs nothing.",
        "launch",
    )
    add(
        "Overworld",
        "Walk through walls (hold R)",
        vba(*Mixed(walls(), None).lines),
        "Hold R to walk through walls, people and water. Let go only on normal ground; if stuck, hold R and walk back.",
        "hold R",  # the routine's own (ROM patches only): shown, never a gate
    )
    add(
        "Overworld",
        "Field moves without badges",
        rom_code([(0x081B54EA, 0xE01B)]),
        "Use Cut, Surf, Fly and the other field moves from the POKéMON menu without their badges.",
        "launch",
    )
    add(
        "Overworld",
        "Gym leader rematches",
        # trainerRematches[REMATCH_ROXANNE..REMATCH_JUAN]: each leader ready
        vba(*(f"[03005D8C]+{0x9CA + i:04X}:01" for i in range(65, 73))),
        "Talk to a gym leader you've beaten for a rematch: a double battle, their team stronger each time.",
    )
    add(
        "Overworld",
        "Any Pokémon in the Frontier",
        # AppendIfValid and GetBattleEntryEligibility skip gFrontierBannedSpecies
        rom_code([(0x081A3F32, 0xE00F), (0x081B8630, 0xE00B)]),
        "Legendaries and any other Pokémon may enter every Battle Frontier facility.",
        "launch",
    )

    # Minigames (D31, D43)
    add(
        "Minigames",
        "Endless Safari Zone",
        cb(CB.write8(0x0203A04C, 30), CB.write16(0x0203A04E, 500)),
        "In the Safari Zone you keep 30 balls and 500 steps.",
    )
    add(
        "Minigames",
        "Safari Pokémon never flee",
        rom_code([(0x0813334E, 0xE011)]),  # Cmd_if_random_safari_flee: never jump
        "Pokémon in the Safari Zone never run away.",
        "launch",
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
        "Run it once: a gold Pokéblock with every flavor at max goes in your case's last slot.",
    )
    add(
        "Minigames",
        "Quick contests",
        cb(CB.op("EQ", 0x02002E30, 0), CB.write8(0x02002E31, 4)),
        "Contests skip straight to the results.",
    )
    add(
        "Minigames",
        "Max contest score",
        cb(CB.op("EQ", 0x02002F62, 0x082D), CB.write8(0x02002EF4, 0xFF)),
        "Your contest appeal score is always the maximum.",
    )
    add(
        "Minigames",
        "Full Soot Sack",
        ar(AR.write_ptr(2, SB1, 9999, var_offset(0x4048))),
        "Your Soot Sack holds 9,999 ash, for the Glass Workshop on Route 113.",
    )
    return out


def personality(setting, value):
    """The personality routine (tools/golden/pid.s) and its hook in CreateBoxMon,
    the same in each Pokémon cheat that uses it, and this cheat's own switch"""
    stub = Path(__file__).with_name("pid.bin").read_bytes()
    off = 0x080021A4 - (0x08067C74 + 4)  # bl pid, then 8 nops, where the OT ID was read
    hook = (0xF000 | (off >> 12) & 0x7FF, 0xF800 | (off >> 1) & 0x7FF) + (0x46C0,) * 8
    return (
        [
            (0x080021A4 + i, int.from_bytes(stub[i : i + 2], "little"))
            for i in range(0, len(stub), 2)
        ]
        + [(0x08067C74 + 2 * i, h) for i, h in enumerate(hook)]
        + [(0x08F1F000 + setting, value)]
    )


def shop_costs_1():
    """The shop's prices, 1 each: in its list (BuyMenuPrintPriceInList), as an
    item is chosen (Task_BuyMenu) and as its quantity changes
    (Task_BuyHowManyDialogueHandleInput). Each bl GetItemPrice returns 1,
    and the bl IsPokeNewsActive after it (Slateport's sale, which halves
    prices) 0, so a sale never makes it 0; a decoration's price load is 1.
    Selling reads the price elsewhere and is left alone."""
    price, no_sale = (0x2001, 0x46C0), (0x2000, 0x46C0)  # movs r0, #1 / #0; nop
    out = []
    for bl_price in (0x080E0136, 0x080E0B58, 0x080E0DB4):
        out += [(bl_price, price[0]), (bl_price + 2, price[1])]
        out += [(bl_price + 8, no_sale[0]), (bl_price + 10, no_sale[1])]
    out += [
        (0x080E016C, 0x2101),
        (0x080E0B8E, 0x2001),
    ]  # ldrh r1/r0, [.., #20]: movs r1/r0, #1
    return out


def instant_text():
    """Every text speed prints a message up to its next pause in one frame.
    sTextSpeedFrameDelays all 1 (the printer's delay is then 0, as at FAST);
    RenderFont keeps going on RENDER_PRINT as on RENDER_REPEAT; RunTextPrinters
    copies the window to VRAM after every call, not only after RENDER_PRINT.
    (A delay of 0 instead draws the text before the box clears it.)"""
    off = 0x08003658 - (0x080047A4 + 4)  # bl CopyWindowToVram
    return [
        (0x0860F094, 0x0101),
        (0x08004836, 0x07C1),  # RenderFont: lsls r1, r0, #31; beq: loop on 0 and 2
        (0x080047A0, 0x7828),  # ldrb r0, [r5]: the window
        (0x080047A2, 0x2102),  # movs r1, #COPYWIN_GFX
        (0x080047A4, 0xF000 | (off >> 12) & 0x7FF),
        (0x080047A6, 0xF800 | (off >> 1) & 0x7FF),
        (0x080047A8, 0x2C01),  # cmp r4, #RENDER_FINISH
        (0x080047AA, 0xD01B),  # beq: the printer is done
        (0x080047AC, 0xE004),  # b: RENDER_UPDATE runs the callback
        (0x080047AE, 0x46C0),
    ]


POKERUS = 0x080478B8  # tools/golden/pokerus.c, over Unused_ApplyRandomDmgMultiplier (0x3C bytes)
POKERUS_CALL = 0x080566CE  # Cmd_givecaughtmon's bl GiveMonToPlayer


def pokerus():
    """A caught Pokémon has Pokérus (tools/golden/pokerus.c): the routine,
    and the catch's call to GiveMonToPlayer sent through it"""
    code = Path(__file__).with_name("pokerus.bin").read_bytes()
    assert len(code) <= 0x3C, "pokerus.bin runs past Unused_ApplyRandomDmgMultiplier"
    code += b"\0" * (len(code) & 1)
    off = POKERUS - (POKERUS_CALL + 4)
    assert -(1 << 22) <= off < 1 << 22, "out of a bl's reach"
    return [
        (POKERUS + i, int.from_bytes(code[i : i + 2], "little"))
        for i in range(0, len(code), 2)
    ] + [
        (POKERUS_CALL, 0xF000 | (off >> 12) & 0x7FF),
        (POKERUS_CALL + 2, 0xF800 | (off >> 1) & 0x7FF),
    ]


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
    cs = cheats()
    lines = [
        f"cheats = {len(cs)}",
        "",
    ]
    fmt = {CB: "CodeBreaker", AR: "PARv3 raw", VBA: "Torii"}
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
