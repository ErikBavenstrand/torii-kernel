# v0.4 library index and user data: change detection, format, safe writes, background scan

Researched 2026-10-20 for tickets B3 (library index) and B5 (favourites, recents, boot position). Sources: the vendored FatFs R0.15 (`source/ff15/ff.c`, read directly), elm-chan's FatFs docs, Linux and Windows driver sources, and launcher sources (TWiLight Menu++, MinUI, EmulationStation, RetroArch). Torii's code paths: `source/Ezcard_OP.c`, `source/engine/jobs.h`, `source/engine/sdread.h`, `source/crash.s`, and the cart speed figures in `docs/research/cart-test-2026-10-18.md`.

---

### 1. Do folder dates show card changes?

> **Answer:** No. FatFs, Windows and FAT in general don't update a folder's own date when files are added, removed or renamed inside it. Linux does on exFAT, and partly on vfat. The root folder has no date at all, and a change in a subfolder never reaches its parent. "Folder dates checked at boot" would miss games copied from Windows, so it should not be the trigger.
> **Confidence:** 🟢 high for FatFs (read the code) and for "the root has no date". 🟡 for Windows and Linux. 🔴 for macOS.

**Findings**
- 🟢 FatFs writes a modification time only into the object's own entry: in `f_open` on create (ff.c 3783–3796), in `f_sync` (4174, 4194), in `f_mkdir` (5092–5115) and in `f_utime` (5317–5322). `dir_register`, `dir_remove`, `f_unlink` and `f_rename` never touch the parent's entry. So Torii's own writes never change a folder date. [1]
- 🟢 On FAT the root directory has no directory entry, so it has no timestamp. Games kept in the root (common on EZ-Flash cards) have no date to check. [1][6]
- 🟡 Windows fastfat: `FatCreateNewDirent` and `FatDeleteDirent` don't update the parent's LastWriteTime. Rename wasn't read. Fontconfig's code says plainly that Windows doesn't update a FAT folder's mtime when a file is added or removed, which is why it checksums the entries instead. [4][5]
- 🟡 Linux vfat updates the parent's mtime on create, mkdir and rename (`vfat_add_entry`, `vfat_update_dir_metadata`). The unlink path showed no update. Linux exfat updates it on create, unlink and rename. Both are in-memory changes written back later, so pulling the card can lose them. [2][3]
- 🔴 macOS: no source read. FreeBSD's msdosfs, macOS's ancestor, didn't update a folder's mtime on create as of 2011. [7]
- 🟢 Launchers don't use folder dates either. TWiLight Menu++ and MinUI list the open folder live with `readdir` on every visit, and keep only recents and collections lists. RetroArch builds playlists only when the user starts a scan, and skips paths it already has. [8][9][11]

**Cheap signals and what each one misses**

| Signal | Cost | Catches | Misses |
| --- | --- | --- | --- |
| Folder dates | one `f_stat` per folder | Linux and exFAT adds | Windows and FatFs changes, the root, nested folders: unreliable 🟢 |
| Volume serial (`f_getlabel` vsn) | 1 call | a reformat, a different card | every file change 🟡 |
| Free clusters (`f_getfree`) | FAT32: free when FSInfo is valid. exFAT: a bitmap scan on the first call (~128 sectors for 64 GB at 128 KB clusters) | most adds and deletes | renames, and a swap of files that use the same number of clusters. Windows or macOS can leave FSInfo stale 🟢 [1][10] |
| Hash of the root listing | 1 folder walk | changes in the root | changes inside subfolders |
| Full walk with `f_readdir` only (name, size, date of each entry, hashed per folder) | about 1 s for 2,000 files (estimate below) | everything the index needs | nothing |

**Cost of a full walk (estimate; measure it)**
- 🟢 SD reads on the cart run at 1710 KB/s and writes at 200 KB/s with a sync, measured on a real cart. [12]
- 🟢 `Read_SD_sectors` busy-waits on the FPGA's `0xEEE1` busy status, in bursts of at most 4 sectors. The CPU is blocked for the whole of each command. [13]
- 🔴 One `f_readdir` hitting a sector already in `fs->win` costs only CPU: walking the LFN entries, UTF-16 to UTF-8, and `get_fileinfo`. Estimate 3–10k cycles. A sector miss adds one single-sector command. At 1710 KB/s a sector takes at least 0.3 ms. Single-command overhead is unmeasured; estimate 0.5–1 ms, which is 8–17k cycles out of a 280,896-cycle frame.
- 🔴 With names of about 40 characters, each FAT entry takes 4 LFN slots plus the SFN (160 B), and each exFAT entry takes about 4 slots (128 B). 2,000 files are about 250–320 KB of directory data, or 500–650 sectors. Add one FAT sector per cluster boundary, plus `f_opendir` for 100 folders: each one resolves its path by scanning the parent up to the entry, a few sectors. That is 600–1,000 sectors, roughly **0.4–1 s of card time**, spread over 60–120 frames if a frame spends about 8 ms on it. The emulator doesn't give these timings, so time it on the cart with `cycles_now()` and log it.

