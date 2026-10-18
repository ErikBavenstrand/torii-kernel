# EZ-Flash Omega DE: firmware and register interface

Source: official repo `ez-flash/omega-de-kernel` (HEAD, last commit 2025-09-08).

| File | SHA-256 |
|---|---|
| `ezkernelnew.bin` (0x2719DC bytes) | `84b1f0b0f1ae01af4283574446ac1a4d41bf2f03c7bad6d34b7a768c1e7cf65e` |
| `image.bin` (0x1719DC bytes) | `3bab5ddb2c94fac750db203b32f3bfa2ef5bc635715ecc28476bdcd953275c84` |

## 1. Upgrade file layout

`ezkernelnew.bin` = compiled kernel (`0x000000–0x0FFFFF`, padded) + `image.bin` byte-identical
at `0x100000`. The file is mapped at GBA ROM `0x08000000`, so the `0x08xxxxxx` addresses in
`ez_define.h` are file offsets + `0x08000000`.

| ROM address | Content |
|---|---|
| `0x08100000–0x0818D610…` | Manuals, splash, UI backgrounds, icons |
| `0x08194410…` | Theme colour table |
| `0x08195000` (+`0x535BC`) | FPGA bitstream, XC6SLX9, firmware v5 |
| `0x08200000` (+`0x716A0`) | FPGA bitstream, XC6SLX16, firmware v7 |

Both extracted blobs match the CRC32s hard-coded in the kernel (`0x7E33C4FB`, `0x709EF09C`).
Extract them from `image.bin` (offsets are ROM address − `0x08100000`):

```bash
dd if=image.bin of=lx9.bin bs=1 skip=$((0x95000)) count=$((0x535bc))
dd if=image.bin of=lx16.bin bs=1 skip=$((0x100000)) count=$((0x716a0))
```

## 2. FPGA bitstreams (parsed with `tools/spartan6_bitstream.py`)

Both are raw Xilinx Spartan-6 `.bin` bitstreams (no `.bit` header, so no design name, tool
version or date strings). Every packet parses from sync word to `DESYNC` with nothing left over.

| | LX9 image (Rev.A carts) | LX16 image (Rev.B carts) |
|---|---|---|
| IDCODE written | `0x04001093` XC6SLX9 | `0x04002093` XC6SLX16 |
| Structure | single image | multiboot header + image |
| Main FDRI write | 170,157 words, 74% zero bytes | 231,953 words, 80% zero bytes |
| Extra FDRI writes | 3 × 130 words (FAR `0x0104/0x010E/0x020E`, minor `0x17`) | 1 × 130 words (FAR `0x0016`, minor `0x17`) |
| Final CRC | `0x002B1C55` | `0x002098AB` |
| Flash write address | `0x080000` | `0x100000` |

Shared config: `COR1=0x3D0C`, `COR2=0x89EE`, `CCLK_FREQ=0x3C0F`, `CTL=0x0081`, `SEU_OPT=0x1BE2`.

**Not encrypted.** The frame data is mostly zero bytes and highly structured. Encrypted
frames would look random. Spartan-6 only has AES on LX75 and larger parts.

**LX16 multiboot header** (first `0x44` bytes): writes `GENERAL1–4 = 0x0044 / 0x6B00`
(next and fallback image at SPI offset `0x44`, read with SPI opcode `0x6B` quad-output fast read),
`MODE_REG=0x3100`, then `IPROG`. The FPGA reloads itself from offset `0x44`, where the real
bitstream starts at the second sync word.

### What can and can't be recovered

- **Done:** device, configuration options, frame-write layout, and integrity values.
- **Possible with more work:** a frame-by-frame map of which parts of the chip are used, and
  pulling block-RAM init contents out of the BRAM frames. That needs the Spartan-6 frame-address
  layout, which comes from older community tools such as debit/bitfile research and isn't as
  well documented as 7-series.
- **Research-grade:** a full netlist. There's no maintained open Spartan-6 bitstream database
  like Project X-Ray. The most useful hardware model comes from the register interface below.

## 3. Cart register interface (from `Ezcard_OP.c`, `RTC.h`, patch `.s` files)

### Unlock / commit sequence

Every control-register write is wrapped like this:

```c
*(u16*)0x09FE0000 = 0xD200;
*(u16*)0x08000000 = 0x1500;
*(u16*)0x08020000 = 0xD200;
*(u16*)0x08040000 = 0x1500;
*(u16*)REG        = value;
*(u16*)0x09FC0000 = 0x1500;   // commit/lock
```

