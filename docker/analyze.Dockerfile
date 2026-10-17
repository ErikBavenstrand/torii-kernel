# Static analysis image for `just analyze`: Debian trixie's cppcheck. Sources
# are mounted at run time, at the same path as on the host.
#
# Pinned: the base image by digest, and packages to the Debian snapshot that
# image was built from, so rebuilds install the same cppcheck.
FROM debian:trixie-slim@sha256:a29215f6a35e51e22adffa17f89e9d2ef06214e64a2bad10d765c46aea49f11f

ARG SNAPSHOT=20261005T000000Z
RUN sed -Ei "s|^URIs: http://deb.debian.org/(debian(-security)?)\$|URIs: http://snapshot.debian.org/archive/\\1/${SNAPSHOT}|" /etc/apt/sources.list.d/debian.sources \
 && apt-get -o Acquire::Check-Valid-Until=false update \
 && apt-get install -y --no-install-recommends cppcheck=2.17.1-2 \
 && rm -rf /var/lib/apt/lists/*
