# Linux container for the wine-nx host-sim (presentation-path only).
#
# Exists because native macOS builds on this machine are blocked by a
# macOS-26-level kernel policy against Wine's loader segment-alignment trick
# -- see wine-nx-probe/switch-shims/macos26-loader-incompatibility.md. Linux
# doesn't use that Mach-O-specific mechanism at all, so the whole problem
# doesn't apply here.
#
# This image only provides the OS-level toolchain. The actual configure/make
# steps run interactively against a bind-mounted repo (see run-host-sim.sh),
# same pattern as wine-nx-probe/build-switch.sh's use of devkitpro/devkita64.

FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    bison \
    flex \
    pkg-config \
    libsdl2-dev \
    libfreetype6-dev \
    libfontconfig1-dev \
    gettext \
    curl \
    ca-certificates \
    xz-utils \
    xvfb \
    imagemagick \
    x11-utils \
    && rm -rf /var/lib/apt/lists/*

# Pinned to match the macOS host recipe in the main README (mstorsjo/llvm-mingw),
# just the Linux/aarch64 build of the same release instead of macos-universal.
ARG LLVM_MINGW_VERSION=20260505
RUN curl -L -o /tmp/llvm-mingw.tar.xz \
      "https://github.com/mstorsjo/llvm-mingw/releases/download/${LLVM_MINGW_VERSION}/llvm-mingw-${LLVM_MINGW_VERSION}-ucrt-ubuntu-22.04-aarch64.tar.xz" \
    && mkdir -p /opt/llvm-mingw \
    && tar -C /opt/llvm-mingw --strip-components=1 -xf /tmp/llvm-mingw.tar.xz \
    && rm /tmp/llvm-mingw.tar.xz

ENV PATH="/opt/llvm-mingw/bin:${PATH}"

WORKDIR /work