### Registers

| Address | Name in kernel | Values / meaning |
|---|---|---|
| `0x09400000` | `SetSDControl` | 0 = off, 1 = SD on (buffer readable), 3 = start/read-state; while it is on, ROM reads return the SD buffer (below) |
| `0x09420000` | `SetbufferControl` | 0 = off, 1 = buffer write, 3 = execute; used to send FAT tables and firmware pages |
| `0x09600000` | SD LBA low 16 bits | |
| `0x09620000` | SD LBA high 16 bits | |
| `0x09640000` | SD block count | 1–4 blocks; bit 15 set = write |
| `0x09660000` | `SetSPIControl` | 1 = enable; then `0x09E00000` reads the FPGA version |
| `0x09680000` | `SetSPIWrite` | 1 = program the staged 256 B page into config flash |
| `0x096A0000` | `Set_RTC_status` | bit 0 = GPIO port on: the clock (in kernel mode too: 1 and 3 let it answer, 0 and 2 keep it silent) and the rumble motor (real cart, 2026-10-18) |
| `0x096C0000` | `Set_AUTO_save` | commented out in this kernel |
| `0x096E0000` | `Set_LED_control` | bit 7 enable, bits 5/4/3 breathing R/G/B, bits 2/1/0 SD-activity R/G/B |
| `0x09700000` | `Set_64MROM_flag` | 64 MB ROM mapping for NOR games |
| `0x09860000` | `SetPSRampage` | PSRAM window page (C3) |
| `0x09880000` | `SetRompage` | ROM map: `0x200` = boot from PSRAM, NOR game = `NORaddress >> 17` (C4) |
| `0x09C00000` | `SetRampage` | 4 KB page of the save and work RAM at `0x0E000000` (E0); see *Save and work RAM* |
| `0x09E00000` | data port (read/write) | SD sector buffer (up to 2 KB), status reads; `0xEEE1` = busy |
| `0x09E20000` | `Set_rumble` | `0xF1` = rumble on the Omega DE's motor: 2 written at `0x08001000` then starts it, 0 stops it, while `0x096A0000` bit 0 is set and the ROM page isn't the kernel's (see *RTC and rumble*) |

**SD mode hides the ROM.** While `0x09400000` is on, reads from cart ROM
outside the data port don't return NOR or PSRAM: they return data left in
the FPGA's SD buffer (real cart, 2026-10-18). Code that reads ROM in SD mode
gets that data, and an SD write whose source buffer is in ROM writes it: v0.2.0
zero-filled its log files from a ROM array, and they filled with junk. The
kernel's SD code runs from IWRAM, and `disk_write` refuses a source in ROM.
Which buffer bytes a given address returns isn't known; the emulated cart
repeats the 2 KB buffer through the ROM space and starts it at `0xFF` at
power-on, so stale data never reads as zeros. It doesn't model instruction
fetches in SD mode.

### Other mapped hardware

