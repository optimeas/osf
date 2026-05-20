#!/usr/bin/env bash
# install-hdf5.sh — fetch the HDF5 Linux x64 runtime for the OSF -> HDF5 exporter.
#
# Bash counterpart of install-hdf5.ps1. Downloads the official HDF Group
# binary distribution, extracts libhdf5.so* into ./linux-x64/, records a
# SHA-256 and writes ../VERSION.txt.
#
# The Windows path (install-hdf5.ps1) is the verified one — osftool today
# builds and runs on Win64. This script mirrors the same shape for Linux
# and is provided for parity; adjust HDF5_URL to the Linux asset that
# matches your distribution if the default 404s.
#
# Binaries are never committed (see lib/.gitignore); rerun on a fresh
# checkout. Idempotent.
set -euo pipefail

HDF5_VERSION='1.14.4-3'
# The HDF Group ships per-distribution Linux archives; pick the one that
# matches the target glibc. This default targets a generic x86_64 build.
HDF5_URL='https://github.com/HDFGroup/hdf5/releases/download/hdf5_1.14.4.3/hdf5-1.14.4-3-ubuntu-2204_gcc.tar.gz'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LINUX_DIR="$SCRIPT_DIR/linux-x64"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

mkdir -p "$LINUX_DIR"

echo "Downloading HDF5 $HDF5_VERSION (Linux x64) ..."
curl -fsSL "$HDF5_URL" -o "$WORK_DIR/hdf5.tar.gz"

echo "Extracting ..."
tar -xzf "$WORK_DIR/hdf5.tar.gz" -C "$WORK_DIR"

# The HDF Group package may wrap the distribution in a nested archive.
nested="$(find "$WORK_DIR" -name '*.tar.gz' ! -name 'hdf5.tar.gz' -print -quit || true)"
if [ -n "$nested" ]; then
  echo "Extracting nested archive $(basename "$nested") ..."
  tar -xzf "$nested" -C "$WORK_DIR"
fi

so="$(find "$WORK_DIR" -name 'libhdf5.so*' -type f -print -quit || true)"
if [ -z "$so" ]; then
  echo "libhdf5.so was not found inside the downloaded archive." >&2
  exit 1
fi
lib_dir="$(dirname "$so")"
echo "Found libhdf5 in $lib_dir"

# Copy libhdf5.so* and its companion runtime objects (libz, libaec, ...).
copied=0
for f in "$lib_dir"/libhdf5.so* "$lib_dir"/libz.so* "$lib_dir"/libaec.so* "$lib_dir"/libsz.so*; do
  if [ -e "$f" ]; then
    cp -f "$f" "$LINUX_DIR/"
    echo "  copied $(basename "$f")"
    copied=$((copied + 1))
  fi
done
if [ "$copied" -eq 0 ]; then
  echo "No runtime objects were copied — unexpected archive layout." >&2
  exit 1
fi

hash="$(sha256sum "$LINUX_DIR"/libhdf5.so* | head -n1 | cut -d' ' -f1)"
echo "$hash  libhdf5.so" > "$LINUX_DIR/libhdf5.so.sha256"

{
  echo "HDF5 version : $HDF5_VERSION"
  echo "Source URL   : $HDF5_URL"
  echo "Installed UTC: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "libhdf5 SHA256: $hash"
} > "$SCRIPT_DIR/VERSION.txt"

echo ""
echo "HDF5 runtime installed to $LINUX_DIR"
