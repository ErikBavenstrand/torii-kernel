# Dev-loop speed: where the time goes

Measured on 2026-10-17 on an Apple M4 Pro (10 performance + 4 efficiency cores, 24 GB), at commit 04820c8.
Containers ran in the colima VM `torii` (4 CPUs, 6 GB, vz, virtiofs, Rosetta for amd64).
Two other sessions were building and testing in the same VM at the time (host load average about 5),
so the local numbers carry about ±20 % noise. Run-to-run spread is given where it was measured.
A review agent re-ran the main measurements a few minutes later: warm `just build` 0.6–0.7 s, `just test` 6.6–6.9 s,
`just compat` 22.4 s, `just analyze` 31.3 s, slowest test script 4.4 s. All are within 10 % of the numbers below, and CI matched the API.

## Summary

The build and test tools are already fast. The slow part of the loop is the agents' process around them.

- Warm `just build` takes 0.6 s and `just test` 7 s. After a one-file edit, `just test` takes 11–17 s.
  `just compat` takes 20 s and `just compat --all` 110 s.
- In the v0.2 sessions, about 50 % of active time went to the model generating text.
  About 20 % went to waiting for review agents, and 15–30 % to tools.
  All build, test, compat and analyze runs together took about 1.5 h, against 5 h of generation.
- Review agents spent 78 % of their own time generating. Faster tools barely shorten them;
  fewer and narrower reviews do.
- CI takes about 1 min 50 s per push. Most of that is the `torii-emu` image (about 38 s), built again in each of 3 jobs,
  and the mGBA build (34 s). No CI step is cached.
- The `torii` VM uses 4 of the Mac's 10 performance cores. `just compat` is CPU-bound:
  in a separate VM with 10 CPUs and no other sessions' load, it took 11 s instead of 20 s, and `--all` took 56 s instead of 110 s.

## Top five, ranked by time saved × frequency ÷ effort

| # | Change | Saves | How often | Effort | Risk |
|---|---|---|---|---|---|
| 1 | Tiered verification and a lighter review policy | 15–30 min per feature session (estimated from the transcripts) | every session | a CLAUDE.md section and the brief template | fewer bugs caught if risky areas lose their adversarial pass |
| 2 | Give the `torii` VM 8 CPUs and 8 GB | at most compat 20 → 11 s, `--all` 110 → 56 s (upper bound, measured at 10 CPUs); less contention between sessions | every compat run (90 in v0.2) and every run while sessions overlap | 5 min | less CPU left for macOS and the agents while compat runs |
| 3 | Build the `torii-emu` image only where CI needs it, then cache it and the emulator | about 60–70 s off each push's wall clock, about 150 runner-seconds per run (estimate) | about 8 pushes a day | 1 h for the first half, 2–3 h for both | cache keys and `dist/.fresh` records must match, or `just` rebuilds anyway |
| 4 | Run `just analyze`'s three tools at once, and its GCC step in parallel | 29 → about 10–15 s | every C change (20 runs in v0.2) | 1 h | interleaved output would mangle findings; per-file clang-tidy repeats header findings |
| 5 | Skip tiers by changed files (`just check`) | skips compat for docs, host-test and listed UI changes: 20–110 s per check | every pre-commit check | 1–2 h | a wrong list skips a needed test; full runs before merge cover that |

### 1. Tiered verification and a lighter review policy

What the sessions did (from their transcripts; durations exclude idle gaps longer than 30 min):

| Session | Generating | Tools | Waiting for agents | Review rounds (agents) |
|---|---|---|---|---|
| Foundations: bug fixes | 28 min | 4 min | 7 min | 1 (4) |
| Tooling | 29 min | 35 min | 13 min | 1 (3) |
| Cart model and hooks | 51 min | 19 min | 8 min | 2 (4) |
| Source cleanup | 35 min | 14 min | 25 min | 5 (7) |

- The 20 review agents took 2.6 h in total, 78 % of it generating. Adversarial testers ran longest:
  13–18 min each, writing their own harnesses under `dist/` and running raw `docker run` (48 min of `docker` calls in all).
  Code reviewers took 3–10 min each.
- Each round adds the longest agent's time plus a fix-and-reverify pass. Source cleanup ran five rounds,
  and two of them re-checked fixes from the round before.
- Across v0.2 the sessions ran `just compat` 66 times and `just compat --all` 24 times, `just test` 59 times,
  and `just build`/`size`/`stack` 94 times.
  In 17 of the 58 compat runs kept in `dist/compat/runs` (main checkout and two worktrees), the kernel and filters
  were the same as in an earlier run. Some of those followed changes to the cart model or `tests/games.py`, which the kernel hash doesn't cover.
