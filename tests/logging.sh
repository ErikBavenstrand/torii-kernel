#!/usr/bin/env bash
# SD logging, the crash dump and Diagnostics on the emulated cart
# (source/tlog.c, source/cartlog.c, source/diag.c). One part per run, so
# tests/all.sh runs them side by side:
#   launch  logging off touches nothing; at Info a launch is logged and the
#           next boot says how the session ended; a launch cut off is caught
#   debug   the in-game menu's DEBUG entry says it saved, writes only cart
#           RAM page 0xF0, and RESET from the menu logs it
#   crash   with logging off, Diagnostics (started by keys held at power-on)
#           logs its report, leaves the save chip alone, and its crash test's
#           dump reaches the log, named by tools/logs.py
#   rotate  a full log file hands over to the next one
#   junk    a log file with junk after its text (v0.2.0 on the real cart)
#           is left alone, and logging goes on in the next
#   full    a full card stops the logging, not the kernel
#   flood   a scrolled list's frame-cycles go to the card as summaries
# Every part checks the card with fsck.fat. Runs inside the torii-emu container.
set -euo pipefail

part=$1
cart=dist/cart-logging-$part
rm -rf "$cart"
mkdir -p "$cart"

fail() {
  echo "FAIL: logging $part: $*" >&2
  [ ! -f "$cart/kernel.log" ] || tail -15 "$cart/kernel.log" >&2
  exit 1
}

new_card() { # size in KB, FAT type
  mkfs.fat -C -F "$2" -n LOGGING "$cart/sd.img" "$1" >/dev/null
  mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/Hello.gba
}

# Runs the kernel from power-on, which clears cart RAM past the save chip;
# its log lines go to kernel.log. A crash report fails the test.
boot() {
  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin "$@" >>"$cart/out.log" 2>"$cart/run.log" || true
  cat "$cart/run.log" >>"$cart/kernel.log"
  ! grep -q '^crash at frame' "$cart/run.log" || fail "the emulator caught a crash"
}
# Runs it as after a soft reset (RESET, or a crash's restart), with the cart
# still powered: cart RAM keeps its contents.
soft_boot() { OMEGA_SOFT_RESET=1 boot "$@"; }
# How many lines of log file N hold TEXT.
count() {
  log "${2:-0}" >"$cart/log.txt"
  grep -cF -- "$1" "$cart/log.txt" || true
}

