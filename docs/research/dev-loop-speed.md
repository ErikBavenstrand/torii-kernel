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

## Update 2026-10-19: `just test` back to about 20 s

By 2026-10-19 a warm `just test` took 174–194 s (`test-host` alone 18.5 s). Measured the same day, warm tree, `torii` VM with 8 CPUs,
other sessions testing in the same VM (load 5–7): 193.5 s before the changes below, 19–26 s after (four runs), all 272 PASS lines kept.

- **Carts off the virtiofs mount** (most of it). The emulator memory-maps the cart files and syncs them at the end of each power-on.
  On `dist/` that costs about 0.5 s a power-on, mostly the 64 MB game NOR; on the container's own disk 0.03 s for the same 600 frames.
  `tests/nor_safety.sh` (63 power-ons) went from 119 s to 6.2 s alone, `tests/loader_safety.sh` from 38 s to 8.8 s, `tests/nor.sh` from 48 s to 1.9 s.
  The test scripts now call `local_carts` (`tests/carts.sh`), which puts their carts in the container's `/tmp` and copies them to the old `dist/cart-*` folder only when the script fails;
  `tests/shots.py` keeps its carts there too. This corrects "mgba-shot runs no faster from tmpfs" under Compat below: it holds for long game runs, but each power-on still pays the sync.
  `nor_safety.sh` also stopped writing a 64 MB 0xFF game NOR per case (1.3 s each through `tr`); the emulator fills a new or short file with 0xFF itself.
- **Shards.** `tests/all.sh` runs `nor_safety.sh` as 3 jobs and `loader_safety.sh` as 4 (`N/M` argument: every Mth case from the Nth), 2–2.7 s each.
- **Host tests.** `tools/hostcc.py` compiles each file to its own object in parallel, cached in `dist/host/obj` by a hash of its preprocessed text (not times, which virtiofs can show late).
  The theme checker (removed in v0.4 with the SD themes) shared the objects. Its 19 runs in `tests/themecheck_test.py` took 0.55 s each, nearly all LeakSanitizer's check at exit; they ran 8 at a time.
  `just test-host`: 18.5 s to 3.7 s warm.

What remains: about 4 s of `test-host` and docker starts, then `tests/all.sh` (about 15 s), whose slowest jobs are `tests/shots.py` (7 s alone) and `saves.sh full` (5 s).
The run is CPU-bound: run one after another, the jobs took about 75 s.

## Update 2026-10-19: compat carts off the virtiofs mount

`tests/games.py` now keeps its carts in the container's `/tmp` too and copies a failed game's carts to `dist/cart-games`;
reports and screenshots still go to `dist/compat`. Measured old and new in turns on a warm tree, `torii` VM with 8 CPUs, each pair started with no other container running (load 2–9):
`just compat` 15.7–19.4 s before, 11.5–14.0 s after (three pairs); `just compat --all` 71 s before, 57 s after (one pair).
The gain is smaller than for `just test` because game power-ons are long (2,400+ frames) and the run stays CPU-bound.
A passing run also no longer leaves 140 MB per game in `dist/cart-games` (16 GB after `--all`).

## Update 2026-10-28: `just check` from about 8 to under 4 minutes, and instant when re-run

By 2026-10-27, `just check` for a one-line kernel C change ran lint, test, size, analyze and compat one after another in about 8 minutes.
Measured in a worktree on a warm tree, `torii` VM with 8 CPUs, with another session testing in the same VM most of the time (load 4–11), so times carry about ±20 % noise.

| Tier | Before, one-line edit to `source/screens/games.c` | After, alone (quiet VM) | After, inside `just check` |
|---|---|---|---|
| lint | 18 s | – | 28–31 s, on the Mac beside the rest |
| test | 246 s | 96–101 s (`tests/all.sh`) | 136–143 s |
| size | 1 s | – | 2–4 s |
| analyze | 89 s | 28.6 s | 69–108 s, beside test |
| compat | 142 s | 87–94 s | 80 s |
| **`just check`** | **496 s** (sum) | | see below |

`just check` after the changes:

| Change | Tiers | Before | After |
|---|---|---|---|
| one line in `source/actions.c` (launch path) | lint, test, size, analyze, compat | 496 s | 222 s |
| one line in `source/screens/games.c` (menu page) | lint, test, size, analyze (compat no longer) | 496 s | 150 s |
| the same, run again (after a commit, or a docs-only edit) | none: all passed with the same inputs | 496 s | 0.5 s |