- Signed commits wait on 1Password: 16 of 150 `git commit` calls took over 10 s, two failed after 60–80 s with
  "1Password: failed to fill whole buffer" while nobody approved them.

Proposed policy (for CLAUDE.md and session briefs):

| Tier | When | Runs | Time now (4 CPUs) |
|---|---|---|---|
| 0, inner loop | after each edit | `just build`, or `just test-kernel` / `test-host` for the code touched | 1–5 s |
| 1, before each commit | always | `just lint`, `just test`; `just analyze` if C changed | 7–17 s, plus 29 s |
| 2, before handing back | launch, saves, patching or the cart model changed | `just compat` (default set) | 20 s |
| 3, before merge | once per branch, only if tier 2 applied | `just compat --all` | 110 s |

Review rules:
- One round of at most two agents: a code reviewer, plus an adversarial tester only for launch, saves, the cart model or patching.
  Pass them the test and compat results (`dist/compat/runs/*.json`), so they don't rebuild or re-run them.
- Re-check fixes in the same session instead of a second agent round. Spawn another round only for a fix that changed behaviour.
- Use a smaller model for layout-only or docs-only reviews (the reformat review needed 2.6 min).
- Give each tester a time budget ("stop after 10 minutes and report").

Expected saving: 15–30 min per feature session, an estimate from the transcripts (the shortest round, at most two agents, against the rounds the sessions ran). First step: add a "Verification tiers" section to CLAUDE.md
and use it in the next session brief.

### 2. Give the `torii` VM 8 CPUs and 8 GB

`_vm` in the justfile starts the VM with `--cpu 4`. Games run in parallel with one worker per CPU
(`ThreadPoolExecutor(max_workers=os.cpu_count())` in `tests/games.py`), and each game is CPU-bound in mGBA:

| Run | 4 CPUs (`torii`) | 10 CPUs (temporary `torii-bench` VM) |
|---|---|---|
| `just compat` (27 games) | 20.0–21.6 s | 11.4 s |
| `just compat --all gba` (122) | 85.4 s | – |
| `just compat --all` (196) | 110.3 s | 55.7 s |
| `just test` | 6.8–7.0 s | 6.1–7.2 s |

Per game, the emulator runs about 3,600 frames per second (60× real time), and a GBA game takes about 7,000 frames over 4 power-ons.
With 10 workers, `--all` keeps about 7.8 games running at a time.
The 10-CPU runs used a second VM, while the other sessions' load stayed in `torii`.
So the saving combines more CPUs with less contention, and is an upper bound.
Three sessions share `torii`, so more CPUs also shorten their tests while one of them runs `--all`.
10 vCPUs would leave no performance core for macOS, the default colima VM and the agents.
Hence 8, with 8 GB for 8 or more mGBA processes at once.