# Settings slots (source/ez_define.h) in the kernel NOR, as SLOT=VALUE.
settings() {
  local slot value
  for slot in "$@"; do
    value=${slot#*=}
    printf %b "\\x$(printf %02x $((value & 0xFF)))\\x$(printf %02x $((value >> 8)))" |
      dd of="$cart/kernel-nor.bin" bs=1 seek=$((0x7B0000 + 2 * ${slot%=*})) conv=notrunc status=none
  done
}

# keys FRAME KEY... -> mgba-shot press/release pairs, one key every 8 frames.
keys() {
  local frame=$1
  shift
  for key in "$@"; do
    printf -- '-k %d:%s -k %d:- ' "$frame" "$key" $((frame + 4))
    frame=$((frame + 8))
  done
}

# The text of log file N (its zero padding dropped).
log() { mtype -i "$cart/sd.img" "::/torii/logs/torii-$1.log" | tr -d '\0'; }
# (Through a file: grep -q stops reading early, which pipefail counts as a failure.)
has() {
  log "${2:-0}" >"$cart/log.txt"
  grep -qF -- "$1" "$cart/log.txt" || fail "no '$1' in torii-${2:-0}.log"
}
# Every line of log file N is a header or "SECONDS.CC LEVEL text": a byte
# lost or shifted on the way to the card breaks one.
lines_ok() {
  log "${1:-0}" >"$cart/log.txt"
  ! grep -naEv '^(torii log [0-9]{8} +| *[0-9]+\.[0-9]{2} [EID] .*)$' "$cart/log.txt" >"$cart/bad.txt" ||
    fail "torii-${1:-0}.log has a broken line: $(head -2 "$cart/bad.txt")"
}
# Every byte of log file N after its text is zero: a new file is filled
# with zeros from RAM (on the cart, ROM read in SD mode is the SD buffer).
zeros_ok() {
  mtype -i "$cart/sd.img" "::/torii/logs/torii-${1:-0}.log" >"$cart/raw.log"
  python3 - "$cart/raw.log" <<'EOF' || fail "torii-${1:-0}.log isn't all zeros after its text"
import sys
data = open(sys.argv[1], "rb").read()
end = data.find(b"\0", 24)
if end >= 0 and data[end:].strip(b"\0"):
    junk = end + next(i for i, b in enumerate(data[end:]) if b)
    sys.exit(f"text ends at {end:#x}, then a nonzero byte at {junk:#x}")
EOF
}
card_ok() { fsck.fat -n "$cart/sd.img" >/dev/null 2>&1 || fail "fsck.fat finds the card damaged: $(fsck.fat -n "$cart/sd.img" 2>&1 | tail -3)"; }

# Hex of LEN bytes of cart SRAM (sram.bin) from OFFSET.
sram() { od -A n -t x1 -v -j $(($1)) -N $(($2)) "$cart/sram.bin" | tr -d ' \n'; }
blank() { [ -z "$(sram "$1" "$2" | tr -d f)" ]; }

case $part in
launch)
  new_card 65536 32
  boot -f 200
  # Off: no folder on the card, nothing in cart RAM page 0xF0, even after a launch.
  # shellcheck disable=SC2046 # $(keys) is a list of options
  boot $(keys 100 A) $(keys 160 A) -f 400
  grep -q '^kernel: launch: starting the game' "$cart/kernel.log" || fail "Hello.gba didn't launch"
  ! mdir -i "$cart/sd.img" ::/torii >/dev/null 2>&1 || fail "logging off made /torii"
  blank 0xF0000 0x2000 || fail "logging off wrote cart RAM page 0xF0"
  card_ok

  settings 14=1 26=2 # AUTO SAVE on (no "Save sav?" after a launch), LOGGING Info
  boot -f 200
  [ "$(mdir -b -i "$cart/sd.img" ::/torii/logs)" = "::/torii/logs/torii-0.log" ] || fail "not one log file"
  [ "$(mtype -i "$cart/sd.img" ::/torii/logs/torii-0.log | wc -c)" = 65536 ] || fail "the log file isn't 64 KB"
  [ "$(log 0 | head -1)" = "torii log 00000001     " ] || fail "header: $(log 0 | head -1)"
  has "logging info, build main"
  has "previous session: nothing in cart RAM"

  # SAVER and torii sort first: down twice to Hello.gba, then CLEAN BOOT.
  # shellcheck disable=SC2046
  boot $(keys 100 DOWN DOWN A) $(keys 160 A) -f 400
  has "launch: loading Hello.gba, CLEAN BOOT"
  has "launch: header: title"
  has "launch: CART clock on, rumble on"
  has "launch: starting the game"
  # Back in the kernel by a soft reset, cart RAM says where the session ended.
  soft_boot -f 200
  has "previous session: ended in Hello.gba"
  [ "$(log 0 | grep -c 'Torii kernel boot')" = 3 ] || fail "not one session marker a boot"
  # After a power cycle cart RAM holds nothing.
  lost="previous session: nothing in cart RAM"
  before=$(count "$lost")
  boot -f 200
  [ "$(count "$lost")" = $((before + 1)) ] || fail "after a power cycle, the boot report didn't say cart RAM was lost"
  card_ok

  # A GB game: the next boot names it by the title in its header, as it has
  # no game code. Zeros are NOPs to the GB CPU, so it runs.
  python3 -c "
