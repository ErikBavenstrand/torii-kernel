#!/usr/bin/env python3
"""Compares Torii's decoding of GBA cheats with mGBA's, cheat by cheat, and
explains each difference by a rule in tests/cheat_expected.toml: an "allow"
rule is a difference Torii means (it follows the real device, or reads more
than mGBA), an "xfail" rule a known bug waiting for its fix. What no rule
explains is a mismatch: a finding.

Used by tests/cheat_corpus.py (libretro's database, and tests/cheats/
corpus_sample.cht in test-host) and tests/cheat_engine.py (each case's codes).

The rows are tools/cheat_corpus.c's (Torii: file, cheat, status, lowering,
format, ops) and tools/cheat_oracle.c's (mGBA: file, cheat, SKIP or REJn,
ops), the ops normalized alike:

    ASSIGN/2/02034096/300/1/0/0      a write: op/width/address/value/count/
                                     address step/value step
    EQ/2/04000130/2BB/1/0            a conditional: .../covered/else
    BUTTON/1/0                       a button or never-true test
    | 08001234/20A5/2                after the bar: ROM patches

Each cheat's code is also read here line by line (decrypted with the file's
seeds and CodeBreaker key, as both decoders carry them) to name its code
types ("PAR if-ULT/2/block", "CB 5", "GSA 8/F"), which the rules and the
report's grouping use.
"""

import re
import sys
import tomllib
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from cheat_encode import GSA_SEEDS, PAR_SEEDS, TABLES, CBKey, gs_reseed, tea_decrypt

EXPECTED = ROOT / "tests" / "cheat_expected.toml"
PAD = "ADD/1/00000000/0/1/0/0"  # Action Replay v3's "80000000 00000000"
COND_OPS = {"EQ", "NE", "LT", "GT", "ULT", "UGT", "IFAND", "LAND", "NAND"}


# --- The code's lines ------------------------------------------------------


def split_code(code):
    """A cheat's code as libretro stores it, in lines: [(kind, op1, op2,
    text)], kind "CB", "GS" or "VBA"; None if it doesn't split (or has digit
    stand-ins)"""
    toks = [t for t in re.split(r"[+\s]+", code.strip()) if t]
    out, i = [], 0
    while i < len(toks):
        t = toks[i]
        if ":" in t:
            a, _, v = t.partition(":")
            if not re.fullmatch(r"[0-9A-Fa-f]{8}", a) or not re.fullmatch(
                r"[0-9A-Fa-f]{2,8}", v
            ):
                return None
            out.append(("VBA", int(a, 16), int(v, 16), t))
            i += 1
            continue
        if not re.fullmatch(r"[0-9A-Fa-f]+", t):
            return None
        if len(t) == 12:
            out.append(("CB", int(t[:8], 16), int(t[8:], 16), t))
        elif len(t) == 16:
            out.append(("GS", int(t[:8], 16), int(t[8:], 16), t))
        elif (
            len(t) == 8
            and i + 1 < len(toks)
            and re.fullmatch(r"[0-9A-Fa-f]{4}|[0-9A-Fa-f]{8}", toks[i + 1])
        ):
            n = toks[i + 1]
            out.append(
                ("CB" if len(n) == 4 else "GS", int(t, 16), int(n, 16), t + " " + n)
            )
            i += 1
        else:
            return None
        i += 1
    return out


PAR_SPECIAL = {
    0x00: "end",
    0x08: "slowdown",
    0x10: "button/1",
    0x12: "button/2",
    0x14: "button/4",
    0x18: "patch1",
    0x1A: "patch2",
    0x1C: "patch3",
    0x1E: "patch4",
    0x40: "endif",
    0x60: "else",
    0x80: "fill/1",
    0x82: "fill/2",
    0x84: "fill/4",
}
PAR_COND = {1: "EQ", 2: "NE", 3: "LT", 4: "GT", 5: "ULT", 6: "UGT", 7: "AND"}
PAR_ACTION = {0: "next", 1: "next2", 2: "block", 3: "disable"}


def label_gsa(op1, op2):
    t = op1 >> 28
    if op1 == 0xDEADFACE:
        return "GSA deadface"
    if op2 == 0x001DC0DE:
        return "GSA id"
    if t == 8:
        return f"GSA 8/{op1 >> 20 & 0xF:X}"
    return f"GSA {t:X}"