---

### 2. Index format for 4,096 entries in PSRAM

> **Answer:** A versioned header with a CRC over the body, then fixed-size records, then a folder table and a string pool, then a sort order per console stored as u16 index arrays. Sort on a folded key with natural number order. Ignore a leading "The" in the key, so the A–Z order and the letter jump follow No-Intro's ", The" naming. Since the index can always be rebuilt, a bad CRC just triggers a rescan.
> **Confidence:** 🟡 medium. These are design conventions, and the sort conventions come from launcher sources.

**Findings**
- 🟢 Launchers keep sorting simple. TWiLight and MinUI use `strcasecmp`, so "Game 10" sorts before "Game 2". EmulationStation upper-cases names and can drop leading "a/an/the", off by default. It also has a per-game `sortname` override. None of them sort numbers naturally. [8][9][10b]
- 🟡 Sizes: records of 16–20 B × 4,096 = 64–80 KB. File names average about 40 B, so the pool is about 165 KB. The folder table stores each folder path once, which is about 5 KB for 100 folders. Sort arrays per console are 8 KB. In total, under 300 KB fits the 0–4 MB region many times over and loads in about 0.2 s at 1710 KB/s. Writing it takes about 1.3 s at 200 KB/s, so the write must be a job too, and only happen when the content changed. (Arithmetic from [12].)
- 🟢 PSRAM is hidden while SD mode is on, so FatFs can't read straight into it. Data goes through RAM, as `nameindex.c` and `sdread.h` already do. [13][14]