rom = bytearray(32768)
rom[0x134 : 0x134 + 9] = b'TORIITEST'
open('$cart/Test.gb', 'wb').write(rom)"
  mcopy -i "$cart/sd.img" "$cart/Test.gb" ::/Test.gb
  # shellcheck disable=SC2046
  boot $(keys 100 DOWN DOWN DOWN A) $(keys 160 A) -f 400
  has "launch: loading Test.gb with Jagoomba Color"
  soft_boot -f 200 # cart RAM keeps the session only across a soft reset
  has "previous session: ended in Test.gb (TORIITEST)"

  # A launch cut off (the emulator stops in the frame that logs it): after
  # a soft reset, cart RAM says where it stopped.
  # shellcheck disable=SC2046
  boot $(keys 100 DOWN DOWN A) $(keys 160 A) -u 'launch: loading Hello.gba'
  soft_boot -f 200
  has "previous session: ended abnormally in launching (step 0): Hello.gba"
  # After a power cut, only the log's last line can say so, if its flush
  # finished. A frame holds the whole launch, so write that line here, and
  # after it part of one, as a flush cut off between sectors leaves.
  mtype -i "$cart/sd.img" ::/torii/logs/torii-0.log >"$cart/torii-0.log"
  python3 - "$cart/torii-0.log" <<'EOF'
import sys
data = bytearray(open(sys.argv[1], "rb").read())
end = data.index(b"\0", 24)
line = b"   9.99 I launch: loading Hello.gba, CLEAN BOOT\n   9.9"
data[end : end + len(line)] = line
open(sys.argv[1], "wb").write(data)
EOF
  mcopy -o -i "$cart/sd.img" "$cart/torii-0.log" ::/torii/logs/
  boot -f 200
  has "previous session: a launch never reached the game; its last line was:    9.99 I launch: loading Hello.gba"
  has "log: dropped 6 bytes of a line cut off by power loss"
  card_ok
  lines_ok
  zeros_ok
  echo "PASS: logging off touches neither the card nor cart RAM; at Info a launch is logged, and a cut-off one caught"
  ;;

debug)
  new_card 65536 32
  # English, RESET and SAVESTATE on, sleep key L+R+SELECT, menu key
  # L+R+START, fast patch engine, CART clock and rumble on, LOGGING Info.
  settings 0=0xE1E1 1=1 2=1 3=0 4=0 5=9 6=8 7=2 8=9 9=8 10=3 11=1 12=0 13=1 14=1 25=1 26=2
  boot -f 200
  cp "$cart/sram.bin" "$cart/sram-before.bin"
  # Hello.gba, BOOT WITH ADDON; in game, L+R+START, then down to DEBUG
  # (RESET SAVE LOAD DEBUG) and A. The menu stays, saying the snapshot is
  # saved, with RESET picked (a power-off would clear cart RAM): A on it goes
  # back to the kernel, whose boot logs the snapshot.
  # The game starts around frame 850, once the savestate file is made.
  # The message's first row (y 147, from x 20) shows in VRAM.
  # shellcheck disable=SC2046
  boot $(keys 100 DOWN DOWN A) $(keys 160 DOWN A) -k 1200:L+R+START -k 1206:- $(keys 1240 DOWN DOWN DOWN A) \
    -m 1300:$(printf 0x%X $((0x06000000 + (147 * 240 + 20) * 2))):400 -s 1300:"$cart/debug.png" $(keys 1320 A) -f 1700
  grep -qi 'e0 *03' "$cart/out.log" || fail "no green message after DEBUG: $(tail -3 "$cart/out.log")"
  has "launch: IRQ hook from"
  has "launch: in-game code: in-game menu (RTS_ReplaceIRQ)"
  has "patched words"
  # Past the save chip, the menu's screen copy and the savestate buffer
  # (0x00000-0xAFFFF), only page 0xF0 changed (cmp counts bytes from 1).
  changed=$({ cmp -l "$cart/sram-before.bin" "$cart/sram.bin" || true; } |
    awk -v states_end=$((0xB0000)) -v ours=$((0xF0000)) -v ours_end=$((0xF2000)) \
      '$1 - 1 >= states_end && ($1 - 1 < ours || $1 - 1 >= ours_end) { n++ } END { print n + 0 }')
  [ "$changed" = 0 ] || fail "DEBUG changed $changed bytes of cart RAM outside page 0xF0"
  has "debug info from the in-game menu: "
  has "(the hook is in place)"
  has "debug info: IE "
  blank 0xF1000 16 || [ "$(sram 0xF1000 16)" = "00000000000000000000000000000000" ] || fail "the debug info wasn't cleared"
  card_ok
  lines_ok
  zeros_ok
  echo "PASS: the in-game menu's DEBUG says it saved, writes only cart RAM page 0xF0, and RESET logs it"
  ;;

