# Recovering a cart that starts in safe mode

**Torii v0.2.1 and v0.2.2 must not be used.** Both can erase EZ-Flash's boot
loader: v0.2.2 at the first boot on a cart with NOR games from the official
kernel, and v0.2.1 on WRITE TO NOR and on deleting a NOR game. The cart then
starts in EZ-Flash's safe mode and shows `No_Loader_found`. v0.2.3 can't
write there (`docs/research/omega-de-hardware.md`, *Kernel NOR and the boot
loader*).

These are the steps that recovered a cart on 2026-10-18. Your saves on the
card and your NOR games are untouched by them.

## What happened

The cart's kernel NOR holds EZ-Flash's boot loader at its start and the
kernel 256 KB in. The FPGA starts the loader, and the loader starts the
kernel. With the loader erased, the FPGA's safe mode takes over. Safe mode
can rewrite the loader ("Update loader"), but it reads the new loader from
the SD card's raw sectors, from byte `0x200`, not from a file. So the loader
image has to be written to the card below its file system first.

## You need

- A computer that can write an SD card's raw sectors. The steps below are
  for macOS, in the **Terminal app**: terminals inside other apps (an editor,
  Claude's) can't open raw disks, even with `sudo`.
- The card the cart uses, with a kernel that's safe to install:
  EZ-Flash's official `ezkernelnew.bin` (1.06) or Torii v0.2.3 or later,
  copied to its root as `ezkernelnew.bin`. Copy it before the steps below.
- EZ-Flash's loader image, `DE.BIN`, from
  <https://www.ezflash.cn/zip/de.bin.zip>. The one used on 2026-10-18 has
  SHA-1 `0c13deff474cd6be924c0e0c81f0f8a64a0f2217` and is 13444 bytes.

## 1. Prepare DE.BIN

Unzip it, check its SHA-1, and pad it to whole 512-byte sectors (27 of them):

```bash
unzip de.bin.zip
```

```bash
shasum DE.BIN
```

```bash
dd if=DE.BIN of=de-padded.bin bs=512 conv=sync
```

`shasum` must print `0c13deff474cd6be924c0e0c81f0f8a64a0f2217`, and
`de-padded.bin` must be 13824 bytes (27 × 512). (Match the file name's case
to what the zip holds.)

## 2. Find the card and check there is room

Put the card in the computer, then:

```bash
diskutil list
```

Find the card by its size: `/dev/diskN` with `N` a number. Every command
below uses that `N`. **A wrong `N` overwrites another disk.** Unmount it,
without ejecting:

```bash
diskutil unmountDisk /dev/diskN
```

Check where its first partition starts:

```bash
sudo fdisk /dev/rdiskN
```

The loader goes in sectors 1 to 27, so the first partition's start sector
must be **28 or more**. Cards formatted by a computer start it at 2048 or
8192. If it starts earlier, stop: writing would damage the file system.
Stop too if `fdisk` shows no partition table, or entries that make no
sense: some cards hold the file system from sector 0, with no partition
table, and sectors 1 to 27 are then part of it. Copy the card's files off,
reformat it with an MBR partition table (Disk Utility: *Master Boot Record*,
*MS-DOS (FAT)* or *ExFAT*), copy them back, and start this step again.

## 3. Write DE.BIN at byte 0x200

```bash
sudo dd if=de-padded.bin of=/dev/rdiskN bs=512 seek=1 conv=notrunc
```

`seek=1` skips sector 0, the partition table, which must stay as it is.

## 4. Read it back

```bash
sudo dd if=/dev/rdiskN bs=512 skip=1 count=27 | cmp - de-padded.bin && echo same
```

It must print `same`. Then eject the card:

```bash
diskutil eject /dev/diskN
```

## 5. Update the loader in safe mode

Put the card in the cart and switch the GBA on. Safe mode starts; choose
**Update loader** and wait until it says it's done.

## 6. Install a safe kernel: hold R

Switch the GBA off. Then **hold R while switching it on**, and confirm the
update: the loader installs `ezkernelnew.bin` from the card's root. Hold R
even if the kernel in the cart's flash looks fine: the loader does the
update before it starts the kernel, and a kernel like v0.2.2, which erases
the loader at boot, must not get to run. After the update, the cart starts
the kernel you installed.

## Afterwards

- The loader image stays at byte `0x200` on the card, harmless below the
  file system. Keep it there on a test card (`docs/release-checklist.md`):
  if a test bricks the cart, step 5 works at once.
- Torii's Diagnostics (settings page 2, DIAGNOSE) reads the loader and logs
  its CRC and state ("boot loader present"), whether it is this DE.BIN, and
  warns if it's blank, erased or overwritten.
