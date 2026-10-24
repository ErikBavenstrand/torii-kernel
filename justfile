# Torii build commands. Needs Docker and just; `play` and `dev` also need
# CMake, pkg-config and SDL2 to build the patched mGBA natively.
#
# The kernel source is forked from the official 1.06 release. It is built with
# devkitARM r54 (GCC 10.1), the closest release to the r53 EZ-Flash used that
# compiles it without changes, pinned by digest. It runs under amd64 emulation.

# On macOS, containers run in a colima VM of their own, "torii", which shares
# the repo over virtiofs (colima's default sshfs shows edits up to 20 s late).
export DOCKER_CONTEXT := if os() == "macos" { "colima-torii" } else { env("DOCKER_CONTEXT", "default") }

# Run before every build in a container, see tools/fresh.py
fresh := "python3 tools/fresh.py"
# The image fresh.py's sync checks the share with
export FRESH_IMAGE := kernel_image
# What each build reads; a step is skipped while these are unchanged
kernel_inputs := "source tests/kernel Makefile emulators/out/jagoombacolor.gba emulators/out/pocketnes.gba image.bin tools/omega_image.py tools/nm_to_sym.py tools/build_id.py " + kernel_image
test_kernel_inputs := "source tests/kernel Makefile emulators/out/jagoombacolor.gba emulators/out/pocketnes.gba tools/build_id.py " + kernel_image
emu_inputs := "emu/mgba/src emu/mgba/include emu/mgba/CMakeLists.txt emu/mgba/cmake emu/mgba-shot.c emu/build.sh docker/emu.Dockerfile"
test_rom_outputs := "tests/roms/hello/hello.gba tests/roms/crash/crash.gba tests/roms/crash/crash.sym tests/roms/savetest/flash128.gba tests/roms/savetest/eeprom512.gba tests/roms/idle/idle.gba tests/roms/eeprom/eeprom.gba tests/roms/rumble/rumble.gba"
kernel_image := "devkitpro/devkitarm@sha256:1acab59884288115af0e70782a9b69017a8cc6bad69542b24e8652233f215c56"
# The built-in emulators each build with the devkitARM of their own source's
# time (emulators/fetch.sh pins the source): r58 (GCC 12, 2022-05-31) for
# Jagoomba Color v0.5, which newer GCC rejects, and r66 (GCC 15, 2025-07-28)
# for PocketNES 2025, which needs GCC 14's flags
jagoombacolor_image := "devkitpro/devkitarm@sha256:7dff610fb307a6c258a12e68aa38b365e9a1598300be32163db31645c64ccbfc"
pocketnes_image := "devkitpro/devkitarm@sha256:a8a5fbf5336ba400fd960d20c4c4c370d9a4c56abfc0a4348e40ba9c0071fb5c"
emu_image := "torii-emu"
kernel_docker := "docker run --rm --platform linux/amd64 -u $(id -u):$(id -g) -v \"$PWD\":/src -w /src -e LIBGBA=/opt/devkitpro/libgba " + kernel_image
# LC_ALL: mtools stores UTF-8 file names correctly only in a UTF-8 locale
emu_docker := "docker run --rm -u $(id -u):$(id -g) -e LC_ALL=C.UTF-8 -v \"$PWD\":/src " + emu_image
# C formatter for `just lint` and `just format`; .clang-format-ignore lists the files it skips
clang_format := "23.1.2"
# Advisory analysis for `just analyze` (cppcheck is pinned in docker/analyze.Dockerfile)
clang_tidy := "22.1.8"
# The SDL emulator, printing the kernel's tlog lines ("GBA Debug: ...")
mgba := "emu/build-native/sdl/mgba -C logLevel.gba.debug=31"
# Emulator window size as a multiple of 240x160, e.g. `just scale=3 play`
scale := "4"
cart := "dist/cart"
# How a container reaches mGBA's GDB stub, which listens on the host's loopback
gdb_net := if os() == "linux" { "--network host" } else { "--add-host=host.docker.internal:host-gateway" }
gdb_host := if os() == "linux" { "127.0.0.1" } else { "host.docker.internal" }