crash)
  new_card 65536 32
  boot -f 100 # makes the cart's files
  fram=$(sram 0 0x20000 | md5sum)
  # A game in NOR, listed in the kernel NOR's table: Diagnostics reads the
  # game NOR's ID, and a chip left in ID mode used to empty this list.
  dd if=tests/roms/hello/hello.gba of="$cart/game-nor.bin" conv=notrunc status=none
  python3 - "$cart/kernel-nor.bin" <<'EOF'
import struct, sys
rom = open("tests/roms/hello/hello.gba", "rb").read()
entry = b"Hello.gba".ljust(100, b"\0") + struct.pack("<HHHHIBBH", 0, 0, 0, 0, len(rom), 0x11, 0, 0) + rom[0xA0:0xB0]
nor = bytearray(open(sys.argv[1], "rb").read())
nor[0x7A0000 : 0x7A0000 + 132 * 64] = entry + bytes(132 * 63)
open(sys.argv[1], "wb").write(nor)
EOF
  nor_list=$(od -A n -t x1 -j $((0x7A0000)) -N 132 "$cart/kernel-nor.bin" | tr -d ' \n')
  # Diagnostics from L+R+SELECT at power-on, A to start. When its questions come, START
  # skips them; at the summary, SELECT crashes the kernel on purpose.
  presses=$(for f in $(seq 2000 24 4400); do printf -- '-k %d:START -k %d:- -k %d:SELECT -k %d:- ' "$f" $((f + 4)) $((f + 12)) $((f + 16)); done)
  # shellcheck disable=SC2086
  boot -k 1:L+R+SELECT -k 60:- -k 120:A -k 124:- $presses -f 5200
  has "diag: Diagnostics started"
  has "diag: unit tests: pass"
  has "diag: PSRAM holds a pattern: pass"
  has "diag: 800 KB of NOR read correctly: pass"
  has "diag: save chip untouched by the tests: pass"
  has "diag: SD write test: pass"
  has "diag: questions skipped"
  has "diag: crash test"
  has "crash: the kernel crashed"
  has "crash: pc 0200"
  id=$(head -1 dist/ezkernelnew.sym | cut -d' ' -f5)
  has "crash: build id $id"
  ! grep -q "crash: build id $id, not" "$cart/log.txt" || fail "the crash test's dump came from another build"
  has "crash: | "
  grep -q '^kernel error: crash: ' "$cart/kernel.log" || fail "the crash dump wasn't logged as an error"
  [ "$(sram 0 0x20000 | md5sum)" = "$fram" ] || fail "Diagnostics changed the save chip"
  [ "$(od -A n -t x1 -j $((0x7A0000)) -N 132 "$cart/kernel-nor.bin" | tr -d ' \n')" = "$nor_list" ] ||
    fail "Diagnostics' restart emptied the NOR game list"
  mkdir -p "$cart/card/torii/logs"
  mcopy -n -i "$cart/sd.img" ::/torii/logs/* "$cart/card/torii/logs/"
  python3 tools/logs.py "$cart/card" dist/ezkernelnew.sym "$cart/logs" >/dev/null
  grep -q 'crash: lr .* (Diagnostics+0x' "$cart/logs/torii.log" || fail "tools/logs.py didn't name the crash's lr: $(grep 'crash: lr' "$cart/logs/torii.log")"
  # With logging on, the session's state is in cart RAM too: the boot after
  # a crash says where it was.
  settings 26=2
  boot -f 200
  # The largest crash dump there can be (64 stack words that look like code
  # addresses, a full 1 KB of log) reaches the card whole, header first.
  python3 - "$cart/sram.bin" <<'EOF'
import struct, sys
regs = [0x1000 + i for i in range(16)] + [0x6000001F]
regs[13:16] = [0x03007E00, 0x08004321, 0x02001234]
log = b""
while len(log) + 70 <= 1024:
    log += b"   1.00 I " + b"y" * 59 + b"\n"
# Frames, the build ID (another kernel's), the stack's address and length
dump = b"TORIICR2" + struct.pack("<17I", *regs) + struct.pack("<IIII", 1234, 0x12345678, 0x03007E00, 64)
dump += struct.pack("<64I", *[0x0800ABCD] * 64) + struct.pack("<I", len(log)) + log.ljust(1024, b"\0")
sram = bytearray(open(sys.argv[1], "rb").read())
sram[0xF0100 : 0xF0100 + len(dump)] = dump
open(sys.argv[1], "wb").write(sram)
EOF
  dd if="$cart/sram.bin" of="$cart/dump.bin" bs=1 skip=$((0xF0100)) count=1376 status=none
  soft_boot -f 200 # the crash handler restarts the kernel without a power cycle
  has "crash: the kernel crashed 1234 frames after boot: pc 02001234"
  has "crash: build id 12345678, not this kernel's"
  has "previous session: the kernel crashed in the menu (step 0)"
  [ "$(log 0 | grep -c 'crash: stack 03007.*0800ABCD')" = 64 ] || fail "the big crash dump lost stack lines"
  blank 0xF0100 8 || [ "$(sram 0xF0100 8)" = 0000000000000000 ] || fail "the big crash dump wasn't cleared"
  # A crash in Diagnostics' crash test (phase 6) is reported as that.
  dd if="$cart/dump.bin" of="$cart/sram.bin" bs=1 seek=$((0xF0100)) conv=notrunc status=none
  printf '\x06' | dd of="$cart/sram.bin" bs=1 seek=$((0xF000C)) conv=notrunc status=none
  soft_boot -f 200
  has "previous session: Diagnostics' crash test, which crashes the kernel on purpose"
  # A dump in v0.2.0's layout is reported as skipped, and cleared.
  printf 'TORIICR1' | dd of="$cart/sram.bin" bs=1 seek=$((0xF0100)) conv=notrunc status=none
  soft_boot -f 200 # cart RAM past the save is lost on a power cycle, as on the real cart
  has "crash: an older kernel crashed"
  blank 0xF0100 8 || [ "$(sram 0xF0100 8)" = 0000000000000000 ] || fail "the old crash dump wasn't cleared"
  card_ok
  lines_ok
  zeros_ok
  echo "PASS: Diagnostics logs with logging off and leaves the save chip and NOR list alone; its crash test's dump reaches the log"
  ;;

rotate)
  new_card 65536 32
  settings 26=2
  boot -f 100
  # File 2 (sequence 7) has 30 bytes left; file 3 is an older one (sequence 6).
  python3 - "$cart" <<'EOF'
import sys
cart = sys.argv[1]
def make(seq, text_len):
    text = b"torii log %08d     \n" % seq
    line = b"   1.00 I filler line, long enough to fill the file quickly....\n"
    while len(text) + len(line) < text_len - 20:
        text += line
    last = b"   1.00 I end"
    text += last + b"." * (text_len - len(text) - len(last) - 1) + b"\n"
    assert len(text) == text_len
    return text + bytes(65536 - text_len)
open(f"{cart}/torii-2.log", "wb").write(make(7, 65536 - 30))
open(f"{cart}/torii-3.log", "wb").write(make(6, 4000))
EOF
  mcopy -o -i "$cart/sd.img" "$cart/torii-2.log" "$cart/torii-3.log" ::/torii/logs/
  boot -f 200
  # Whole lines only: what didn't fit in file 2 starts file 3.
  [ "$(log 2 | tail -c 1 | od -A n -t x1 | tr -d ' ')" = 0a ] || fail "file 2 ends in the middle of a line"
  [ "$(log 3 | head -1)" = "torii log 00000008     " ] || fail "file 3 wasn't started as sequence 8: $(log 3 | head -1)"
  has "Torii kernel boot" 3
  log 3 >"$cart/log.txt"
  ! grep -q "filler line" "$cart/log.txt" || fail "file 3 kept the old text"
  [ "$(mtype -i "$cart/sd.img" ::/torii/logs/torii-3.log | wc -c)" = 65536 ] || fail "file 3 isn't 64 KB"
  card_ok
  lines_ok 2
  lines_ok 3
  zeros_ok 2
  zeros_ok 3
  echo "PASS: a full log file hands over to the next, which starts empty, between lines"
  ;;

junk)
  # What v0.2.0 left on the real cart: file 0 (sequence 5) holds a header,
  # some text, then junk to its end.
  new_card 65536 32
  settings 26=2
  boot -f 100
  python3 - "$cart" <<'EOF'
import random, sys
random.seed(1)
text = b"torii log 00000005     \n" + b"   1.00 I the last good line\n"
junk = bytes(0 if i % 97 < 40 else random.randrange(1, 256) for i in range(65536 - len(text)))
open(f"{sys.argv[1]}/torii-0.log", "wb").write(text + junk)
EOF
  mcopy -o -i "$cart/sd.img" "$cart/torii-0.log" ::/torii/logs/
  boot -f 200
  mtype -i "$cart/sd.img" ::/torii/logs/torii-0.log | cmp -s - "$cart/torii-0.log" || fail "the junk file was changed"
  [ "$(log 1 | head -1)" = "torii log 00000006     " ] || fail "file 1 wasn't started as sequence 6: $(log 1 | head -1)"
  has "log: torii-0.log has junk after its text; going on in the next file" 1
  has "Torii kernel boot" 1
  boot -f 200 # the next boot goes on in file 1
  [ "$(count 'Torii kernel boot' 1)" = 2 ] || fail "the next boot didn't go on in file 1"
  # Junk with no zero byte in it: 0xFF from the text to the end of file 1.
  mtype -i "$cart/sd.img" ::/torii/logs/torii-1.log >"$cart/torii-1.log"
  python3 - "$cart/torii-1.log" <<'EOF'
import sys
data = open(sys.argv[1], "rb").read()
end = data.index(b"\0", 24)
open(sys.argv[1], "wb").write(data[:end] + b"\xff" * (len(data) - end))
EOF
  mcopy -o -i "$cart/sd.img" "$cart/torii-1.log" ::/torii/logs/
  boot -f 200
  [ "$(log 2 | head -1)" = "torii log 00000007     " ] || fail "file 2 wasn't started as sequence 7: $(log 2 | head -1)"
  has "log: torii-1.log has junk after its text; going on in the next file" 2
  mtype -i "$cart/sd.img" ::/torii/logs/torii-1.log | cmp -s - "$cart/torii-1.log" || fail "the 0xFF junk file was changed"
  card_ok
  lines_ok 2
  zeros_ok 2
  # tools/logs.py shows the junk as a count of its bytes, not the bytes
  # (v0.2.0's made a 42 KB line), and keeps the lines around it.
  mkdir -p "$cart/card/torii/logs"
  mcopy -n -i "$cart/sd.img" ::/torii/logs/* "$cart/card/torii/logs/"
  python3 tools/logs.py "$cart/card" dist/ezkernelnew.sym "$cart/logs" >/dev/null
  grep -q '^   1.00 I the last good line$' "$cart/logs/torii.log" || fail "tools/logs.py lost the line before the junk"
  grep -q '^\[[0-9]* bytes of junk\]$' "$cart/logs/torii.log" || fail "tools/logs.py didn't count the junk"
  [ "$(awk '{ if (length($0) > m) m = length($0) } END { print m + 0 }' "$cart/logs/torii.log")" -le 300 ] ||
    fail "tools/logs.py wrote a junk line"
  ! LC_ALL=C grep -q '[^[:print:][:space:]]' "$cart/logs/torii.log" || fail "tools/logs.py wrote bytes that aren't text"
  echo "PASS: a log file with junk after its text, with or without zeros in it, is left alone, logging goes on in the next file, and tools/logs.py counts the junk"
  ;;

full)
  # A small FAT16 card with under 64 KB free.
  new_card 16384 16
  # Hello.gba's save is there already, so launching it needs no new space.
  head -c 65536 /dev/zero | tr '\0' '\377' >"$cart/Hello.sav"
  mmd -i "$cart/sd.img" ::/SAVER
  mcopy -i "$cart/sd.img" "$cart/Hello.sav" ::/SAVER/Hello.sav
  free=$(mdir -i "$cart/sd.img" ::/ | sed -n 's/^ *\([0-9 ]*\) bytes free.*/\1/p' | tr -d ' ')
  head -c $((free - 49152)) /dev/urandom >"$cart/filler.bin"
  mcopy -i "$cart/sd.img" "$cart/filler.bin" ::/FILLER.BIN
  settings 14=1 26=3 # AUTO SAVE on, LOGGING Debug
  boot -f 200
  grep -q "no more SD logging until the next boot" "$cart/kernel.log" || fail "a full card didn't stop the logging"
  ! mtype -i "$cart/sd.img" ::/torii/logs/torii-0.log >/dev/null 2>&1 || fail "the log file it couldn't finish is still there"
  # The kernel still launches games: past SAVER, torii and FILLER.BIN.
  # shellcheck disable=SC2046
  boot $(keys 100 DOWN DOWN DOWN A) $(keys 160 A) -f 400
  grep -q '^kernel: launch: starting the game' "$cart/kernel.log" || fail "with a full card, Hello.gba didn't launch"
  mtype -i "$cart/sd.img" ::/FILLER.BIN | cmp -s - "$cart/filler.bin" || fail "the other file changed"
  card_ok
  echo "PASS: a full card stops the logging, not the kernel, and other files stay as they were"
  ;;

