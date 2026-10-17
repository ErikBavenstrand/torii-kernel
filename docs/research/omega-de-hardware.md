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
| `0x09400000` | `SetSDControl` | 0 = off, 1 = SD on (buffer readable), 3 = start/read-state |
| `0x09420000` | `SetbufferControl` | 0 = off, 1 = buffer write, 3 = execute; used to send FAT tables and firmware pages |
| `0x09600000` | SD LBA low 16 bits | |
| `0x09620000` | SD LBA high 16 bits | |
| `0x09640000` | SD block count | 1–4 blocks; bit 15 set = write |
| `0x09660000` | `SetSPIControl` | 1 = enable; then `0x09E00000` reads the FPGA version |
| `0x09680000` | `SetSPIWrite` | 1 = program the staged 256 B page into config flash |
| `0x096A0000` | `Set_RTC_status` | 1 = GPIO port (RTC and rumble) on in game |
| `0x096C0000` | `Set_AUTO_save` | commented out in this kernel |
| `0x096E0000` | `Set_LED_control` | bit 7 enable, bits 5/4/3 breathing R/G/B, bits 2/1/0 SD-activity R/G/B |
| `0x09700000` | `Set_64MROM_flag` | 64 MB ROM mapping for NOR games |
| `0x09860000` | `SetPSRampage` | PSRAM window page (C3) |
| `0x09880000` | `SetRompage` | ROM map: `0x200` = boot from PSRAM, NOR game = `NORaddress >> 17` (C4) |
| `0x09C00000` | `SetRampage` | Save SRAM 64 KB bank (`0x00`, `0x10`) (E0) |
| `0x09E00000` | data port (read/write) | SD sector buffer (up to 2 KB), status reads; `0xEEE1` = busy |

### Other mapped hardware

- **Kernel NOR (S71, PL064/GL064):** `0x08000000`, AMD/Spansion command set (`0x555/0x2AA`).
  Settings at `+0x790000` (save info), `+0x7A0000` (NOR index), `+0x7B0000` (settings).
  GL064 uses buffered writes (`0x25 … 0x29`).
- **Game NOR (S98):** `0x09000000–0x097FFFFF`, same command set.
- **PSRAM:** `0x08800000` window.
- **SRAM / flash-save emulation:** `0x0E000000`; `Bank_Switching` uses the flash-save command
  sequence `0x5555=AA, 0x2AAA=55, 0x5555=B0, 0x0000=bank`.
- **RTC and rumble:** standard GBA cart GPIO at `0x080000C4` (data), `0xC6` (dir), `0xC8` (enable),
  S-3511-style serial protocol for the RTC; rumble games drive the motor from a GPIO pin.
  One register (`0x096A0000`) turns the whole port on for the game, so there is no
  separate rumble switch. The Omega DE has its own rumble motor on that port (the same
  one Mode B's DS Rumble Pak uses), and the bundled Goomba drives it for MBC5+RUMBLE
  games (ez-flash/omega-de-kernel#5). The kernel opens the port per game
  (`Game_hardware`, `Game_GPIO_status`): Drill Dozer and WarioWare: Twisted! and
  MBC5+RUMBLE GB/GBC games use rumble; other GBA games, MBC3+TIMER GB/GBC games and
  PocketNES use the clock.

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

- Bit 15 set = kernel mode (`0x8002` at boot). Bits 12–14 select which 8 MB of
  the 64 MB game NOR shows in the `0x09000000` window.
- `0x200` = game mode, all 32 MB of PSRAM at `0x08000000`.
- `N < 0x200` = game mode, game NOR from offset `N × 128 KB` at `0x08000000`
  (NOR games store `rompage = NORaddress >> 17`).

**Kernel-mode map**

| Address | Content |
|---|---|
| `0x08000000–0x087FFFFF` | Kernel NOR (S29PL064-style, ID `0x2202`, 64 KB sectors). Kernel at 0, save info `+0x790000`, NOR index `+0x7A0000`, settings `+0x7B0000` |
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

**RTC:** a GPIO RTC at `0x080000C4` (S-3511 protocol), used by the menu clock
in kernel mode and by games when register `0x096A0000` is 1.

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

## 5. Save prompt behaviour

On power-on, if a game ran since the last save, the kernel asks "Save sav?".
A copies cart SRAM to `SAVER/<game>.sav`. B (CANCEL) discards that session:
the pending flag is cleared without writing, and the next launch loads the
existing `.sav` back into SRAM. This is intended, and lets you throw away a
session.

One edge, observed in the source but not reproduced: `Save_savefile`'s result
is ignored, so if the SD write fails (card full, write error) the flag is still
cleared and that session is lost the same way as with B.

## 6. Open questions

- What the `0x09E00000` status values mean beyond `0xEEE1` (busy) and `0x0000/0x0001` handshakes.
- What `0x096C0000` (auto-save) does in current firmware.
- What logic the extra 130-word frames at minor `0x17` hold (likely BRAM/IOB tail frames).
- The `0xD200/0x1500` unlock is from the original EZ-Flash IV/Omega line. It's unknown whether
  the FPGA checks the exact address/value pairs or only the sequence.