# Build the flashable upgrade file: dist/ezkernelnew.bin
build: _ready emulators
    @mkdir -p dist
    @{{fresh}} check kernel dist/ezkernelnew.bin dist/ezkernelnew.sym ezkernelnew.elf -- {{kernel_inputs}} || {{just_executable()}} _build-kernel

_build-kernel:
    {{fresh}} stamp build "{{kernel_image}}" Makefile
    {{fresh}} track build ezkernelnew.elf ezkernelnew.gba -- source tests/kernel Makefile
    @rm -f build/emulators.o  # .incbin's files aren't in its .d
    {{kernel_docker}} make TARGET=ezkernelnew -j8
    @{{just_executable()}} _package ezkernelnew dist
    {{fresh}} record kernel -- {{kernel_inputs}}

# Turn a linked kernel (STEM.gba and STEM.elf) into OUT/ezkernelnew.bin and its symbols
_package stem out:
    python3 tools/build_id.py {{stem}}.gba {{stem}}.elf
    python3 tools/omega_image.py {{stem}}.gba image.bin {{out}}/ezkernelnew.bin
    {{kernel_docker}} sh -c '/opt/devkitpro/devkitARM/bin/arm-none-eabi-nm -n {{stem}}.elf > {{out}}/ezkernelnew.nm'
    python3 tools/nm_to_sym.py {{out}}/ezkernelnew.nm {{out}}/ezkernelnew.sym {{stem}}.gba