def label_par(op1, op2):
    if op2 == 0x001DC0DE:
        return "PAR id"
    if op1 == 0xDEADFACE:
        return "PAR deadface"
    if op1 == 0:
        return "PAR " + PAR_SPECIAL.get(op2 >> 24 & 0xFE, f"00/{op2 >> 24:02X}")
    if op1 == 0x80000000 and op2 == 0:
        return "PAR pad"
    cond = op1 >> 27 & 7
    width = 1 << (op1 >> 25 & 3)
    if cond:
        return f"PAR if-{PAR_COND[cond]}/{width}/{PAR_ACTION[op1 >> 30]}"
    base = op1 >> 30
    if base == 3:
        return f"PAR {op1 >> 24:02X}"
    return f"PAR {['write', 'ptr', 'add'][base]}/{width}"


def label_cb(op1, op2):
    t = op1 >> 28
    if t == 0xD:
        return "CB D0000020" if op1 & 0x0FFFFFFF == 0x20 else "CB D-other"
    return f"CB {t:X}"


@dataclass
class FileState:
    """What a file's cheats share as both decoders read them"""

    gs_seeds: dict = field(default_factory=lambda: {"GSA": GSA_SEEDS, "PAR": PAR_SEEDS})
    cb: object = None
    cb_line: tuple = None
    second_key: bool = False  # a different CodeBreaker key line came after the first


def labels(fmt, lines, state):
    """Each line's code type, decrypted as fmt says ("GSAv1", "PARv3-raw",
    "CodeBreaker", "VBA" or "-"), carrying the file's state on"""
    out = []
    cont = 0  # continuation lines (a fill's second line, a super code's data)
    for kind, raw1, raw2, _ in lines or []:
        op1, op2 = raw1, raw2
        if kind == "VBA":
            out.append("VBA hi" if op1 >= 0x10000000 else "VBA")
            continue
        if kind == "CB":
            if op1 >> 28 == 9 and (state.cb is None or (op1, op2) != state.cb_line):
                if state.cb is not None:
                    state.second_key = True
                try:
                    state.cb = CBKey(op1, op2)
                    state.cb_line = (op1, op2)
                except ValueError:
                    state.cb = None
                out.append("CB key")
                continue
            if state.cb is not None and (op1, op2) == state.cb_line:
                out.append("CB key")
                continue
            if state.cb is not None:
                op1, op2 = state.cb.decrypt(op1, op2)
            if cont:
                cont -= 1
                out.append("CB data")
                continue
            lab = label_cb(op1, op2)
            if lab == "CB 4":
                cont = 1
            elif lab == "CB 5":
                cont = (2 * op2 + 5) // 6
            out.append(lab)
            continue
        family = "PAR" if fmt.startswith("PAR") else "GSA"
        if fmt.endswith("-raw") or fmt.startswith("-"):
            o1, o2 = op1, op2
        else:
            o1, o2 = tea_decrypt(op1, op2, state.gs_seeds[family])
        if cont:
            cont -= 1
            out.append(f"{family} data")
            continue
        lab = (label_par if family == "PAR" else label_gsa)(o1, o2)
        if lab.endswith("deadface"):
            t = ("par3_t1", "par3_t2") if family == "PAR" else ("gsa1_t1", "gsa1_t2")
            state.gs_seeds[family] = gs_reseed(o2, TABLES[t[0]], TABLES[t[1]])
        if family == "PAR" and re.match(r"PAR (button|fill|patch)", lab):
            cont = 1
        if lab == "GSA 3":
            cont = ((o1 & 0xFFFF) + 1) // 2
        out.append(lab)
    return out


# --- Ops -------------------------------------------------------------------


def parse_ops(text):
    """(ops, patches): each op a list of its fields, the name first"""
    text = (text or "").strip()
    ops_text, _, patches = text.partition("|")
    ops = [t.split("/") for t in ops_text.split()]
    return ops, patches.split()


def is_cond(op):
    return op[0] in COND_OPS or op[0] in ("BUTTON", "NEVER")