First step: change `--cpu 4 --memory 6` to `--cpu 8 --memory 8` in `_vm`.
Then run `colima stop -p torii && colima start -p torii --cpu 8 --memory 8` when no session is using it (colima keeps a running VM's old settings).
Optionally, let an environment variable cap `tests/games.py`'s worker count.
Next, sort the games longest first, so the default set doesn't end with a straggler.

### 3. Build the image only where CI needs it, then cache it and the emulator

From the jobs of run 39048565378 (16:14 on main). Job totals come from the API, the split from the job logs' timestamps:

| Job | Total | Of which |
|---|---|---|
| Test on the emulated cart | 104 s | `torii-emu` image 38 s, devkitARM pull 6 s, kernel 4 s, mGBA fetch and build 36 s, test ROMs and test kernel 3 s, tests 8 s |
| Static analysis | 98 s | image 38 s, devkitARM pull 6 s, analyze image 10 s, cppcheck 6 s, clang-tidy 12 s, `-fanalyzer` 15 s |
| Build kernel | 62 s | image 40 s, devkitARM pull 6 s, kernel 4 s |
| Lint | 18 s | |

- Three jobs build the same `torii-emu` image (about 115 runner-seconds) and pull devkitARM (18 s).
  Nothing is cached apart from uv.
- A version tag runs Release's full `just test` on the commit CI is already testing.

Two of the three image builds can go without any cache.
`just build` needs `torii-emu` only to run `tools/omega_image.py` and `tools/nm_to_sym.py`, which need nothing but Python.
`just analyze` needs it only for `tools/compile_commands.py`.
Run those with the host's `python3`, as `fresh.py` already is, and only the test job builds the image.
That takes about 40 s off the build job and about 38 s of runner time off analysis.

With the image cached (`docker/build-push-action` with `cache-from: type=gha`, or pushed to GHCR keyed on the Dockerfile hash)
and `emu/build-linux` cached with `actions/cache` (keyed on `emu/fetch.sh`, which pins the mGBA tag, `emu/patches/*`, `emu/mgba-shot.c`, `emu/build.sh` and the Dockerfile),
the test job should take about 30 s and the build job about 15 s. Analysis is then the longest job (about 55 s, or about 35 s with #4).
The workflow should finish in about 1 min instead of 1 min 50 s, an estimate to confirm by measuring.
The cache must also carry `dist/.fresh`, or the steps must record what they restored.
`fresh.py check` reads its records from there: with `emu/build-linux` restored but no record, `just` rebuilds mGBA.
Without `dist/.fresh/emu-image`, `just` runs `docker build` over a loaded image.
First step: move the three Python steps to the host, then compare the build job's time.

### 4. Speed up `just analyze`

Warm, 28.6–29.1 s: GCC's `-fanalyzer` 17.7 s, clang-tidy about 6 s, cppcheck 3.8 s, `compile-commands` 0.9 s.
`tools/analyze.py gcc` writes 18 compiler calls to one shell script, run one after another under Rosetta,
and `xargs uvx clang-tidy` runs one clang-tidy over all 17 files.
clang-tidy runs on the Mac through `uvx`, not in the VM.
Per file, `-fanalyzer` takes up to 5.2 s (`setwindow.c`) and 4.1 s (`ezkernelnew.c`), so parallel GCC bottoms out at about 6 s.
Parallel GCC alone brings the run to about 13–15 s. Running cppcheck, clang-tidy and GCC at the same time as well brings it to about 10 s.
Two traps:
- Parallel GCC runs would interleave their messages in `gcc.txt` and break finding lines.
  Give each file its own output, concatenate them, and add the end marker only when every run succeeded.
- Per-file clang-tidy repeats a header's findings once per file, and the count-based baseline would read the repeats as new.
  Keep one clang-tidy process, or drop duplicates before the check.

First step: make `analyze.py gcc` write one output file per source file, and run the calls with `xargs -P"$(nproc)"`.

### 5. Skip tiers by changed files

A `just check` recipe that reads `git diff --name-only main...` and the working tree.
The rules are inverted: compat is the default, and only listed paths skip it.
Every source file links into one binary, and EWRAM has about 400 bytes left (past its end, data wraps onto its start).
So any change to `source/` can break launching, and a list of launch-related files would miss some.
- always: `lint` and `test`;
- C files: `analyze`;
- `source/`, `Makefile`, `image.bin`, `emu/`, `tools/`, `tests/games*`, `tests/hook_watch.lua`: also `compat`;
- docs only (`*.md`, `docs/`): lint alone;
- `tests/host/`, `tests/kernel/` and an explicit list of menu-only files: `test` without compat.

`just compat` already tests one game per set of requirements, and the kernel is shared by every game,
so selecting games by changed file buys little: select whole tiers instead.
First step: write the rules in CLAUDE.md, then turn them into the recipe.

## Measurements

Scripts are in `dist/timing/` of this worktree (not committed) and copied to the shared notes folder
(`dev-loop-speed-scripts/`). `t.sh LABEL CMD...` times a command and logs every `docker` and `python3` call it makes,
through shims put first on PATH.

### Build

| Case | Wall |
|---|---|
| `just build`, no objects (image present) | 5.5 s; compile 3.8 s |
| `just build`, nothing changed | 0.6 s (3 runs) |
| `just build` after editing `source/draw.c` | 2.8 s: compile 1.15 s, 4 other containers about 0.65 s, `fresh.py sync` 0.21 s |
| Full kernel compile in the container, `-j8` / `-j1` | 4.0–4.5 s / 12.0 s |

- `docker run --rm` costs 0.13–0.15 s per container, amd64 or arm64. A build starts up to 5, a warm `just test` 3.
- `tools/fresh.py` takes 30–40 ms per call. `sync` takes 0.05 s when nothing changed and 0.2 s after an edit.
  `check emu-headless` takes 0.1–0.2 s, as it hashes all of `emu/mgba/src`.
- First use on a new machine or VM: VM start 22 s, `torii-emu` image 25 s, devkitARM pull 14 s (1.3 GB).
- Per new worktree: `emu/fetch.sh` (clone and patch) 2.2 s plus the mGBA build 20 s, 23 s in all; afterwards 0.8 s.
  It doesn't rebuild without cause. If the patches change, it fetches again and rebuilds all of mGBA.

### Tests

| Case | Wall |
|---|---|
| `just test`, nothing changed | 6.8–7.0 s: `test-host` 1.3 s, then `tests/all.sh` 4.3 s, about 0.7 s of `just` and `fresh.py` |
| `just test` after editing one kernel source | 11.4 and 17.4 s (2 runs); builds both the kernel and the test kernel (about 1.1 s each) |
| `just test` after editing one unit test | 11.5 s |
| `just test` in a new worktree | 15.5 s, then failed: see "Found while measuring" |
| `just test-host` | 1.8–1.9 s |
| `just lint` / `size` / `stack` | 2.9–3.4 s / 0.8 s / 0.6 s |

Per test script, all 18 running at once inside one container on 4 CPUs, so each time depends on contention.
The review's re-run differed by up to 60 % per script but 10 % on the slowest (`menu.sh`, 4.4 s), which sets the total:

| Script | s | Script | s |
|---|---|---|---|
| menu.sh | 4.03 | clean_boot.sh | 1.92 |
| stale_recent.sh | 3.09 | cheats.sh | 1.72 |
| fragmented.sh | 2.92 | save_types eeprom512 | 1.69 |
| names.sh fat32 | 2.36 | save_types flash64 | 1.64 |
| save_types eeprom8k | 2.28 | hard_reset.sh | 1.48 |
| listing.sh | 2.27 | settings.sh | 1.47 |
| names.sh exfat | 2.25 | kernel.sh | 0.06 |
| save_types flash128 | 2.12 | crash.sh | 0.05 |
| smoke.sh | 2.07 | state.sh | 2.01 |

mgba-shot already runs unthrottled (it sleeps only with `-t`) and stops early on `-u`,
so "faster than real time" is already the case: the kernel unit tests take 60 ms.

### Compat

- Per game, measured serially for 8 games (`compat_profile.py`): mgba-shot 82 % (55,180 frames in 15.4 s, about 3,600 frames per second),
  `make_cart` 14 % (0.17–0.3 s per game: `mkfs.fat` and `mcopy` of a 64–80 MB image), `menu_cart` 7 %. Screenshots, PNG checks and mtools reads take under 1 %.
- A GBA game takes 1.7–2.8 s, a GB game about 1.1 s. The longest, at 3.5–5.4 s, sets the floor for any run.
- `make_cart` is 10× faster on tmpfs (0.024 s against 0.24 s per cart), but at most about 8 % of a compat run.
  mgba-shot itself runs no faster from tmpfs: the cart files are memory-mapped.
- Fixed costs: ROM index 0.01 s (cached), report 0.22 s.
- Disk: each game keeps a 140 MB cart in `dist/cart-games` (SD image, 64 MB game NOR, kernel NOR, SRAM).
  After `--all` that is 16 GB per worktree; the main checkout holds 13 GB and source-cleanup 11 GB.

### CI

Runs on main, 2026-10-17: 1 min 26 s to 2 min 47 s per run. The test job (95–130 s) usually finishes last.
The analysis job (98 s) is advisory but adds to the wall clock. Job breakdown under #3.

## Rejected or low value

| Candidate | Why not |
|---|---|
| One long-lived container per recipe | `docker run` costs 0.14 s, and a recipe starts 3–5: under 1 s per command |
| ccache | a full kernel compile is 4 s and an incremental one 1.1 s |
| Native arm64 devkitARM | the pinned r54 image is amd64-only, newer GCC rejects the source, and the compile is 4 s |
| Save states to skip the kernel's boot | about 300 of 7,000 frames per GBA power-on (under 15 %). The cart's NOR and SD files sit outside mGBA's save state, so the saving would need cart-model work |
| Fewer screenshots | 3 per game; PNG writes and checks are under 1 % |
| Caching the compat baseline, incremental compat | every kernel change changes all games' inputs. Only a re-run with the same kernel, emulator, `tests/games*` and ROMs could be skipped: the tier policy (#1) avoids most of those without a cache |
| Sharing the emulator build between worktrees | 23 s once per worktree |
| Carts on tmpfs | up to 8 % of compat. Worth it later, mainly for the 16 GB of disk; deleting a passing game's cart does the same |
| Running `test-host` alongside `tests/all.sh` | saves 1.3 s |

## Found while measuring

- First `just test` in a new worktree failed: parallel tests copied FatFs into `dist/sdimg` at the same time
  (`cp: cannot create regular file 'dist/sdimg/ff.h': File exists`, `tools/sdimg.sh` line 11).
  Commit 04820c8 made the binary's rename atomic, but the copies before it still race, and so does the `sed` that writes `ffconf.h`.
  A parallel build can therefore compile a half-written header. Building in a `mktemp -d` folder and renaming the binary into place fixes both. Later runs passed.
- `colima start -p <profile>` switches the default Docker context to that profile. Only `just _vm` resets it, to `colima`.