# Check the build is reproducible: rebuild from a shuffled copy of the source in
# another folder (tools/repro.sh) and compare with `just build`'s files
repro: build
    #!/usr/bin/env bash
    set -euo pipefail
    rm -rf dist/repro && mkdir -p dist/repro
    git ls-files -co --exclude-standard source tests/kernel include Makefile > dist/repro/files.txt
    ls emulators/out/*.gba >> dist/repro/files.txt
    docker run --rm --platform linux/amd64 -u $(id -u):$(id -g) --tmpfs /work:exec,uid=$(id -u) -v "$PWD":/src -w /src -e LIBGBA=/opt/devkitpro/libgba {{kernel_image}} tools/repro.sh dist/repro dist/repro/files.txt
    {{just_executable()}} _package dist/repro/ezkernelnew dist/repro
    status=0
    for file in dist/ezkernelnew.bin dist/ezkernelnew.sym ezkernelnew.elf; do
      copy=dist/repro/$(basename "$file")
      if cmp -s "$file" "$copy"; then echo "PASS: $file is the same ($(sha256sum < "$file" | cut -c1-16))"
      else echo "FAIL: $file differs from $copy"; status=1; fi
    done
    exit $status

# Games given are copied onto the SD card unless a file of that name is
# already there, so their saves are kept.
# Run the kernel on the emulated cart (dist/cart), e.g. `just play ~/roms/game.gba`
[positional-arguments]
play *roms: build emu cart
    if [ $# -gt 0 ]; then {{just_executable()}} cart={{cart}} _cart-copy keep "$@"; fi
    OMEGA_DIR={{cart}} {{mgba}} -{{scale}} dist/ezkernelnew.bin

# Copy the kernel (and any games given) to a real SD card and eject it, e.g. `just sd /Volumes/OMEGA`
[positional-arguments]
sd card *games: build
    tools/sd_copy.sh "$@"

# Pull the kernel's logs off an SD card, e.g. `just logs /Volumes/OMEGA`: joined into
# dist/logs/torii.log, with crash dump addresses named from dist/ezkernelnew.sym
# if its build ID matches; `--tag v0.2.1` uses that release's symbols, `--sym FILE` others
[positional-arguments]
logs path *args:
    python3 tools/logs.py "$@"

# Write compile_commands.json for clangd, covering the kernel and its unit tests
# (devkitPro's headers go to dist/devkitpro; .clangd adapts the flags for clang)
compile-commands: _ready
    mkdir -p dist build build-test
    {{kernel_docker}} sh -c 'make TARGET=ezkernelnew -B -n > dist/dry-run-kernel.txt && make TARGET=ezkerneltest BUILD=build-test -B -n > dist/dry-run-test.txt && cd /opt && cp -r --parents devkitpro/libgba/include devkitpro/devkitARM/arm-none-eabi/include /src/dist/'
    python3 tools/compile_commands.py "$PWD" build=dist/dry-run-kernel.txt build-test=dist/dry-run-test.txt
    rm dist/dry-run-kernel.txt dist/dry-run-test.txt

# Kernel size against the cart and GBA memory limits, and the largest symbols
size count="12": build
    {{kernel_docker}} tools/size.sh ezkernelnew.elf ezkernelnew.gba {{count}}

# Largest stack frames and deepest call chains (a static estimate, see tools/stack.py)
stack count="12": build
    python3 tools/stack.py build {{count}}

# Like play, with tools/remote.lua taking commands on 127.0.0.1:8888 (keys, memory, screenshots, states)
[positional-arguments]
play-remote *roms: build emu cart
    if [ $# -gt 0 ]; then {{just_executable()}} cart={{cart}} _cart-copy keep "$@"; fi
    MGBA_SCRIPT=tools/remote.lua OMEGA_DIR={{cart}} {{mgba}} -{{scale}} dist/ezkernelnew.bin

# Headless emulated cart at real speed, taking tools/remote.lua commands on 127.0.0.1:8888 until stopped
remote *args: build emu-headless cart
    docker rm -f torii-remote >/dev/null 2>&1 || true
    docker run --rm --name torii-remote -p 127.0.0.1:8888:8888 -e REMOTE_HOST=0.0.0.0 -u $(id -u):$(id -g) -v "$PWD":/src -w /src -e OMEGA_DIR={{cart}} -e OMEGA_TRACE {{emu_image}} emu/build-linux/mgba-shot dist/ezkernelnew.bin -L tools/remote.lua -t {{args}}

# Like play, but mGBA waits for a debugger on port 2345; attach with `just gdb`
[positional-arguments]
debug *roms: build emu cart
    if [ $# -gt 0 ]; then {{just_executable()}} cart={{cart}} _cart-copy keep "$@"; fi
    OMEGA_DIR={{cart}} {{mgba}} -{{scale}} -g dist/ezkernelnew.bin

# Like play, with mGBA's command-line debugger in this terminal (kernel symbols loaded)
[positional-arguments]
debug-cli *roms: build emu cart
    if [ $# -gt 0 ]; then {{just_executable()}} cart={{cart}} _cart-copy keep "$@"; fi
    OMEGA_DIR={{cart}} {{mgba}} -{{scale}} -d dist/ezkernelnew.bin

# Headless run that waits for `just gdb`; takes mgba-shot options, e.g. `just debug-shot -f 600`
debug-shot *args: build emu-headless cart
    docker rm -f torii-debug >/dev/null 2>&1 || true
    docker run --rm --name torii-debug -u $(id -u):$(id -g) -v "$PWD":/src -w /src -e OMEGA_DIR={{cart}} -e OMEGA_TRACE {{emu_image}} emu/build-linux/mgba-shot dist/ezkernelnew.bin -g 2345 {{args}}

# Extra arguments go to gdb, e.g. `just gdb -batch -ex 'break main' -ex continue -ex bt`
# Attach devkitARM's gdb, with the kernel's symbols, to `just debug` or `just debug-shot`
[positional-arguments]
gdb *args:
    if docker ps -q -f name=^torii-debug$ | grep -q .; then net="--network container:torii-debug" host=127.0.0.1; else net="{{gdb_net}}" host={{gdb_host}}; fi; \
    docker run --rm $([ -t 0 ] && echo -it || echo -i) --platform linux/amd64 $net -u $(id -u):$(id -g) -v "$PWD":/src -w /src {{kernel_image}} /opt/devkitpro/devkitARM/bin/arm-none-eabi-gdb -q -ex "target remote $host:2345" "$@" ezkernelnew.elf

# Rebuild and relaunch the emulated cart whenever source/ changes
dev: emu cart
    OMEGA_SCALE={{scale}} tools/dev_watch.py

# Headless screenshot on the emulated cart, e.g. `just shot 300 100:A 106:-`
shot frame="300" *keys: build emu-headless cart
    mkdir -p dist/shots
    {{emu_docker}} env OMEGA_DIR={{cart}} emu/build-linux/mgba-shot dist/ezkernelnew.bin {{ prepend("-k ", keys) }} -s {{frame}}:dist/shots/shot.png

# Run the emulated cart headlessly with mgba-shot options, e.g. `just run -u 'launch' -r 0 -m 0:0x02000000:64`
[positional-arguments]
run *args: build emu-headless cart
    {{emu_docker}} env OMEGA_DIR={{cart}} OMEGA_TRACE="${OMEGA_TRACE:-}" emu/build-linux/mgba-shot dist/ezkernelnew.bin "$@"

# Run the host tests, then boot test ROMs through the kernel and check every save type survives power cycles
test: test-host build emu-headless test-roms _test-kernel-build
    {{emu_docker}} tests/all.sh
    @{{just_executable()}} _test-run

# Check the golden screens and the cycle budget (part of `just test`), e.g. `just shots settings1`
[positional-arguments]
shots *scenarios: build emu-headless test-roms _test-kernel-build
    {{emu_docker}} python3 tests/shots.py "$@"

# Re-record the golden screens (tests/shots/hashes.txt and png/) and print what changed;
# name scenarios to re-record only those, e.g. `just shots-update settings1`
[positional-arguments]
shots-update *scenarios: build emu-headless test-roms _test-kernel-build
    {{emu_docker}} python3 tests/shots.py --update "$@"

# Re-record the cycle budget (tests/perf/budget.txt) from the scenarios' frame-cycles lines
[positional-arguments]
perf-update *scenarios: build emu-headless test-roms _test-kernel-build
    {{emu_docker}} python3 tests/shots.py --update-perf "$@"

# `just run` works with OMEGA_TRACE unset and set (on a cart of its own)
_test-run:
    #!/usr/bin/env bash
    set -uo pipefail
    plain=$(env -u OMEGA_TRACE {{just_executable()}} cart=dist/cart-run run -f 1 2>&1) && ! grep -q '^omega ' <<<"$plain" &&
      traced=$(OMEGA_TRACE=1 {{just_executable()}} cart=dist/cart-run run -f 1 2>&1) && grep -q '^omega 0: mode kernel' <<<"$traced" &&
      echo "PASS: just run works with and without OMEGA_TRACE" ||
      { echo "FAIL: just run"; printf '%s\n' "${plain:-}" "${traced:-}" | tail -5; exit 1; }

# Check that text files are UTF-8 with LF, and lint and format-check the C, Python, shell scripts and CI workflows (pinned versions)
lint:
    python3 tools/text_check.py
    git ls-files -z '*.c' '*.h' | xargs -0 uvx clang-format@{{clang_format}} --dry-run -Werror
    uvx ruff@0.17.1 check tools tests
    uvx ruff@0.17.1 format --check tools tests
    uvx --from shellcheck-py@0.11.0.1 shellcheck tests/*.sh tools/*.sh emu/*.sh emulators/*.sh
    uvx --from shfmt-py@4.2.0 shfmt -d -i 2 tests/*.sh tools/*.sh emu/*.sh emulators/*.sh
    uvx --from actionlint-py@1.7.12.25 actionlint .github/workflows/*.yml

# Format the C files that `just lint` checks, in place
format:
    git ls-files -z '*.c' '*.h' | xargs -0 uvx clang-format@{{clang_format}} -i

# Advisory static analysis of source/ (not FatFs): cppcheck, clang-tidy and GCC 10's
# -fanalyzer. Fails only on findings missing from tools/analyze-baseline.txt
analyze: _analyze-run
    python3 tools/analyze.py check dist/analyze tools/analyze-baseline.txt

# Run the analysis and write its findings to tools/analyze-baseline.txt
analyze-baseline: _analyze-run
    python3 tools/analyze.py baseline dist/analyze tools/analyze-baseline.txt

# cppcheck reads tests/host/gba_base.h for libgba's macros, as it skips system headers. Left
# out: style-only checks, printf types (GCC checks those for the real target) and GNU void * arithmetic.
cppcheck_flags := "--include=tests/host/gba_base.h --platform=unix32 --enable=warning,style,performance,portability --inline-suppr --suppress=missingIncludeSystem --suppress=variableScope --suppress=constParameterPointer --suppress=constVariablePointer --suppress='invalidPrintfArgType_*' --suppress=arithOperationsOnVoidPointer --template='{file}:{line}: {severity}: {id}: {message}'"

# The three tools run at once, each into its own file, and GCC one process per
# source file (tools/analyze.py gcc). clang-tidy stays one process: per file, it
# would repeat a header's findings once per file it is included from.
# The repo is mounted at its own path, so compile_commands.json's paths work in containers.
_analyze-run: compile-commands _analyze-image
    #!/usr/bin/env bash
    set -uo pipefail
    trap 'kill $(jobs -p) 2>/dev/null' INT TERM
    mkdir -p dist/analyze
    rm -f dist/analyze/cppcheck.txt dist/analyze/clang-tidy.txt dist/analyze/gcc.txt
    python3 tools/analyze.py project dist/analyze/project.json || exit 1
    docker run --rm -u $(id -u):$(id -g) -v "$PWD":"$PWD" -w "$PWD" torii-analyze cppcheck -q --project=dist/analyze/project.json {{cppcheck_flags}} 2> dist/analyze/cppcheck.txt &
    cppcheck=$!
    python3 tools/analyze.py files | xargs uvx clang-tidy@{{clang_tidy}} -p . --quiet > dist/analyze/clang-tidy.txt 2>/dev/null &
    tidy=$!
    python3 tools/analyze.py gcc dist/analyze/gcc | docker run --rm -i --platform linux/amd64 -u $(id -u):$(id -g) -v "$PWD":"$PWD" -w "$PWD" {{kernel_image}} sh > dist/analyze/gcc.txt 2>&1 &
    gcc=$!
    failed=0
    wait $cppcheck || { echo "cppcheck failed:"; tail -n 20 dist/analyze/cppcheck.txt; failed=1; }
    wait $tidy || { echo "clang-tidy failed:"; grep -A3 ' error: ' dist/analyze/clang-tidy.txt | head -n 20; failed=1; }
    wait $gcc || { echo "gcc -fanalyzer failed:"; tail -n 20 dist/analyze/gcc.txt; failed=1; }
    wc -l dist/analyze/cppcheck.txt dist/analyze/clang-tidy.txt dist/analyze/gcc.txt | sed '$d'
    exit $failed

# Run the checks the changed files need (against main, or --staged), e.g. `just check`;
# --full adds the before-merge tier (compat --all), --dry-run only prints the plan
[positional-arguments]
check *args:
    @JUST={{just_executable()}} python3 tools/check.py "$@"

_analyze-image: _ready
    @{{fresh}} check analyze-image -- docker/analyze.Dockerfile && docker image inspect torii-analyze >/dev/null 2>&1 || { docker build -q -t torii-analyze -f docker/analyze.Dockerfile docker >/dev/null && {{fresh}} record analyze-image -- docker/analyze.Dockerfile; }

# Play the listed games through the kernel and write dist/compat/index.html. Filters pick
# games by code, system, save chip or file name, e.g. `just compat gbc` or `just compat Pokemon`
[positional-arguments]
compat *filters: build emu-headless
    {{emu_docker}} python3 tests/games.py "$@"

# Build the hardware-free kernel code with tests/host for the host, under ASan and UBSan, and run it;
# test tools/font.py, tools/asset.py, tools/art.py, tools/art_scrape.py, tools/art_pack.py, tools/library_report.py, the palette and the upgrade file's layout
test-host: emu-image
    {{emu_docker}} tests/host.sh
    @uv run -q tests/font_test.py
    @uv run -q tests/asset_test.py
    @uv run -q tests/image_test.py
    @uv run -q tests/names_db_test.py
    @uv run -q tests/art_test.py
    @uv run -q tests/art_scrape_test.py
    @uv run -q tests/art_pack_test.py
    @uv run -q tests/library_report_test.py

# Convert docs/design/v0.4/palette.json to C: source/ui/palette.c and palette.h
# (committed; tests/host.sh checks they match), then `just shots-update` for the screens
palette:
    uv run -q tools/palette.py

# Convert the menu fonts in assets/fonts to C: source/engine/font_body.c and font_heading.c
# (committed; tests/font_test.py checks they match)
font:
    uv run -q tools/font.py assets/fonts/galmuri/Galmuri9.bdf --name body --line-height 12 --squeeze --clip -o source/engine/font_body
    uv run -q tools/font.py assets/fonts/galmuri/Galmuri11-Bold.bdf --name heading --line-height 16 --squeeze --clip -o source/engine/font_heading

# Convert the menu's art in assets/ui (icons, carousel objects, shadows, the placeholder boxes) to C:
# source/ui/art_data.c and art_ids.h (committed; tests/asset_test.py checks they match)
assets:
    uv run -q tools/asset.py ui

# Redraw the font candidates' preview sheets in docs/research/fonts, e.g. `just font-preview galmuri`
font-preview *keys:
    uv run -q tools/font_preview.py {{keys}}

# Build the test kernel (ezkerneltest.gba) with tests/kernel and run its unit tests headlessly
test-kernel: emu-headless _test-kernel-build
    {{emu_docker}} tests/kernel.sh

_test-kernel-build: emu-image
    @{{fresh}} check test-kernel ezkerneltest.gba -- {{test_kernel_inputs}} || {{just_executable()}} _build-test-kernel

_build-test-kernel:
    {{fresh}} stamp build-test "{{kernel_image}} tests/kernel" Makefile
    {{fresh}} track build-test ezkerneltest.elf ezkerneltest.gba -- source tests/kernel Makefile
    @rm -f build-test/emulators.o
    {{kernel_docker}} make TARGET=ezkerneltest BUILD=build-test -j8
    python3 tools/build_id.py ezkerneltest.gba ezkerneltest.elf
    {{fresh}} record test-kernel -- {{test_kernel_inputs}}

test-roms: _ready
    @{{fresh}} check test-roms {{test_rom_outputs}} -- tests/roms source/eepromprobe.c source/eepromprobe.h source/rumbleprobe.h || {{just_executable()}} _build-test-roms

_build-test-roms:
    {{kernel_docker}} sh -c 'make -B -C tests/roms/hello && make -B -C tests/roms/idle && make -B -C tests/roms/savetest && make -B -C tests/roms/eeprom && make -B -C tests/roms/rumble && make -B -C tests/roms/crash all'
    python3 tools/nm_to_sym.py tests/roms/crash/crash.nm tests/roms/crash/crash.sym
    {{fresh}} record test-roms -- tests/roms source/eepromprobe.c source/eepromprobe.h source/rumbleprobe.h

# Create the emulated cart (SD image from emu/sd) if it does not exist
cart: emu-image
    {{emu_docker}} tools/make_cart.sh emu/sd {{cart}}

# Delete the emulated cart's SD card, NOR and SRAM, then recreate it
cart-reset:
    rm -rf {{cart}}
    just cart

# Copy files onto the emulated cart's SD card, e.g. `just cart-add ~/roms/game.gba`
[positional-arguments]
cart-add +files: cart
    {{just_executable()}} cart={{cart}} _cart-copy overwrite "$@"

# Copy files to the SD card's root (mode: overwrite or keep). They are staged
# in dist/ because the container only sees the repo.
[positional-arguments]
_cart-copy mode +files:
    rm -rf dist/.cart-add && mkdir -p dist/.cart-add && shift && cp "$@" dist/.cart-add/
    {{fresh}} sync --all dist/.cart-add
    {{emu_docker}} tools/cart_copy.sh {{mode}} {{cart}}/sd.img dist/.cart-add/*; status=$?; rm -rf dist/.cart-add; exit $status

# Write the game names database to an SD card, e.g. `just names-update /Volumes/OMEGA`: /torii/names.db
# from the No-Intro dats of libretro-database (CC BY-SA 4.0; downloaded once to the git-ignored
# dist/dats, never bundled) and the CRCs of the card's games in /torii/crc.txt. --help lists the options
[positional-arguments]
names-update sd_path *args:
    uv run -q tools/names_db.py update "$@"

# Your own names for many games on an SD card, e.g. `just names-add /Volumes/OMEGA names.tsv`: FILE has
# a line per game, its file name (with or without the extension), a tab and its name; they go into
# /torii/names.txt, as Rename's do. --dry-run: see --help
[positional-arguments]
names-add sd_path file *args:
    uv run -q tools/names_db.py add "$@"

# Make custom box art from any pictures (PNG, JPEG...) on an SD card: `just art-add /Volumes/OMEGA`
# converts every picture in /torii/art/custom-src (or a folder given after the card) named after a
# game's file name, `just art-add "Pokemon Prism.gbc" ~/prism.jpg /Volumes/OMEGA` one game's:
# /torii/art/custom/NAME.bin, cropped to a square, 80x80, 15 colours dithered. Options: see --help
[positional-arguments]
art-add +args:
    uv run -q tools/art.py add "$@"

# Fetch box art for an SD card's games from libretro-thumbnails, e.g. `just art-scrape /Volumes/OMEGA`:
# /torii/art/<CRC32>.bin, matched by CRC through the No-Intro dats (dist/dats), keeping custom art and
# art already there, and deleting the old placeholders /torii/art/box-<console>.bin. Pictures are
# cached in the git-ignored dist/art-cache. Box art has no licence grant: keep it on your card, never
# publish it. --dry-run, --offline, --console: see --help
[positional-arguments]
art-scrape sd_path *args:
    uv run -q tools/art_scrape.py "$@"

# Build the full art pack on an SD card once, e.g. `just art-pack /Volumes/OMEGA`: /torii/art.pak, box art
# for every No-Intro GBA, GBC and GB game (NES too if the card has NES games) that libretro-thumbnails
# has, so new official games get covers with no PC (romhacks still need `just art-add`). Downloads are
# cached in dist/art-cache, so a stopped run resumes. Keep the pack on your card, never publish it.
# --console, --offline, --jobs: see --help
[positional-arguments]
art-pack sd_path *args:
    uv run -q tools/art_pack.py "$@"

# Report an SD card's games that need a name or box art, e.g. `just library-report /Volumes/OMEGA`:
# offline, reads the card only. --write-templates writes names-todo.txt (for `just names-add`) and
# art-todo.txt here (or --out DIR). Options: see --help
[positional-arguments]
library-report sd_path *args:
    uv run -q tools/library_report.py "$@"

# Copy games (files or whole folders) into roms/ and list them in roms/games.toml, e.g. `just game-add ~/roms`
[positional-arguments]
game-add +paths:
    python3 tools/games.py add "$@"

# Build the patched mGBA natively (SDL frontend) for `play` and `dev`
emu:
    emu/fetch.sh
    cmake -Wno-author -Wno-deprecated -S emu/mgba -B emu/build-native -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix lua@5.4 2>/dev/null)" -DBUILD_QT=OFF -DBUILD_SDL=ON -DSDL_VERSION=2 -DUSE_FFMPEG=OFF -DUSE_LIBZIP=OFF -DUSE_MINIZIP=OFF -DUSE_SQLITE3=OFF -DUSE_ELF=OFF -DUSE_LUA=5.4 -DUSE_EPOXY=OFF -DUSE_DISCORD_RPC=OFF -DUSE_EDITLINE=ON -DUSE_LZMA=OFF -DBUILD_PERF=OFF -DBUILD_TEST=OFF -DBUILD_SUITE=OFF -DBUILD_EXAMPLE=OFF >/dev/null
    cmake --build emu/build-native -j8 -- --no-print-directory 2>&1 | grep -E 'error|warning: .*omega' || true

# Build the patched mGBA and the mgba-shot runner headlessly in Docker
emu-headless: emu-image
    @emu/fetch.sh
    @{{fresh}} check emu-headless emu/build-linux/mgba-shot -- {{emu_inputs}} || {{just_executable()}} _build-emu-headless

_build-emu-headless:
    {{fresh}} track emu/build-linux emu/build-linux/libmgba.a -- emu/mgba/src emu/mgba/include
    {{emu_docker}} emu/build.sh
    {{fresh}} record emu-headless -- {{emu_inputs}}

# Fetch the built-in emulators (emulators/fetch.sh) and build any whose source changed
emulators: _ready
    @emulators/fetch.sh
    @{{fresh}} check jagoombacolor emulators/out/jagoombacolor.gba -- emulators/build.sh emulators/jagoombacolor/src emulators/jagoombacolor/Makefile {{jagoombacolor_image}} || {{just_executable()}} _build-emulator jagoombacolor {{jagoombacolor_image}} "emulators/jagoombacolor/src emulators/jagoombacolor/Makefile"
    @{{fresh}} check pocketnes emulators/out/pocketnes.gba -- emulators/build.sh emulators/pocketnes/src emulators/pocketnes/Makefile emulators/pocketnes/Makefile_mb {{pocketnes_image}} || {{just_executable()}} _build-emulator pocketnes {{pocketnes_image}} "emulators/pocketnes/src emulators/pocketnes/Makefile emulators/pocketnes/Makefile_mb"

_build-emulator name image inputs:
    docker run --rm --platform linux/amd64 -u $(id -u):$(id -g) -v "$PWD":/src -w /src {{image}} emulators/build.sh {{name}}
    {{fresh}} record {{name}} -- emulators/build.sh {{inputs}} {{image}}

# Save changes made in emulators/NAME back into emulators/patches/NAME.patch
emulators-save-patch name:
    emulators/save-patch.sh {{name}}

# Save changes made in emu/mgba back into emu/patches
emu-save-patch:
    emu/save-patch.sh

# Build the tools image that tests, the emulated cart and mgba-shot run in
emu-image: _ready
    @{{fresh}} check emu-image -- docker/emu.Dockerfile && docker image inspect {{emu_image}} >/dev/null 2>&1 || { docker build -q -t {{emu_image}} -f docker/emu.Dockerfile docker >/dev/null && {{fresh}} record emu-image -- docker/emu.Dockerfile; }

# Every recipe that runs a container comes through here: make sure the torii
# VM runs and that containers see the latest edits.
_ready: _vm
    @{{fresh}} sync source Makefile image.bin tests tools emulators emu/mgba/src emu/mgba/include emu/mgba-shot.c emu/build.sh docker

# Start the torii colima VM on macOS if it isn't running
_vm:
    @if [ "$(uname)" = Darwin ] && ! colima status -p torii >/dev/null 2>&1; then colima start -p torii --vm-type vz --mount-type virtiofs --vz-rosetta --cpu 8 --memory 8 && docker context use colima >/dev/null 2>&1 || true; fi

# Remove build outputs (keeps the emulated cart in dist/cart)
clean:
    {{kernel_docker}} make TARGET=ezkernelnew clean
    rm -rf dist/*.bin dist/*.gba dist/*.img dist/shots emu/build-linux emu/build-native