def cover(op):
    """(covered, else) of a conditional"""
    return (
        (int(op[1]), int(op[2]))
        if op[0] in ("BUTTON", "NEVER")
        else (int(op[4]), int(op[5]))
    )


def with_cover(op, rep, neg):
    if op[0] in ("BUTTON", "NEVER"):
        return [op[0], str(rep), str(neg)]
    return [*op[:4], str(rep), str(neg)]


def drop(ops, keep):
    """ops without those keep() refuses, every conditional's cover recounted"""
    kept = [keep(op) for op in ops]
    out = []
    for i, op in enumerate(ops):
        if not kept[i]:
            continue
        if is_cond(op):
            rep, neg = cover(op)
            rep2 = sum(kept[i + 1 : i + 1 + rep])
            neg2 = sum(kept[i + 1 + rep : i + 1 + rep + neg])
            out.append(with_cover(op, rep2, neg2))
        else:
            out.append(op)
    return out


def text(ops):
    return " ".join("/".join(op) for op in ops)


def strip_covers(ops):
    return [with_cover(op, 0, 0) if is_cond(op) else op for op in ops]


# --- Rows ------------------------------------------------------------------


def read_rows(path, key_cols=2):
    rows = {}
    for line in Path(path).read_text(encoding="utf-8", errors="replace").splitlines():
        cols = line.split("\t")
        if len(cols) > key_cols:
            rows[tuple(cols[:key_cols])] = cols[key_cols:]
    return rows


def read_codes(path):
    """{cheat number: code} of a libretro .cht file, as the drivers read it"""
    out = {}
    for line in Path(path).read_text(encoding="utf-8", errors="replace").splitlines():
        m = re.match(r"cheat(\d+)_code\s*=\s*\"(.*)", line)
        if m:
            body = m[2]
            out[m[1]] = body[: body.rfind('"')] if '"' in body else body
    return out


@dataclass
class Cheat:
    file: str
    n: str
    code: str
    t_status: str
    t_lower: str
    t_format: str
    t_ops: list
    t_patches: list
    m_status: str  # SKIP, REJn, MISSING
    m_ops: list
    m_patches: list
    labels: list
    second_key: bool

    @property
    def m_rejected(self):
        return int(self.m_status[3:]) if self.m_status.startswith("REJ") else 0

    @property
    def m_clean(self):
        """mGBA read every line, but for master lines Torii drops (a
        GameShark v1 game ID, the 001DC0DE marker, a second hook)"""
        if not self.m_status.startswith("REJ"):
            return False
        master = sum(
            1 for lab in self.labels if lab in ("GSA id", "GSA F", "CB 1", "PAR C4")
        )
        return self.m_rejected <= master

    @property
    def family(self):
        f = self.t_format.replace("-alone", "")
        if f in ("-", ""):
            kinds = {lab.split()[0] for lab in self.labels}
            return "/".join(sorted(kinds)) or "-"
        return f

    def where(self):
        return f"{self.file} #{self.n}"


def cheats(cht_files, torii_tsv, mgba_tsv):
    """Every cheat of the files, both decodings joined"""
    t_rows, m_rows = read_rows(torii_tsv), read_rows(mgba_tsv)
    for path in cht_files:
        name = Path(path).name
        state = FileState()
        # A file's dominant GameShark format, for a cheat Torii didn't read
        fmts = Counter(
            r[2].replace("-alone", "")
            for k, r in t_rows.items()
            if k[0] == name and len(r) > 2 and r[2] not in ("-", "CodeBreaker", "VBA")
        )
        gs_fmt = fmts.most_common(1)[0][0] if fmts else "-"
        for n, code in read_codes(path).items():
            t = t_rows.get((name, n), ["MISSING"])
            m = m_rows.get((name, n), ["MISSING"])
            lines = split_code(code)
            fmt = t[2] if len(t) > 2 else "-"
            fmt = fmt.replace("-alone", "")
            if fmt in ("-", "CodeBreaker", "VBA"):
                fmt = gs_fmt
            before = state.second_key
            alone = len(t) > 2 and t[2].endswith("-alone")
            labs = (
                labels(fmt, lines, state)
                if not alone
                else labels(fmt, lines, FileState())
            )
            t_ops, t_patches = parse_ops(t[3] if len(t) > 3 else "")
            m_ops, m_patches = parse_ops(m[1] if len(m) > 1 else "")
            yield Cheat(
                name,
                n,
                code,
                t[0],
                t[1] if len(t) > 1 else "-",
                t[2] if len(t) > 2 else "-",
                t_ops,
                t_patches,
                m[0],
                m_ops,
                m_patches,
                labs,
                before or state.second_key,
            )