flood)
  # A list scrolled for seconds at Debug: its measured frames reach the card
  # as a summary, not a line each, and nothing is lost (v0.3's cart test
  # lost 107 KB of log to them)
  new_card 65536 32
  mmd -i "$cart/sd.img" ::/Many
  for i in $(seq -w 1 150); do
    mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba "::/Many/Game $i.gba"
  done
  settings 26=3 # LOGGING Debug
  # Open Many, hold DOWN for 5 s, back out, then SETTINGS and out again (the
  # log is written when SETTINGS closes)
  # shellcheck disable=SC2046
  boot $(keys 160 A) -k 200:DOWN -k 500:- $(keys 530 B R R) $(keys 610 L L) -f 750
  lines_ok
  has "lists: /Many: 0 folders, 150 files"
  has "frame-cycles summary: lists-scroll: "
  [ "$(count 'frame-cycles: ')" = 0 ] || fail "frame-cycles lines on the card"
  [ "$(count 'bytes of log lost')" = 0 ] || fail "log lost"
  grep -q '^kernel: frame-cycles: lists-scroll [0-9]' "$cart/kernel.log" || fail "no frame-cycles lines in mGBA's console"
  # Lines that fill half the RAM ring reach the card once no key is held,
  # with nothing else writing the log: SD list to NOR list and back, 16 times
  before=$(count "ui: pushed nor-list")
  # shellcheck disable=SC2046
  boot $(keys 160 R L R L R L R L R L R L R L R L R L R L R L R L R L R L R L R L) -f 600
  [ "$(count "ui: pushed nor-list")" -ge $((before + 16)) ] || fail "the lists didn't write the log while idle"
  # A few lines reach the card after 5 s with no key held, over the file menu too
  before=$(count "lists: /Many")
  # shellcheck disable=SC2046
  boot $(keys 160 A A) -f 600
  [ "$(count "lists: /Many")" = $((before + 1)) ] || fail "a few lines didn't reach the card after 5 s idle"
  has "ui: pushed file-menu"
  lines_ok
  card_ok
  echo "PASS: measured frames reach the card as summaries, nothing is lost, and idle lists write the log"
  ;;

*)
  echo "usage: $0 launch|debug|crash|rotate|junk|full|flood" >&2
  exit 2
  ;;
esac
