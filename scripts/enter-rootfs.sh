#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

ROOTFS=""

# Locate rootfs image
for dir in "$PROJECT_ROOT/images" "$PROJECT_ROOT/buildroot/output/images"; do
    if [ -f "$dir/rootfs.ext4" ]; then
        ROOTFS="$dir/rootfs.ext4"
        break
    fi
done

if [ -z "$ROOTFS" ]; then
    echo "Error: rootfs.ext4 not found in images/ or buildroot/output/images/"
    echo "Run ./scripts/build-image.sh first."
    exit 1
fi

MOUNTPOINT="$PROJECT_ROOT/build/rootfs-mount"

echo "=== NanoOS Rootfs Chroot ==="
echo "  Rootfs:     $ROOTFS"
echo "  Mountpoint: $MOUNTPOINT"
echo

if [ "$(id -u)" -ne 0 ]; then
    echo "Error: This script requires root privileges."
    echo "Run: sudo $0"
    exit 1
fi

mkdir -p "$MOUNTPOINT"

cleanup() {
    echo
    echo "Cleaning up mounts..."
    umount -l "$MOUNTPOINT/proc" 2>/dev/null || true
    umount -l "$MOUNTPOINT/sys" 2>/dev/null || true
    umount -l "$MOUNTPOINT/dev/pts" 2>/dev/null || true
    umount -l "$MOUNTPOINT/dev" 2>/dev/null || true
    umount -l "$MOUNTPOINT/run" 2>/dev/null || true
    umount -l "$MOUNTPOINT" 2>/dev/null || true
    echo "Done."
}

trap cleanup EXIT

echo "Mounting rootfs..."
mount -o loop "$ROOTFS" "$MOUNTPOINT"

echo "Setting up bind mounts..."
mount --bind /proc "$MOUNTPOINT/proc"
mount --bind /sys "$MOUNTPOINT/sys"
mount --bind /dev "$MOUNTPOINT/dev"
mount --bind /dev/pts "$MOUNTPOINT/dev/pts"
mkdir -p "$MOUNTPOINT/run"
mount -t tmpfs tmpfs "$MOUNTPOINT/run"

echo "Entering chroot... (type 'exit' to leave)"
echo

chroot "$MOUNTPOINT" /bin/bash -l || true