# --- Rules -----------------------------------------------------------------


def agree(c):
    """Whether the two decodings agree outright"""
    if c.t_status == "OK":
        return c.m_clean and c.t_ops == c.m_ops and c.t_patches == c.m_patches
    if c.t_status == "MASTER_ONLY":
        return c.m_clean and not c.m_ops and not c.m_patches
    if c.t_status in ("PLACEHOLDER", "UNREADABLE", "NOQUOTE"):
        # Torii refuses a cheat with a line mGBA rejects or skips
        return c.m_status == "SKIP" or c.m_rejected > 0
    return False


def has(c, pattern):
    return any(re.match(pattern, lab) for lab in c.labels)


def ops_equal_but_covers(c, m_ops=None):
    m = c.m_ops if m_ops is None else m_ops
    return strip_covers(c.t_ops) == strip_covers(m) and c.t_patches == c.m_patches


def rule_alone(c):
    return c.t_format.endswith("-alone")


def rule_master_lines(c):
    return c.t_status == "MASTER_ONLY" and c.m_rejected > 0 and not c.m_ops


def rule_cb_super(c):
    return c.t_status == "OK" and c.m_rejected > 0 and has(c, r"CB 5$")


def rule_gs_list(c):
    return c.t_status == "OK" and has(c, r"GSA 3$")


def rule_vba_hi(c):
    return has(c, r"VBA hi")


def rule_par_block_open(c):
    if c.t_status != "OK" or not c.m_clean or not ops_equal_but_covers(c):
        return False
    blocks = sum(1 for lab in c.labels if re.match(r"PAR if-.*/block", lab))
    ends = sum(1 for lab in c.labels if lab in ("PAR endif",))
    return blocks > ends


def rule_cond_multiline(c):
    return (
        c.t_status == "OK"
        and c.m_clean
        and ops_equal_but_covers(c)
        and has(c, r"(GSA 8/|PAR button|GSA 3$|CB 5$)")
    )


def rule_par_padding(c):
    if c.t_status != "OK" or not has(c, r"PAR pad"):
        return False
    m = drop(c.m_ops, lambda op: "/".join(op) != PAD)
    return c.m_clean and c.t_ops == m and c.t_patches == c.m_patches


def rule_par_padding_count(c):
    if c.t_status != "OK" or not has(c, r"PAR pad"):
        return False
    m = drop(c.m_ops, lambda op: "/".join(op) != PAD)
    return c.m_clean and ops_equal_but_covers(c, m)


def rule_par_io_width(c):
    if c.t_status != "OK" or not c.m_clean or not has(c, r"PAR C[67]"):
        return False
    if len(c.t_ops) != len(c.m_ops) or c.t_patches != c.m_patches:
        return False
    for t, m in zip(c.t_ops, c.m_ops, strict=True):
        if t != m and not (
            t[0] == m[0] == "ASSIGN"
            and t[2] == m[2]
            and t[2].startswith("04")
            and int(t[1]) == 2 * int(m[1])
        ):
            return False
    return True


def rule_slowdown(c):
    if c.t_status != "OK" or not has(c, r"(PAR slowdown|GSA 8/F)"):
        return False
    t = drop(c.t_ops, lambda op: op[0] != "SLOW")
    n = sum(1 for lab in c.labels if re.match(r"(PAR slowdown|GSA 8/F)", lab))
    return c.m_rejected == n and t == c.m_ops and c.t_patches == c.m_patches


def rule_mgba_nand_else(c):
    # mGBA's D0000020 sets repeat but never negativeRepeat, which keeps what
    # the cheat list's memory held: an ELSE count from nowhere
    if (
        c.t_status != "OK"
        or not c.m_clean
        or len(c.t_ops) != len(c.m_ops)
        or c.t_patches != c.m_patches
    ):
        return False
    for t, m in zip(c.t_ops, c.m_ops, strict=True):
        if t != m and not (t[0] == m[0] == "NAND" and t[:5] == m[:5]):
            return False
    return True


