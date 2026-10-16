# Torii build commands. Needs Docker and just; `play` and `dev` also need
# CMake, pkg-config and SDL2 to build the patched mGBA natively.
#
# The kernel source is the official 1.06 release, unmodified. It is built with
# devkitARM r54 (GCC 10.1), the closest release to the r53 EZ-Flash used that
# compiles it without changes, pinned by digest. It runs under amd64 emulation.

kernel_image := "devkitpro/devkitarm@sha256:1acab59884288115af0e70782a9b69017a8cc6bad69542b24e8652233f215c56"
emu_image := "torii-emu"
kernel_docker := "docker run --rm --platform linux/amd64 -u $(id -u):$(id -g) -v \"$PWD\":/src -w /src -e LIBGBA=/opt/devkitpro/libgba " + kernel_image
emu_docker := "docker run --rm -u $(id -u):$(id -g) -v \"$PWD\":/src " + emu_image
# The SDL emulator, printing the kernel's tlog lines ("GBA Debug: ...")
mgba := "emu/build-native/sdl/mgba -C logLevel.gba.debug=31"
# Emulator window size as a multiple of 240x160, e.g. `just scale=3 play`
scale := "4"
cart := "dist/cart"
# How a container reaches mGBA's GDB stub, which listens on the host's loopback
gdb_net := if os() == "linux" { "--network host" } else { "--add-host=host.docker.internal:host-gateway" }
gdb_host := if os() == "linux" { "127.0.0.1" } else { "host.docker.internal" }

# Build the flashable upgrade file: dist/ezkernelnew.bin
build: emu-image
    mkdir -p dist
    {{kernel_docker}} make TARGET=ezkernelnew -j8
    {{emu_docker}} python3 tools/omega_image.py ezkernelnew.gba image.bin dist/ezkernelnew.bin
    {{kernel_docker}} sh -c '/opt/devkitpro/devkitARM/bin/arm-none-eabi-nm -n ezkernelnew.elf > dist/ezkernelnew.nm'
    {{emu_docker}} python3 tools/nm_to_sym.py dist/ezkernelnew.nm dist/ezkernelnew.sym

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

# Write compile_commands.json for clangd (devkitPro's headers go to dist/devkitpro)
compile-commands: emu-image
    mkdir -p dist
    {{kernel_docker}} sh -c 'make TARGET=ezkernelnew -B -n > dist/make-dry-run.txt 2>/dev/null; cd /opt && cp -r --parents devkitpro/libgba/include devkitpro/devkitARM/arm-none-eabi/include /src/dist/'
    {{emu_docker}} python3 tools/compile_commands.py dist/make-dry-run.txt "$PWD"
    rm dist/make-dry-run.txt

# Kernel size against the cart and GBA memory limits, and the largest symbols
size count="12": build
    {{kernel_docker}} tools/size.sh ezkernelnew.elf ezkernelnew.gba {{count}}

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
    {{emu_docker}} env OMEGA_DIR={{cart}} OMEGA_TRACE emu/build-linux/mgba-shot dist/ezkernelnew.bin "$@"

# Boot test ROMs through the kernel and check every save type survives power cycles
test: build emu-headless test-roms
    {{emu_docker}} tests/smoke.sh
    {{emu_docker}} tests/save_types.sh
    {{emu_docker}} tests/crash.sh
    {{emu_docker}} tests/state.sh

# Play the games listed in roms/games.toml through the kernel, e.g. `just compat BPEE`
compat *codes: build emu-headless
    {{emu_docker}} python3 tests/games.py {{codes}}

test-roms:
    {{kernel_docker}} make -C tests/roms/hello
    {{kernel_docker}} make -C tests/roms/savetest
    {{kernel_docker}} make -C tests/roms/crash all
    {{emu_docker}} python3 tools/nm_to_sym.py tests/roms/crash/crash.nm tests/roms/crash/crash.sym

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
    {{emu_docker}} tools/cart_copy.sh {{mode}} {{cart}}/sd.img dist/.cart-add/*; status=$?; rm -rf dist/.cart-add; exit $status

# Copy games into roms/ and add them to roms/games.toml, e.g. `just game-add ~/roms/game.gba`
[positional-arguments]
game-add +roms: emu-image
    cp -n "$@" roms/
    for f in "$@"; do printf 'roms/%s\0' "$(basename "$f")"; done | xargs -0 {{emu_docker}} python3 tools/games.py add

# Build the patched mGBA natively (SDL frontend) for `play` and `dev`
emu:
    emu/fetch.sh
    cmake -Wno-author -Wno-deprecated -S emu/mgba -B emu/build-native -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release -DBUILD_QT=OFF -DBUILD_SDL=ON -DSDL_VERSION=2 -DUSE_FFMPEG=OFF -DUSE_LIBZIP=OFF -DUSE_MINIZIP=OFF -DUSE_SQLITE3=OFF -DUSE_ELF=OFF -DUSE_LUA=OFF -DUSE_EPOXY=OFF -DUSE_DISCORD_RPC=OFF -DUSE_EDITLINE=ON -DUSE_LZMA=OFF -DBUILD_PERF=OFF -DBUILD_TEST=OFF -DBUILD_SUITE=OFF -DBUILD_EXAMPLE=OFF >/dev/null
    cmake --build emu/build-native -j8 -- --no-print-directory 2>&1 | grep -E 'error|warning: .*omega' || true

# Build the patched mGBA and the mgba-shot runner headlessly in Docker
emu-headless: emu-image
    emu/fetch.sh
    {{emu_docker}} emu/build.sh

# Save changes made in emu/mgba back into emu/patches
emu-save-patch:
    emu/save-patch.sh

emu-image:
    @docker build -q -t {{emu_image}} -f docker/emu.Dockerfile docker >/dev/null

# Remove build outputs (keeps the emulated cart in dist/cart)
clean:
    {{kernel_docker}} make TARGET=ezkernelnew clean
    rm -rf dist/*.bin dist/*.gba dist/*.img dist/shots emu/build-linux emu/build-native
