# Tools image: builds the patched mGBA (with the Omega DE cart model) headlessly
# for screenshots and tests, and runs the image linker. Sources are mounted at run time.
FROM debian:bookworm-slim

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential cmake pkg-config libpng-dev zlib1g-dev liblua5.4-dev mtools dosfstools python3 \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
