# Static analysis image for `just analyze`: Debian trixie's cppcheck,
# pinned. Sources are mounted at run time, at the same path as on the host.
FROM debian:trixie-slim

RUN apt-get update \
 && apt-get install -y --no-install-recommends cppcheck=2.17.1-2 \
 && rm -rf /var/lib/apt/lists/*