**Suggested layout** (little-endian, word-aligned)
- **Header, 64 B:** magic `TLIB`; format version u16 (any mismatch means rebuild); header size; record size; record count; a truncated flag (more than 4,096 games); counts per console; offsets and lengths of each section; the scan fingerprint (see 1: a hash of every folder's name, size and date tuples, plus the volume serial); CRC32 of everything after the header; CRC32 of the header itself.
- **Record, 16 B:** u16 folder index, u16 flags (console in 4 bits, has save, deleted), u32 file size, u32 file name offset into the pool, u32 sort key prefix: the first 4 folded bytes, so most comparisons never read the pool.
- **Folder table:** one pool offset per folder. A path is folder + "/" + name, which saves about 30 B per game against storing full paths.
- **Sort arrays:** for each console, u16 record numbers in display order. Rebuilding the order on every load would mean about 50k comparisons, around 0.5 s on the ARM7, so store it.
- **Display names** (B4's cleaned names and `names.txt`) stay in the names region by handle. The index stores only what the card says, so a rename in `names.txt` never invalidates it.

**Sort key**
- Fold: ASCII to lower case, and Latin-1 letters to their base letter (é→e, ß→ss), which `charset_next_latin1()` can already decode. Other UTF-8 characters keep their code point and sort after z.
- Ignore a leading "the ": B4 moves ", The" to the front for display, but No-Intro and GoodTools sort "Legend of Zelda, The" under L. Ignoring "the " keeps Zelda under L. This is the user's choice; flag it.
- Compare runs of digits by value ("Mega Man 2" before "Mega Man 10"), as Windows `StrCmpLogicalW` and glibc `strverscmp` do. Leave Roman numerals alone.
- Break ties on the raw name, then the path, so the order is total and reproducible for goldens.
- Letter jump: anything that starts with a digit or a symbol goes in a "#" bucket before A.

**Incremental updates**
- Delete: set the record's deleted flag, `memmove` it out of its console's sort array (at most 8 KB), and fix the count.
- Rename or add: append the new name to the pool (the old bytes become waste), then binary-search the insertion point in the sort array.
- Rewrite the file after a change, as a job. When waste passes about 25% of the pool, the next rewrite compacts it.

---

### 3. Safe file writes with FatFs

> **Answer:** The plan's "write .tmp, sync, delete old, rename" works, but `f_rename` can leave two names sharing one cluster chain if the power fails mid-rename. Recovering by deleting either name then frees the chain under the other, and on exFAT FatFs then refuses to touch the survivor at all (`FR_INT_ERR`). For the small text files, writing two copies with no renames or deletes is safer: write `NAME.bak`, then `NAME`, each validated by an end marker. `library.idx` is a rebuildable cache, so it needs no atomic replace: write it in place with a CRC, and rescan if the CRC fails.
> **Confidence:** 🟢 for FatFs behaviour (code and docs). 🟡 for the recommendation (inferred from the code; not tested on the cart).

**Findings**
- 🟢 `f_rename` fails with `FR_EXIST` when the new name exists, so replacing a file needs an `f_unlink` first. [1][15]
- 🟢 `f_rename` first registers the new entry (`dir_register`), then removes the old one (`dir_remove`), then calls `sync_fs` (ff.c 5178–5227). The new entry points at the same start cluster. If the two entries are in different directory sectors, `move_window` writes the new one before it loads the old one's sector. A power cut between those two writes leaves both names pointing at one chain, which is a cross link. If both entries are in the same sector, a single sector write carries both changes. [1]
- 🟢 `f_unlink` removes the entry, then frees the chain (`remove_chain`, ff.c 5032–5040). On exFAT, `change_bitmap` returns `FR_INT_ERR` when a bit is already clear (ff.c 1364), so after one cross-linked name is deleted, the other can be neither deleted nor overwritten (`FA_CREATE_ALWAYS` truncates through `remove_chain`). On FAT32, `remove_chain` stops at a free cluster (ff.c 1456), so overwriting the survivor happens to work there. [1]
- 🟢 elm-chan's app note: an interruption while a file is open for writing can cause a cross link or lost clusters, but "each case does not affect any file not opened in write mode". Keep the time a file is open for writing short. [16]
- 🟢 `f_sync` flushes the file's cached data and directory entry, as `f_close` does. [17]
- 🟢 Today `Recently_play_write` opens `/SAVER/Recently play.txt` with `FA_CREATE_ALWAYS` and rewrites it in place, so a power cut mid-write loses the list. B5 fixes a real hazard. [18]
- 🟡 Wear: a few KB rewritten per launch is negligible for an SD card's FTL wear levelling. The FAT and directory sectors get rewritten each time, but the card remaps them. (General knowledge; no SD Association source read.)

**Failure windows of the plan's sequence** (write `NAME.tmp`, sync and close, unlink `NAME`, rename)

| Cut during | Card holds | Boot recovery |
| --- | --- | --- |
| tmp write | `NAME` intact, `NAME.tmp` partial | delete tmp (safe: tmp's chain is its own) |
| unlink | `NAME` or not, `NAME.tmp` complete | if `NAME` is gone, rename tmp |
| rename, entries in two sectors | `NAME` and `NAME.tmp` cross-linked | deleting tmp corrupts `NAME`. Must detect it (`f_open` both and compare `fil.obj.sclust`), and on exFAT FatFs then has no clean way out |

Recovery has to tell a partial tmp from a complete one, so the files need an end marker or a CRC either way.

**Write-twice sequence (recommended for `favorites.txt`, `recent.txt`, `names.txt`)**
1. Write the new content to `NAME.bak` with `FA_CREATE_ALWAYS`, ending with an end line such as `# end 0x<crc32>`, then `f_close`.
2. Write the same content to `NAME` the same way.
3. At load, use `NAME` if its end line is valid. Otherwise use `NAME.bak` if valid, and rewrite `NAME` from it. Otherwise treat the list as empty and log `tlog_error`.

Only one file is ever open for writing, and the other copy is never touched while it is (the app note's guarantee). There are no renames or deletes, so no cross links. It costs one extra small write per change.

Hand edits on a PC: a `NAME` with no end line is taken as hand-edited when it is newer (FILINFO date and time) than `NAME.bak`, and is then rewritten with a marker. If the RTC is unset, timestamps can't be trusted, so let a marker-less `NAME` win whenever it parses. Rule to settle with the user: a missing marker means "edited by hand", not "corrupt".

**`library.idx`:** write it in place with `FA_CREATE_ALWAYS`, as chunks inside a job, then close. On load, a bad magic, version or CRC means rescan. No tmp file or rename is needed, because losing the cache costs only one scan.

---

### 4. Scanning in the background at 60 fps

> **Answer:** Run the walk as an engine job. Each step does one bounded unit of work: open a folder, or read up to N entries, and stop early once the step's cycle budget is spent. Copy results into PSRAM between FatFs calls. The VBlank IRQ is safe during SD commands because its whole path runs from IWRAM. The real limit is that each SD command blocks the CPU, so a step must stop at a sector-read boundary well before the frame deadline.
> **Confidence:** 🟡 medium. The constraints come from the code; per-call costs are estimates.

**Findings**
- 🟢 The engine already has the pieces. `jobs.h` runs state-machine steps in the frame's spare cycles, at least one per frame, and names "an f_readdir of a few entries" as a step. `sdread.h` keeps a FIL open between steps, stages PSRAM writes through RAM, and runs steps only in the job phase. So a step never overlaps the VBlank commit, another FatFs call or `tlog_flush`, and the kernel's ROM is mapped. [14]
- 🟢 SD mode hides the ROM, so nothing in ROM can run while a command is under way. `torii_irq` lives in `.iwram` and hands off to libgba's `IntrMain`, also IWRAM, and the engine only enables VBlank without a ROM handler. So IRQs during SD commands are safe as long as no ROM-resident IRQ handler is ever installed. Keep it that way, and add an assert. [13][19]
- 🟢 `FF_USE_LFN 1` uses one static LFN buffer, and `FF_FS_REENTRANT 0`. That is fine only because jobs run FatFs calls one at a time. Never call FatFs from an IRQ. [1]
- 🟡 Directory and FAT sectors share the single `fs->win` window. A walk interleaved with the cover loader's reads or a tlog flush reloads sectors it already had. Pause the scan job while covers load (`JOB_WAIT`), or accept the extra reads.
- 🔴 Step budget: about 140k cycles of a frame's 280,896 is roughly 8 sector misses plus decoding. Reading entries until `cycles_now()` passes the budget, then returning `JOB_MORE`, gives about 20–60 entries per frame. The walk then takes 1–2 s while the menu runs.

**Step design**
- Keep an explicit folder stack in EWRAM, with one `DIR` per depth (about 50 B each) and a depth cap of about 8. Skip hidden folders (B4's rule) before `f_opendir`.
- Per entry: match the extension against the console table. Add size and date to the folder's running hash. Append a record and the name to an EWRAM staging buffer, and flush it to PSRAM with `psram_write` once SD mode is off, after the step's last FatFs call.
- Save flags: walk `/SAVER` once and keep a hash set of the save base names. Don't `f_stat` once per game.
- At the end, compare the new fingerprint with the loaded index. If they are equal, discard the new data and write nothing. Otherwise swap in the new index, wake the frame, and start the write job.

---

## Recommendations for tickets B3 and B5

**Overrides to the plan**
1. **B3: drop "folder dates checked at boot".** 🟢 FatFs and Windows never update them, and the root has none. Replace it with **stale-while-revalidate**: at boot, load `library.idx` (about 0.2 s) and show the menu at once. Then always run the background walk as a job. Compare its fingerprint (a hash of name, size and date per folder, plus the volume serial) with the stored one, and rewrite the index only if they differ. The first scan with no index, or a bad CRC or version, shows progress through `dialog.h`, as planned. Optional fast path: if the volume serial differs (a new card), skip straight to the full scan with progress. The `tests/library.sh` case "a changed folder date that triggers a rescan" becomes "an added, removed or renamed game in the root and in a nested folder appears after boot without Rescan".
2. **B5: replace "write NAME.tmp, sync, delete the old file, rename" with write-twice** (`NAME.bak`, then `NAME`, each ending in a CRC end line). 🟢 `f_rename`'s register-then-remove can cross-link both names, and on exFAT a cross-linked survivor can't be repaired with FatFs. Boot recovery becomes "`NAME` valid, else `NAME.bak`". The power-cut test in `tests/userdata.sh` should cut during each of the two writes. If the user keeps tmp+rename instead, the recovery must compare start clusters before deleting anything.

**Format (B3's "report the file format")**
- `library.idx`: a 64 B header (magic `TLIB`, version, sizes, counts per console, truncated flag, fingerprint, CRC32 of the body and of the header), then 16 B records, a folder table, a string pool, and u16 sort arrays per console. Rebuild on any version or CRC mismatch. Write in place, as a job, only when the content changed.
- Sort key: folded Latin-1, natural number order, a leading "the " ignored (confirm with the user), ties by raw name and then path, and a "#" bucket for digits and symbols.

**Scan slicing**
- One job: a folder stack in EWRAM, steps bounded by `cycles_now()` (about half the frame), stopping at a sector boundary. Stage in EWRAM, then `psram_write` after the step's FatFs calls. One pass over `/SAVER` for the save flags. `JOB_WAIT` while the cover loader is busy.
- Instrument it: log `library: walked N entries in F folders, S sectors, T ms` with `tlog`, and record a figure from the real cart in `docs/research/omega-de-hardware.md`. The estimates above (0.4–1 s for 2,000 files) are unverified.

**Open questions**
- ❔ The cost of one single-sector SD command on the real cart, and so the true walk time.
- ❔ Whether macOS's msdosfs or exFAT driver updates folder dates (moot if folder dates are dropped).
- ❔ Whether Windows' exfat.sys updates folder dates. Also moot.
- ❔ Whether the sort should ignore a leading "The" and use natural number order. Both are the user's call.
- ❔ How Pico Launcher, nds-bootstrap and the EverDrive OS cache their lists. Not read; they are the closest examples of an on-card index on slow media.

---

**Sources**
1. [FatFs R0.15 ff.c, ffconf.h](/Users/erik.bavenstrand/Code/torii-kernel/source/ff15/ff.c) · R0.15 vendored · primary (code)
2. [Linux fs/fat/namei_vfat.c](https://raw.githubusercontent.com/torvalds/linux/master/fs/fat/namei_vfat.c) · master, fetched 2026-10-20 · primary
3. [Linux fs/exfat/namei.c](https://raw.githubusercontent.com/torvalds/linux/master/fs/exfat/namei.c) · master, fetched 2026-10-20 · primary
4. [Windows-driver-samples fastfat dirsup.c](https://github.com/microsoft/Windows-driver-samples/blob/main/filesys/fastfat/dirsup.c) · main · primary (sample, not the shipping driver; partial read)
5. [fontconfig src/fcdir.c comment](https://gitlab.freedesktop.org/fontconfig/fontconfig/blob/038aa930ae2c3b7972eefe334917e7222fe478ec/src/fcdir.c) · secondary (seen only as a search snippet)
6. Microsoft FAT specification (fatgen103): the root has no directory entry · primary (known; not re-fetched)
7. [freebsd-fs: msdosfs directory mtime](https://lists.FreeBSD.org/pipermail/freebsd-fs/2011-June/011646.html) · 2011-06 · forum
8. [TWiLight Menu++ fileBrowse.cpp](https://github.com/DS-Homebrew/TWiLightMenu/blob/master/romsel_dsimenutheme/arm9/source/fileBrowse.cpp) · HEAD 2026-10-20 · primary
9. [MinUI minui.c](https://github.com/shauninman/MinUI/blob/main/workspace/all/minui/minui.c) · HEAD 2026-10-20 · primary
10. [FatFs f_getfree and config docs](https://elm-chan.org/fsw/ff/doc/getfree.html) · primary
10b. [EmulationStation FileSorts.cpp, Settings.cpp](https://github.com/RetroPie/EmulationStation/blob/master/es-app/src/FileSorts.cpp) · HEAD 2026-10-20 · primary
11. [RetroArch manual_content_scan.c](https://github.com/libretro/RetroArch/blob/master/manual_content_scan.c) · HEAD 2026-10-20 · primary (grep only)
12. [Torii cart test 2026-10-18](/Users/erik.bavenstrand/Code/torii-kernel/docs/research/cart-test-2026-10-18.md) · 2026-10-18 · primary (measured)
13. [source/Ezcard_OP.c, omega-de-hardware.md](/Users/erik.bavenstrand/Code/torii-kernel/source/Ezcard_OP.c) · primary (code)
14. [source/engine/jobs.h, sdread.h](/Users/erik.bavenstrand/Code/torii-kernel/source/engine/jobs.h) · primary (code)
15. [FatFs f_rename](https://elm-chan.org/fsw/ff/doc/rename.html) · primary
16. [FatFs application note](https://elm-chan.org/fsw/ff/doc/appnote.html) · primary
17. [FatFs f_sync](https://elm-chan.org/fsw/ff/doc/sync.html) · primary
18. [source/ezkernelnew.c Recently_play_write](/Users/erik.bavenstrand/Code/torii-kernel/source/ezkernelnew.c) · primary (code)
19. [source/crash.s, source/engine/frame.c](/Users/erik.bavenstrand/Code/torii-kernel/source/crash.s) · primary (code)

*deep · 2 rounds · 19 sources read · 3 sonnet researchers + own reading of the code*