Where the time went, and what changed:

- **`tests/shots.py`'s `sd-many` scenarios** (most of `test`). They put 1,025 and 3,000 files in one folder with `mcopy`, which checks every new name against the whole folder:
  one `mcopy` ran over 2 minutes, and `test` waited on it. They now go through `tools/sdimg` (the kernel's FatFs) in one mount, whose `put` takes many file pairs:
  both scenarios take 7.5 s with the sdimg build, and their golden hashes are unchanged.
- **`tests/all.sh` is CPU-bound.** Its 140 jobs take about 540 CPU-seconds (measured per job with bash's `time`), so on 8 CPUs it can't go much below 70 s; it takes 96–101 s.
  The largest: `tests/shots.py` 41 s, `saveorphan.sh trash-split empty-split` 34 s, `saves.sh rename-walk` 27 s, `saveorphan.sh relink-split ...` 23 s, `cheats.sh` 22 s, `cheat_engine.sh` 15 s per shard (4), `state_trips.sh hello` 18 s, `states.sh cut` 17 s, `ppu_render.sh` 15 s.
  The power-cut sweeps (`cut_sweep`: saveorphan, saves rename-cut, slotstore splits, states cut) take about 120 of the 540: each cutpoint boots and replays its scenario from frame 0. Starting each from a save state taken before the operation could cut most of that, but the cart's SD and NOR files sit outside mGBA's state, so that needs work of its own.
- **`tests/cheat_codes.sh` ran its 13 games one after another** (about 60 s). Each game now runs on carts of its own, side by side: 22 s alone. `just compat` runs it beside `tests/games.py`.
- **`tests/games.py` ended on a straggler.** After 63 s only a few games were left and the last ended at 115 s. It now records each game's seconds in the run file and starts the longest first, by the last 20 runs: 87 s with the cheat codes beside it.
- **clang-tidy was one process over every file**, about 75 s on one core. It now runs 4 files per process, one per Mac CPU (15 s), and drops a finding (file, line, column, message) repeated in another process's output, as one process would.
  The findings matched the single process's, except that `gamename.c`'s `memcpy` finding, which appears every time the file is analysed alone, was reported by the single process in only one of two runs. It is a false alarm ("the caller ends it") and is now in the baseline.
- **`just test-host` ran its 10 Python test files one after another.** They now run at once beside `tests/host.sh`. `tests/art_scrape_test.py` took 12 s, nearly all `HTTPServer.shutdown()` waiting for the 0.5 s poll: its fixture server polls every 10 ms, 3.5 s.
- **`just check` runs the tiers side by side**: lint at once on the Mac; one `just` call then builds what the others need, so none builds the same thing at the same time; size and analyze start, and test, compat and repro run one after another.
  Run all at once, test and compat took 242 s and 276 s instead of about 100 s each: about 160 emulator processes on 8 CPUs and 8 GB.
- **Pass cache.** A tier that passed is recorded in `dist/.check/passed.json` under a hash of its inputs: every file whose rules (`tools/check.py`) select the tier (both test and test-host's for test, every file for repro), unsaved edits in `emu/mgba` and the emulators' checkouts, and for compat the names, sizes and times of `roms/`. Files are hashed from git's index, and from disk only where the work tree differs (0.1 s for the repo).
  A tier is recorded only if its inputs still hash the same after it ran. `--no-cache` runs everything.
  So the second `just check` the workflow asks for, after committing a change and again after its CHANGELOG entry, runs only lint again (about 30 s): the CHANGELOG is an input of lint alone.
- **`NO_COMPAT` grew by the menu pages a player opens**: Games, SD card, NOR, cheats, saves, settings, Diagnostics' drawing and the carousel's affine maths.
  `tests/games.py` launches through the tests' hook in `screens/nav.c`, straight into `actions.c`, so their code never runs in compat; their only callers are other menu pages and `nav.c`'s page table, whose hooks run only for an open page.
  Pages a launch can show itself (slot picker, slot flow, save page) and headers that `nav.c` or `actions.c` include stay out.

Not done:

- More CPUs for the VM. The Mac has 14 cores and `torii` takes 8; test and compat are CPU-bound, so 12 would cut them by up to a third, at the cost of the Mac's and other sessions' headroom.
- Running compat's games and `tests/all.sh` from save states. Ruled out before for compat (boot is under 15 % of a game's frames), and the cut sweeps above need the cart files snapshotted too.

## Update 2026-10-28, later: run only what a change touches

After the first update, `just check` still took 2.5–4 minutes for a one-line kernel change: `tests/all.sh` alone is about 540 CPU-seconds on the VM's 8 CPUs.
Running all of it every time can't get much below 2 minutes, so `just check` now runs only the tests a change can affect, and `just check --full` (before merge) runs everything.

| One-line edit to `source/screens/info.c` | Before | After |
|---|---|---|
| test | 136–194 s (every job) | 14 s (6 of 221 jobs) |
| analyze | 42–77 s | 5–7 s (that file again, the rest cached) |
| lint | 25 s | 4–7 s |
| size | 1 s | 1 s |
| **`just check`** (no tools/ change, so no repro) | about 150 s | about 20 s |

Measured with another session's tests in the same VM, so the times vary by about ±20 %. A change on the launch path still runs compat (about 80 s).

- **Test selection** (`tools/testmap.py`, `TestJobs` in `tools/check.py`). Every test run records the kernel code each job executes: mgba-shot marks each halfword the CPU executes (TORII_COVERAGE, a hook in mGBA's CPU loop that costs nothing when off), and `tests/all.sh` keeps each job's marks apart.
  Each golden scenario is a job of its own.
  A job runs again only when a function it executed changed, when its script or the files every job reads (the harness: test helpers, test ROMs, the emulator, tools) changed, or when it never passed.
  A function's fingerprint comes from the object files: its bytes, with each relocated word replaced by what it points to (a function's name; a variable's name and, for constant data, its content, recursively). So moving code changes nothing, while a change to a header, an inlined function, a string or a table changes every function that uses it.
  A one-line change in a function changed the fingerprints of only that function and the one it was inlined into. Over the 2,592 functions, the median function is run by 9 jobs; the boot path (about a quarter of them) is run by nearly all, so changes there still run nearly everything.
  What selection can't see is layout: data that moves can break code that didn't change (an overflow that now hits something else). `just check --full` runs every job for that, before merge.
- **Analyze caches per file.** GCC's `-fanalyzer` and clang-tidy outputs are kept in `dist/analyze/cache` under a hash of the file, every header its `.d` file lists, its command and the analysis's configuration; cppcheck uses its own `--cppcheck-build-dir`, and runs with `-j`. Warm: 31 s to 5 s. A planted null dereference was still reported.
- **shellcheck ran on one core** (17 s of lint's 18 s). It now runs one file per process with `-x` (following sourced files, as one process did when given them all): lint takes 4 s.
- **mgba-shot draws only the frames it shows.** mGBA's renderer now runs only for frames a `-s`, `-h` or `-H` reads, and the frame before each (`-R` draws every one). A homebrew GBA ROM ran 39 % faster (20,000 frames: 5.2 to 3.2 s); `tests/all.sh` used about 7 % less user CPU, as the menu's frames are cheap. Every golden hash still matches.
- Not done: compat could be selected the same way (record which functions its games run, and skip it when none changed), in place of the hand-kept `NO_COMPAT` list.
  Carts on tmpfs failed: 140 jobs' 64 MB NOR files filled it.

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
  mgba-shot also runs faster off virtiofs: it syncs the memory-mapped cart files at the end of each power-on, about 0.5 s on `dist/` (see the 2026-10-19 updates; carts are now in the container's `/tmp`).
- Fixed costs: ROM index 0.01 s (cached), report 0.22 s.
- Disk: each game kept a 140 MB cart in `dist/cart-games` (SD image, 64 MB game NOR, kernel NOR, SRAM).
  After `--all` that was 16 GB per worktree; since 2026-10-19 only failed games' carts are kept there.

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
| Running `test-host` alongside `tests/all.sh` | saves 1.3 s |

## Found while measuring

- First `just test` in a new worktree failed: parallel tests copied FatFs into `dist/sdimg` at the same time
  (`cp: cannot create regular file 'dist/sdimg/ff.h': File exists`, `tools/sdimg.sh` line 11).
  Commit 04820c8 made the binary's rename atomic, but the copies before it still race, and so does the `sed` that writes `ffconf.h`.
  A parallel build can therefore compile a half-written header. Building in a `mktemp -d` folder and renaming the binary into place fixes both. Later runs passed.
- `colima start -p <profile>` switches the default Docker context to that profile. Only `just _vm` resets it, to `colima`.
