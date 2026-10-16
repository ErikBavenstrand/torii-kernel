# Omega Remix build commands. Needs Docker and just; `play` and `dev` also need
# CMake, pkg-config and SDL2 to build the patched mGBA natively.
#
# The kernel source is the official 1.06 release, unmodified. It is built with
# devkitARM r54 (GCC 10.1), the closest release to the r53 EZ-Flash used that
# compiles it without changes, pinned by digest. It runs under amd64 emulation.

kernel_image := "devkitpro/devkitarm@sha256:1acab59884288115af0e70782a9b69017a8cc6bad69542b24e8652233f215c56"
emu_image := "omega-remix-emu"
kernel_docker := "docker run --rm --platform linux/amd64 -u $(id -u):$(id -g) -v \"$PWD\":/src -w /src -e LIBGBA=/opt/devkitpro/libgba " + kernel_image
emu_docker := "docker run --rm -u $(id -u):$(id -g) -v \"$PWD\":/src " + emu_image
mgba := "emu/build-native/sdl/mgba"
cart := "dist/cart"

# Build the flashable upgrade file: dist/ezkernelnew.bin
build: emu-image
    mkdir -p dist
    {{kernel_docker}} make TARGET=ezkernelnew -j8
    {{emu_docker}} python3 tools/omega_image.py ezkernelnew.gba image.bin dist/ezkernelnew.bin

# Run the kernel on the emulated Omega DE cart (cart files in dist/cart)
play: build emu cart
    OMEGA_DIR={{cart}} {{mgba}} dist/ezkernelnew.bin

# Rebuild and relaunch the emulated cart whenever source/ changes
dev: emu cart
    tools/dev_watch.py

# Headless screenshot on the emulated cart, e.g. `just shot 300 100:A 106:-`
shot frame="300" *keys: build emu-headless cart
    mkdir -p dist/shots
    {{emu_docker}} env OMEGA_DIR={{cart}} emu/build-linux/mgba-shot dist/ezkernelnew.bin {{ prepend("-k ", keys) }} -s {{frame}}:dist/shots/shot.png

# Boot test ROMs through the kernel and check every save type survives power cycles
test: build emu-headless test-roms
    {{emu_docker}} tests/smoke.sh
    {{emu_docker}} tests/save_types.sh

test-roms:
    {{kernel_docker}} make -C tests/roms/hello
    {{kernel_docker}} make -C tests/roms/savetest

# Create the emulated cart (SD image from emu/sd) if it does not exist
cart: emu-image
    {{emu_docker}} tools/make_cart.sh emu/sd {{cart}}

# Delete the emulated cart's SD card, NOR and SRAM, then recreate it
cart-reset:
    rm -rf {{cart}}
    just cart

# Copy files onto the emulated cart's SD card, e.g. `just cart-add ~/roms/game.gba`
cart-add +files: cart
    for f in {{files}}; do cp "$f" dist/.cart-add-tmp && {{emu_docker}} mcopy -o -i {{cart}}/sd.img dist/.cart-add-tmp "::/$(basename "$f")"; done; rm -f dist/.cart-add-tmp

# Build the patched mGBA natively (SDL frontend) for `play` and `dev`
emu:
    emu/fetch.sh
    cmake -S emu/mgba -B emu/build-native -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release -DBUILD_QT=OFF -DBUILD_SDL=ON -DSDL_VERSION=2 -DUSE_FFMPEG=OFF -DUSE_LIBZIP=OFF -DUSE_MINIZIP=OFF -DUSE_SQLITE3=OFF -DUSE_ELF=OFF -DUSE_LUA=OFF -DUSE_EPOXY=OFF -DUSE_DISCORD_RPC=OFF -DUSE_EDITLINE=OFF -DUSE_LZMA=OFF -DBUILD_PERF=OFF -DBUILD_TEST=OFF -DBUILD_SUITE=OFF -DBUILD_EXAMPLE=OFF >/dev/null
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
