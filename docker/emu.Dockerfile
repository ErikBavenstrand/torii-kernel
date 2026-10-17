# Tools image: builds the patched mGBA (with the Omega DE cart model) headlessly
# for screenshots and tests, runs the image linker, and builds the host tests
# (gcc with ASan and UBSan). Sources are mounted at run time.
#
# Pinned: the base image by digest, and packages to the Debian snapshot that
# image was built from, so rebuilds install the same versions.
FROM debian:bookworm-slim@sha256:7c7b2c966bc9ee8cedfeef67e0e279108992c77681fa595db4a9d65c06ccc587

ARG SNAPSHOT=20261005T000000Z
RUN sed -Ei "s|^URIs: http://deb.debian.org/(debian(-security)?)\$|URIs: http://snapshot.debian.org/archive/\\1/${SNAPSHOT}|" /etc/apt/sources.list.d/debian.sources \
 && apt-get -o Acquire::Check-Valid-Until=false update \
 && apt-get install -y --no-install-recommends \
      build-essential cmake pkg-config libpng-dev zlib1g-dev liblua5.4-dev mtools dosfstools python3 \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