def rule_gs_slowdown_unreadable(c):
    return c.t_status == "UNREADABLE" and has(c, r"GSA 8/F")


def rule_par_empty_then_else(c):
    # A block whose THEN has no ops (or only ROM patches) before its ELSE:
    # mGBA's _parEndBlock then takes the ELSE block for the THEN
    if (
        c.t_status != "OK"
        or not c.m_clean
        or not ops_equal_but_covers(c)
        or not has(c, r"PAR else")
    ):
        return False
    for t, m in zip(c.t_ops, c.m_ops, strict=True):
        if t != m and not (
            is_cond(t) and cover(t)[0] == 0 and cover(m) == (cover(t)[1], 0)
        ):
            return False
    return True


def rule_cond_rom_patch(c):
    # Torii counts a ROM patch where it lies; mGBA keeps patches in a list
    # of their own, so its conditional covers the next code that isn't one
    return (
        c.t_status == "OK"
        and c.m_clean
        and ops_equal_but_covers(c)
        and has(c, r"(GSA 6|PAR patch)")
        and any(is_cond(op) for op in c.t_ops)
    )


def rule_par_disable(c):
    return has(c, r"PAR if-.*/disable") and c.t_status in ("UNREADABLE", "OK")


def rule_cb_d_types(c):
    return has(c, r"CB D-other")


def rule_cb_second_key(c):
    return c.second_key


RULES = {
    "alone": rule_alone,
    "master-lines": rule_master_lines,
    "cb-super": rule_cb_super,
    "gs-list": rule_gs_list,
    "vba-high-address": rule_vba_hi,
    "par-block-open-at-end": rule_par_block_open,
    "cond-covers-multiline": rule_cond_multiline,
    "par-padding": rule_par_padding,
    "par-padding-count": rule_par_padding_count,
    "par-io-width": rule_par_io_width,
    "slowdown": rule_slowdown,
    "mgba-nand-else": rule_mgba_nand_else,
    "gs-slowdown-unreadable": rule_gs_slowdown_unreadable,
    "par-empty-then-else": rule_par_empty_then_else,
    "cond-covers-rom-patch": rule_cond_rom_patch,
    "par-disable": rule_par_disable,
    "cb-d-types": rule_cb_d_types,
    "cb-second-key": rule_cb_second_key,
}


def load_expected(path=EXPECTED):
    """{"decode": [(id, kind, reason)], "bugs": {id: reason}, "engine":
    {case: (kind, reason or bug id)}}: the known bugs are the [[bug]]
    entries and the decode rules of kind xfail"""
    data = tomllib.loads(Path(path).read_text(encoding="utf-8"))
    decode = []
    for r in data.get("decode", []):
        if r["id"] not in RULES:
            raise SystemExit(
                f"{path}: decode rule {r['id']} has no predicate in tools/cheat_diff.py"
            )
        decode.append((r["id"], r["kind"], r["reason"]))
    bugs = {r["id"]: r["reason"] for r in data.get("bug", [])}
    bugs.update({rid: reason for rid, kind, reason in decode if kind == "xfail"})
    engine = {}
    for r in data.get("engine", []):
        if "bug" in r:
            if r["bug"] not in bugs:
                raise SystemExit(
                    f"{path}: engine case {r['case']} names an unknown bug {r['bug']}"
                )
            engine[r["case"]] = ("xfail", r["bug"])
        else:
            engine[r["case"]] = ("allow", r["reason"])
    return {"decode": decode, "bugs": bugs, "engine": engine}


def check_expectations(path, torii_tsv, bugs):
    """Each line of an expect.tsv (file, cheat, regex, bug or -) against
    Torii's decoding: [(PASS|FAIL|XFAIL|XPASS, line)]"""
    rows = read_rows(torii_tsv)
    out = []
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        name, n, pattern, bug = line.split("\t")
        t = rows.get((name, n), ["MISSING"])
        got = f"{t[0]} {t[3].strip() if len(t) > 3 else ''}"
        ok = re.match(pattern, got) is not None
        if bug != "-" and bug not in bugs:
            raise SystemExit(f"{path}: {name} #{n} names an unknown bug {bug}")
        tag = ("XPASS" if ok else "XFAIL") if bug != "-" else ("PASS" if ok else "FAIL")
        why = f" ({bug}: {bugs[bug]})" if bug != "-" else ""
        out.append((tag, f"{name} #{n}: want /{pattern}/, Torii has {got}{why}"))
    return out