- **Kernel NOR (S71GL064A08's 8 MB flash; the kernel handles a PL064 and a GL064):**
  `0x08000000` in kernel mode, AMD/Spansion command set (`0x555/0x2AA`). EZ-Flash's boot
  loader is at chip 0 and the kernel at chip `0x40000`, which ROM page `0x8002` shows at
  `0x08000000`; see *Kernel NOR and the boot loader* in section 4. The kernel's data, at
  window offsets: save info `+0x790000`, NOR index `+0x7A0000`, settings `+0x7B0000`, and
  Torii's second copy of the NOR index at `+0x780000` (*Game NOR list* below).
  GL064 uses buffered writes (`0x25 … 0x29`).
- **Game NOR (S98):** `0x09000000–0x097FFFFF`, same command set. Autoselect word `0x0E` reads
  `0x223D`; `Loadfile2NOR` writes only to a chip with that ID. The kernel (`NORflash_OP.c`)
  erases sectors (`80 … 30`, four `0x30`s across the 32 KB boot sectors at 0 and `0x3FE0000`),
  programs with the 16-word buffer (`25`, count 15 at the buffer, 16 words, `29`; a wrong count,
  a word outside the buffer page or a missing `29` aborts until `AA, 55, F0`), and before each
  write runs `C0` (PPB command set), `80, 30` (erase all PPBs), `90, 00` (exit). Each 8 MB of the
  chip is a ROM page step of `0x1000`.
- **PSRAM:** `0x08800000` window.
- **SRAM / flash-save emulation:** `0x0E000000`, paged by `0x09C00000`; `Bank_Switching` uses
  the flash-save command sequence `0x5555=AA, 0x2AAA=55, 0x5555=B0, 0x0000=bank`.
- **RTC and rumble:** standard GBA cart GPIO at `0x080000C4` (data), `0xC6` (dir), `0xC8` (enable),
  S-3511-style serial protocol for the RTC. `0x096A0000` bit 0 turns the port on.
  The Omega DE has its own rumble motor (the one Mode B's DS Rumble Pak uses). **It runs
  only while `0x096A0000` bit 0 is set and the ROM page isn't the kernel's**, and then
  two things drive it (real cart, 2026-10-18, Diagnostics and `tests/roms/rumble`):

  - **The EZODE way**, as EZ-Flash's Goomba (`emusrc/src/mappers.s`, `MBC5RAMB`) and the
    bundled Jagoomba Color do it. All 16-bit writes:

    ```
    unlock (0x09FE0000=D200, 0x08000000=1500, 0x08020000=D200, 0x08040000=1500)
    0x09E20000 = 0x00F1    ; the motor's mode
    0x09FC0000 = 0x1500    ; lock
    0x08001000 = 2         ; motor on
    0x08001000 = 0         ; motor off
    ```

    Jagoomba runs the whole sequence, 2 then 0 at once, each time the GB game sets bit 3
    (rumble) of its MBC5 RAM bank register, and nothing when it clears it. Pokémon
    Pinball asks on 4 or 5 frames in a row, and those short pulses rumble. Holding 2 for
    a second rumbles too, as a Pokémon Pinball: Ruby & Sapphire rumble patch does
    (github.com/Wokann/PokemonPinballRubySapphire_CHN, `include/rumble.h`, which names
    the interface "EZODE"). The writes to `0x08001000` don't reach the PSRAM the game
    runs from: it read `0000` before and after them in game mode. Nothing sets
    `0x09E20000` back afterwards, as no other value for it is known; Jagoomba leaves it
    at `0xF1` too, and games launched after it ran fine.
  - **GPIO pin 3**, as Drill Dozer's cart wires it: enable `1`, direction bit 3, data
    bit 3 (on) or clear (off). It rumbled in game mode.

  Under the kernel's ROM page `0x8002`, `0x08001000` is the kernel NOR's window: the
  write goes to the NOR, which ignores it as no flash command, and the motor stays
  still. GPIO pin 3 didn't rumble in kernel mode either (v0.2.3). Diagnostics therefore
  pulses the motor with the ROM page at PSRAM (`0x200`), from IWRAM with interrupts off,
  and puts the kernel's page back after; there, `0x08001000` read `FFFF` throughout.
  With `0x096A0000` at 0, Jagoomba's pulses didn't rumble in game mode, matching
  ez-flash/omega-de-kernel#5 (Goomba's rumble needs GAME RTC on). So the kernel opens
  the port for rumble games too (`Game_hardware`, `Game_GPIO_status`): Drill Dozer and
  WarioWare: Twisted! and MBC5+RUMBLE GB/GBC games use rumble; other GBA games,
  MBC3+TIMER GB/GBC games and PocketNES use the clock. The emulated cart has no motor
  but traces what it would do under this rule (`rumble on`, `rumble off`, `(GPIO pin
  3)`, or `rumble write ignored: ...`).

### FPGA firmware update protocol

1. `Read_FPGA_ver()`: high nibble `0xA`/`0xB` = LX16 (Rev.B), anything else = LX9 (Rev.A).
   Low byte = firmware version.
2. If the version is lower than the built-in one, CRC32 the embedded bitstream.
3. For each 256 B page: buffer = `[u32 flash_addr][256 B data]` → `Send_FATbuffer(…, 2)`
   (write to `0x09E00000` with buffer control 1), then `SPI_Write_Enable()`, poll
   `0x09E00000` until it reads 0, then `SPI_Write_Disable()`.
4. Tell the user to power off. The FPGA reloads from SPI flash on the next power-up.

The FPGA writes its own config SPI flash, so the GBA can never reach the flash directly.
If an update fails, the cart is bricked. That's why EZRecovery exists.

## 4. Memory map and game launch

Worked out from the kernel source and confirmed by booting the kernel on the
emulated cart (`emu/`).

**ROM page register (`0x09880000`)**

- Bit 15 set = kernel mode. Its low 9 bits pick where `0x08000000` starts in
  the kernel NOR, in 128 KB steps: `0x8000` shows the boot loader (chip 0),
  `0x8002` the kernel (chip `0x40000`), which is what the kernel runs under.
  Bits 12–14 select which 8 MB of the 64 MB game NOR shows in the
  `0x09000000` window.
- `0x200` = game mode, all 32 MB of PSRAM at `0x08000000`.
- `N < 0x200` = game mode, game NOR from offset `N × 128 KB` at `0x08000000`
  (NOR games store `rompage = NORaddress >> 17`).

**Kernel-mode map**

| Address | Content |
|---|---|
| `0x08000000–0x087FFFFF` | Kernel NOR from chip `0x40000` (ROM page `0x8002`): the kernel at `0x08000000`, Torii's second NOR index `+0x780000`, save info `+0x790000`, NOR index `+0x7A0000`, settings `+0x7B0000`; from `+0x7C0000` the window wraps onto the boot loader at chip 0 |
| `0x08800000–0x08FFFFFF` | PSRAM window, page register `0x09860000` in steps of `0x1000` per 8 MB |
| `0x09000000–0x097FFFFF` | Game NOR window (64 MB chip, 128 KB sectors, 32 KB boot sectors in the first and last 128 KB, 16-word buffered programming) |
| `0x09E00000` | Data port (SD sectors, FAT table, status) |

Registers between `0x09400000` and `0x09700000` overlap the game NOR window;
the unlock sequence decides whether a write is a register or a NOR command.

**SD access:** write LBA (`0x09600000`/`0x09620000`) and count (`0x09640000`,
bit 15 = write, up to 4 sectors), with the data going through the port.
While SD control is 3 the port returns status; `0xEEE1` means busy.

**Launching from SD:**

1. The kernel walks the file's FAT chain and writes `[sector offset, start
   sector]` pairs for each contiguous run (terminated by `0xFFFFFFFF`) into a
   1 KB table. Word `0x1F4/4` is a mode: 0 = load the ROM, 2 = only set
   parameters. The RTS file's table goes at `+0x300`.
2. `Send_FATbuffer` writes the table to the port with buffer control 1, then
   sets buffer control 3. In load mode the FPGA copies the file into PSRAM and
   the port reads non-zero, then not 1, when done.
3. With patches enabled the kernel loads the ROM itself through the PSRAM
   window instead, patching each 128 KB block on the way.
4. `SetRompageWithHardReset(0x200)` maps PSRAM at `0x08000000` and soft-resets
   (SWI 0) into the game, or hard-resets (SWI 0x26) when L is held.
   The hard reset reboots through the BIOS intro, which clears all RAM and I/O
   registers but not the cart: its rompage and save type stay as the kernel set
   them, so the game boots from PSRAM. mGBA's HLE BIOS lacks SWI 0x26; the
   patched emulator clears memory and registers and starts at `0x08000000`
   without the intro (`tests/hard_reset.sh`).

**Saves:** before launch the kernel writes the save path and size to kernel NOR
`+0x790000` with flag `0x11` and loads the `.sav` into SRAM (`0x0E000000`, 64 KB
banks via `0x09C00000` or the flash-style `AA/55/B0` bank command). On the next
power-on, if the flag is set, it asks (or auto-saves) and copies SRAM back to
`SAVER/<game>.sav`. So a save only reaches the SD card on the next boot of the
kernel.

**Save types:** the kernel never patches a game's save code. It sends the save
type to the FPGA in the FAT table's parameter block, and the FPGA presents the
matching chip in game mode:

| Word | Content |
|---|---|
| `0x1F0` | Game file size |
| `0x1F4` | Mode: 1 = copy ROM to PSRAM, 2 = parameters only |
| `0x1F8` | Sectors per cluster |
| `0x1FC` | `save type << 24`, OR'd with the save size |

Save types (`Get_saveMODE`, looked up by game code in `saveMODE.h`, 2,800+
entries, or picked in the menu): `0x11` SRAM 32K, `0x21` EEPROM 512,
`0x22` EEPROM 8K, `0x23` EEPROM 8K for games over 16 MB (EEPROM only at
`0x0DFFFF00`), `0x31` Flash 128K, `0x32`/`0x33` Flash 64K, `0x10` unknown
(treated as 64 KB SRAM). Pokémon Ruby, Sapphire, FireRed, LeafGreen and
Emerald (`BPEE`, `BPEJ`) are all `0x31`.

**Save and work RAM** (from *EZ-FLASH OMEGA DE DOCUMENT.pdf* in this repo,
matching what the kernel does). `0x0E000000–0x0E00FFFF` shows 64 KB starting
at page `0x09C00000` × 4 KB:

| Pages | Memory | Used for |
|---|---|---|
| `0x00`, `0x10` | FM28V100 FRAM, 128 KB | game saves |
| `0x20`, `0x30` | S71GL064 SRAM (1 MB; its first 128 KB are unused behind the FRAM) | the in-game menu's copy of VRAM (`0x10000` + `0x2C00` bytes) |
| `0x40`–`0xAF` | S71GL064 SRAM | the save-state buffer: the 0x70000-byte `RTS/<game>.rts` is loaded here at launch. VRAM, IWRAM, EWRAM, OAM, palette, IO, registers, and a flag at `0xAFFF0` |
| `0xC0`, `0xD0` | S71GL064 SRAM | Torii: Diagnostics' pattern test (free otherwise) |
| `0xF0` | S71GL064 SRAM | Torii: what the kernel keeps for its next boot (`source/cartlog.h`): where the session got to at `0xF0000`, a kernel crash dump at `0xF0100`, the in-game menu's DEBUG snapshot at `0xF1000`, the EEPROM probe ROM's report at `0xF1800` |

Pages `0x20` and up have no battery: on the real cart they survive a soft
reset (RESET, or the crash handler's restart) but not a power cycle
(2026-10-18). So the session state, the crash dump and the DEBUG snapshot
reach the next boot only without a power cycle. The emulated cart fills
them with `0xFF` at power-on, which every `mgba-shot` run is;
`OMEGA_SOFT_RESET=1` starts a run as after a soft reset instead.

The page applies in game mode too: the menu sets `0x20` to keep the screen
and `0x40`+ for states, then goes back to `0x00`. The menu does this
whatever the game's save chip, so we take pages from `0x20` up to be plain
RAM even while the FPGA presents a flash chip on the save pages (inferred
from the kernel; the PDF doesn't say). The emulated cart models this
with one 1 MB `sram.bin` (before 2026-10-17 it had 512 KB, so the state
buffer wrapped onto the game's save). `tests/menu.sh` checks it.

**RTC:** a GPIO RTC at `0x080000C4` (S-3511 protocol), used by the menu clock
in kernel mode and by games. It answers only while bit 0 of `0x096A0000` is
set, in kernel mode too; the kernel sets it to 1 at boot.

**Verified with a commercial game:** Pokémon Emerald (`BPEE`, 16 MB, Flash
128K, RTC) runs the whole cycle on the emulated cart, launched from SD: new
game, in-game save, power-cycle with the kernel's "Save sav?" prompt writing
`SAVER/Emerald.sav` (identical to the first 128 KB of cart SRAM), and
Continue on the next launch. The game's save sits in cart SRAM as plain
bytes in Emerald's own layout (4 KB sections, signature `0x08012025`); the
kernel copies it without interpreting it. The kernel sets `0x096A0000` for
this game: with game-mode RTC disabled in the model, Emerald says "The
internal battery has run dry" after the title screen; with it enabled, it
doesn't.

**Settings** live in the kernel NOR at `+0x7B0000`, as 16-bit slots (names in
`source/ez_define.h`). A new cart's NOR is erased, so slots the kernel never
wrote read `0xFFFF`. The emulated cart does the same, since it creates
`kernel-nor.bin` filled with `0xFF` (`tests/settings.sh`). At boot
`CheckSwitch` maps every value it doesn't know to a default: English,
addons off, fast patch engine, thumbnails off, CLOCK and RUMBLE on (slots 13
and 25), auto-save off, LED defaults. Saving any setting erases the sector
and writes slots 0–255 back. The kernel's own slots get their values, slots
14–64 keep what they read (so DS Style's 23 and 24 stay `0xFFFF` until DS
Style writes them), and 65–255 become `0` from the unused part of the
buffer. The hotkey slots 5–10 are written as the defaults the settings page
shows (L R SELECT, L R START) the first time any row is saved.

The in-game key masks in `GBApatch.c` used to build `1 << value` from
slots 5–10 with no range check, so an erased slot gave `0x3FF`, "no key
held". Since `b736015`, slots past 9 count as the defaults the settings page
shows.

**Game NOR list.** The official kernel keeps the list of NOR games at kernel
NOR `+0x7A0000`: 64 entries of `FM_NOR_FS` (132 bytes each: file name, ROM
page, patch flags, NOR area size, save type, then the 16 bytes of title and
code from header `0xA0`). Games sit back to back from offset 0, each in whole
128 KB blocks, and a new one goes after the last. At boot the kernel walks
the list against the game NOR, comparing each entry's 16 bytes with the
header at its offset (the official kernel also required `0x96` at `0xB2`).
Saving erases the sector and programs it, and the official kernel saved an
empty list whenever the walk found no games.

Torii (`source/norlist.h`) keeps the same 64 entries but adds a 32-byte
trailer at `+0x2100` (magic `TNL2`, a sequence number, the game count, a
CRC-32), and writes a second copy at `+0x780000` (v0.2.1 and v0.2.2 put it at
`+0x7C0000`, which is the boot loader: see below). A save writes the copy the
list wasn't read from first, then the other, so a power cut leaves one whole
copy; the reader takes the whole copy with the higher number, else the
trailerless list at `+0x7A0000` as an older kernel's. The official kernel
reads Torii's copy A unchanged, and saves only copy A, without the trailer:
when A has none but B is whole, the reader takes whichever more of whose
games are in NOR, so games added after a downgrade stay. Entries from a
trailerless list count as unmarked, since older kernels left `reserved1`
uninitialised. Each game Torii writes also gets a
completion mark, programmed last, in the last 512 bytes of its NOR area
(magic `TNM1`, offset, size, header `0xA0–0xBF`, its list entry, CRC-32):
the area grows by a block when the ROM or its patch reaches those bytes,
which most commercial ROMs (whole powers of two) do. When that block no
longer fits, the game is written unmarked and listed by its header, as an
older kernel's.
Its entry's `reserved1` byte is `'T'`. A marked entry without its mark isn't
listed, and a game with a mark the list lacks is put back. A power cut
during FORMAT ALL's chip erase can still leave a half-erased game whose
header and mark survive; format again. Erase and program
take the model no time, so `OMEGA_POWER_CUT` (`emu/mgba/src/gba/cart/omega.c`) cuts the
power between operations for `tests/nor_safety.sh`.

**Kernel NOR and the boot loader.** From the official *EZ-FLASH OMEGA DE
DOCUMENT.pdf* (its NOR allocation lists 16-bit word addresses, so double
them for bytes) and a bricked cart (2026-10-18):

| Chip offset | Window offset (page `0x8002`) | Content |
|---|---|---|
| `0x000000–0x00FFFF` | `0x7C0000–0x7CFFFF` | EZ-Flash's boot loader (ROM page `0x8000` shows it at `0x08000000`) |
| `0x010000–0x03FFFF` | `0x7D0000–0x7FFFFF` | unused |
| `0x040000–…` | `0x000000–…` | the kernel (`ezkernelnew.bin`: 2.44 MB with its images; the PDF allows 2 MB) |
| `0x7C0000–0x7FFFFF` | `0x780000–0x7BFFFF` | the kernel's data: Torii's NOR list copy B, save info, NOR list, settings |

The chip is 8 MB and the window too, so the window's last 256 KB wrap onto
the chip's first: offsets from `0x7C0000` are the loader. v0.2.1 and v0.2.2
wrote the NOR list's copy B at `0x7C0000`. The erase cleared the loader's
first 8 KB sector, the program wrote the list over it and then hung on
loader bytes it couldn't program (the chip's DQ5 error), and the cart came
back up in EZ-Flash's safe mode, `No_Loader_found`. `docs/recovery.md` has
the recovery. The PDF's own table lists the data records at the window
offsets, not chip offsets; only the brick shows the window starts at chip
`0x40000`.

Sector maps: the S29PL064J (autoselect `227E 2202 2201`, the kernel's
`S71id == 0x2202`) has 64 KB sectors with eight 8 KB boot sectors at each
end (Infineon S29PL-J datasheet) and no write buffer. The S29GL064A, which
the kernel assumes for any other ID and programs through the write buffer,
comes with eight 8 KB boot sectors at the bottom (model R4, `227E 2210
2200`) or the top (R3, `2210 2201`), or uniform 64 KB sectors (R1/R2,
`220C`; IDs from flashrom). So the loader is 8 KB sectors on a PL064J and a
bottom-boot GL064A, where one erase there clears 8 KB, and one 64 KB sector
on the others, where it clears all of it. The window's `0x7B0000–0x7BFFFF`,
the settings sector, is 8 KB sectors on a PL064J and a top-boot GL064A: an
erase there clears only 8 KB. Which chip a cart has is in Diagnostics' log
("kernel NOR id"). The PDF says "S71GL064A08" but doesn't give the model.

**What writes the kernel NOR** (audit for v0.2.3, from `grep` over `source/`
for `Save_info`, `FlashBase_S71`, the command bytes `0x30`, `0xA0`, `0x25`,
`0x29` and `0x10`, `0x08000000` writes and `0x09880000`/`SetRompage`):

| Path | Writes | Through |
|---|---|---|
| Settings saved (`Save_SET_info`: SETTINGS, a game's save type, thumbnails) | `0x7B0000`, 512 bytes | `Save_info` |
| Pending-save record set at launch, cleared at the next boot (`Save_sav_info`) | `0x790000`, 512 bytes | `Save_info` |
| NOR list saved: WRITE TO NOR, delete latest, FORMAT ALL, the boot's migration of an older list (`Save_NOR_list`) | `0x780000` then `0x7A0000` (or the other way), 0x2120 bytes each | `Save_info` |
| `Read_S71NOR_ID` (in `Save_info` and Diagnostics' first step) | autoselect `90` and reset `F0` at `0x0AAA`/`0x0554`/0: commands, no erase or program | — |
| The register unlock (`SetRompage` and the other `Set*` in `Ezcard_OP.c`, the in-game menu's `.s` files) | `0x1500`/`0xD200` at `0x08000000`, `0x08020000`, `0x08040000`: not NOR commands | — |
| Diagnostics' loader check (`diag_loader_read`) | sets ROM page `0x8000` and reads 64 KB, from IWRAM with interrupts off, then `0x8002` again | — |

`Save_info` (`source/Ezcard_OP.c`) is the only code that erases or programs
the kernel NOR. It refuses, writing nothing, any record that
`kernel_nor_record_ok` (`source/bootloader.c`, host tested) rejects: one
that isn't a 64 KB-aligned sector between `0x780000` and `0x7BFFFF` holding
up to 64 KB, or, at `0x7B0000`, more than 8 KB. Game NOR writes
(`NORflash_OP.c`) go to `0x09000000`, a different chip. Nothing else sets
ROM page `0x8000`: the in-game menu's reset sets `0x8002`, launches set a
game page, and `cartlog.c` sets `gl_currentpage`. Static asserts in
`NORflash_OP.c` keep both list copies inside the data sectors.

The emulated cart (`emu/mgba/src/gba/cart/omega.c`) models this: the window
at chip `(page & 0x1FF) × 128 KB` wrapping at 8 MB, the PL064J (no write
buffer) and the three GL064A maps (`OMEGA_KERNEL_NOR=gl064`, `gl064top`,
`gl064uniform`), a program over unerased bits failing with DQ5 until a reset
(F0), and a stand-in loader seeded when the chip below the kernel is blank.
Its limits: erases and programs take no time, instruction fetches from the
cart see the chip's data even while it serves status or ID words (the
kernel's flash code and IRQ handler run from IWRAM, so it never fetches
then), command addresses decode A10–A0, and under page `0x8000` fetches
mirror the first 256 KB. `kernel-nor.bin` holds the chip in window order
(page `0x8002`), so the loader is at its offset `0x7C0000`. Any erase or
program below chip `0x40000` prints `BOOT LOADER WRITTEN` and stops the run
with status 76 (`OMEGA_LOADER_WRITES=allow` runs on, as the real chip
would). `tests/loader_safety.sh` runs every path above, power cuts in them
and the GL064A, and fails on v0.2.1 and v0.2.2.

## 5. Save prompt behaviour

On power-on, if a game ran since the last save, the kernel asks "Save sav?".
A copies cart SRAM to `SAVER/<game>.sav`. B (CANCEL) discards that session:
the pending flag is cleared without writing, and the next launch loads the
existing `.sav` back into SRAM. This is intended, and lets you throw away a
session.

One edge, observed in the source but not reproduced: `Save_savefile`'s result
is ignored, so if the SD write fails (card full, write error) the flag is still
cleared and that session is lost the same way as with B.

## 6. In-game menu hook

See [hook-coverage.md](hook-coverage.md): how the kernel hooks a game's IRQ
for the in-game menu, and which games it misses.

## 7. Logging and Diagnostics

The kernel logs to `/torii/logs` on the card (`source/tlog.c`) and keeps
a little state in cart SRAM page `0xF0` (table above). Things the
emulated cart can't tell, which the real cart's logs will:

- **Page `0xF0` doesn't survive a power cycle** (real cart, 2026-10-18),
  only a soft reset. The boot report then says "nothing in cart RAM". The
  log on the card doesn't depend on it: a launch whose last logged line is
  its first ("launch: loading") is reported as cut off either way.
- **The kernel's crash handler** (`source/crash.s`) checks, on every IRQ,
  where the IRQ interrupted the kernel. What the real BIOS does on an
  undefined instruction isn't known here (mGBA's HLE BIOS skips it), so
  a crash that disables IRQs, or loops inside the kernel's code, goes
  uncaught.
- **WAITCNT:** the real cart reads NOR and PSRAM correctly at WS0 2/1 with
  prefetch (cart test of 2026-10-18), so the menu runs at `0x4018`
  (`source/waitcnt.h`). Untested at that speed, and kept at the BIOS's
  `0x0000`: writes to the control registers, transfers through the SD buffer
  at `0x09E00000`, NOR erases and writes, the RTC's serial lines, and
  launching, with its PSRAM writes. Games start at `0x0000`. In
  mGBA, opening a folder of 300 files takes 23 frames instead of 40. Whether
  the real cart runs the menu's code correctly at `0x4018` is still to be
  confirmed. Diagnostics times faster settings and checks their data against
  the default's.
- **EEPROM 512 vs 8K** (cart test of v0.2.1, `cart-test-2026-10-18b.md`,
  with the probe ROM `tests/roms/eeprom` started as a game):
  - The address width is decided per request: at save types `0x21` and
    `0x22`, 6-bit and 14-bit requests both read and write.
  - Even at `0x21`, the EEPROM holds 1024 distinct 8-byte blocks: 14-bit
    writes to blocks 65 and 1023 didn't land on blocks 1 and 63. A `0x21`
    save file is only 512 bytes, though, so blocks past 63 don't survive a
    power cycle.
  - Commercial games' writes keep all 64 bits (Crash Bandicoot XS's save).
    The probe's own writes lost their last byte (it read `FF`), polling for
    ready right after the stop bit; Crash's EEPROM_V122 library waits over
    100 cycles first. v0.2.2's probe tries four ways to write to settle it.
  - In kernel mode (no game started), reads at `0x0DFFFF00` return `AAAA…`
    at both types and widths: the EEPROM answers only in game mode. The
    emulated cart answers as an EEPROM only once a BIOS SoftReset or
    HardReset has started the game in game mode, as every launch does.
- **Rumble** (resolved, real cart, 2026-10-18): the motor needs
  `0x096A0000` bit 0 and a ROM page other than the kernel's; pulses and a
  held 2 at `0x08001000` both work, the writes don't reach the PSRAM, and
  GPIO pin 3 drives it in game mode (*RTC and rumble*).
- **SD writes through `Write_SD_sectors` use 16-bit DMA** from the
  caller's buffer, and FatFs passes whole sectors straight from the
  buffer given to `f_write`. A buffer at an odd address shifts the data by
  a byte, as the DMA rounds the address down (seen on the emulated cart
  too). The logger writes through a word-aligned buffer for that reason.

## 8. Open questions

- What the `0x09E00000` status values mean beyond `0xEEE1` (busy) and `0x0000/0x0001` handshakes.
- What `0x096C0000` (auto-save) does in current firmware.
- What logic the extra 130-word frames at minor `0x17` hold (likely BRAM/IOB tail frames).
- What `0x09700000` (`Set_64MROM_flag`) changes in the ROM map. The emulated cart stores it and
  maps only 32 MB of a NOR game. Its NOR erases and writes also finish at once, so emulation
  can't cut power in the middle of one.
- The `0xD200/0x1500` unlock is from the original EZ-Flash IV/Omega line. It's unknown whether
  the FPGA checks the exact address/value pairs or only the sequence.