def verdict(c, rules):
    """("same" | "allow" | "xfail" | "mismatch", rule id or None)"""
    if agree(c):
        return "same", None
    for rid, kind, _ in rules:
        if RULES[rid](c):
            return kind, rid
    return "mismatch", None


def group(c):
    """A mismatch's code type, for the report: the format, and the first op
    the two decodings differ at with its code types"""
    kinds = sorted({lab for lab in c.labels if not lab.endswith(("data", "key", "id"))})
    if c.t_status != "OK" or not c.m_status.startswith("REJ"):
        what = f"Torii {c.t_status}, mGBA {c.m_status if c.m_status != 'REJ0' else 'reads it'}"
    else:
        what = "ops differ"
        for t, m in zip(c.t_ops + [["-"]] * 99, c.m_ops + [["-"]] * 99, strict=False):
            if t != m:
                what = f"ops differ at {t[0]} / {m[0]}"
                break
    return f"{c.family}: {what} [{', '.join(kinds[:4])}]"


def describe(c):
    return (
        f"{c.where()}: {c.code[:120]}\n"
        f"      torii: {c.t_status} {c.t_format} {text(c.t_ops)} | {' '.join(c.t_patches)}\n"
        f"      mgba:  {c.m_status} {text(c.m_ops)} | {' '.join(c.m_patches)}\n"
        f"      types: {' '.join(c.labels)}"
    )


def report(all_cheats, rules, title, examples=3):
    """(report text, unexplained count)"""
    reasons = {rid: (kind, reason) for rid, kind, reason in rules}
    by_format = Counter()
    by_status = Counter()
    one_sided = Counter()
    verdicts = Counter()
    matched = defaultdict(list)
    groups = defaultdict(list)
    for c in all_cheats:
        by_status[c.t_status] += 1
        if c.t_status == "OK":
            by_format[c.t_format.replace("-alone", "")] += 1
        t_reads = c.t_status in ("OK", "MASTER_ONLY")
        m_reads = c.m_clean
        if t_reads != m_reads:
            one_sided[
                "Torii reads, mGBA doesn't" if t_reads else "mGBA reads, Torii doesn't"
            ] += 1
        v, rid = verdict(c, rules)
        verdicts[v] += 1
        if rid:
            matched[rid].append(c)
        if v == "mismatch":
            groups[group(c)].append(c)
    total = sum(by_status.values())
    out = [title, "", f"{total} cheats"]
    out += [f"  Torii {s:12} {n:6}" for s, n in by_status.most_common()]
    out += ["", "Read by Torii, by format:"]
    out += [f"  {f:14} {n:6}" for f, n in by_format.most_common()]
    out += ["", "Read by one decoder only:"]
    out += [f"  {k:28} {n:6}" for k, n in one_sided.most_common()] or ["  none"]
    out += ["", "Torii against mGBA:"]
    out += [f"  {k:10} {verdicts[k]:6}" for k in ("same", "allow", "xfail", "mismatch")]
    for kind, heading in (
        ("allow", "Intentional differences (allow)"),
        ("xfail", "Known bugs (xfail)"),
    ):
        out += ["", heading + ":"]
        ids = [rid for rid, (k, _) in reasons.items() if k == kind]
        for rid in ids:
            got = matched.get(rid, [])
            out.append(f"  {rid} ({len(got)}): {reasons[rid][1]}")
            for c in got[:examples]:
                out.append("    " + describe(c))
    out += ["", f"Unexplained mismatches ({verdicts['mismatch']}), by code type:"]
    if not groups:
        out.append("  none")
    for g, cs in sorted(groups.items(), key=lambda kv: -len(kv[1])):
        out.append(f"  {g}: {len(cs)}")
        for c in cs[:examples]:
            out.append("    " + describe(c))
    return "\n".join(out) + "\n", verdicts["mismatch"]
