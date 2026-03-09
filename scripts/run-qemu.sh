#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

SERIAL=false
EXTRA_ARGS=()

usage() {
    echo "Usage: $0 [--serial] [-- extra-qemu-args...]"
    echo "  --serial    Run in serial/nographic mode (no GUI)"
    exit 0
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --serial) SERIAL=true; shift ;;
        -h|--help) usage ;;
        --)
            shift
            EXTRA_ARGS=("$@")
            break
            ;;
        *) echo "Unknown option: $1"; usage ;;
    esac
done

# Locate kernel and rootfs images
KERNEL=""
ROOTFS=""

for dir in "$PROJECT_ROOT/images" "$PROJECT_ROOT/buildroot/output/images"; do
    if [ -z "$KERNEL" ] && [ -f "$dir/Image" ]; then
        KERNEL="$dir/Image"
    fi
    if [ -z "$ROOTFS" ] && [ -f "$dir/rootfs.ext4" ]; then
        ROOTFS="$dir/rootfs.ext4"
    fi
done

if [ -z "$KERNEL" ]; then
    echo "Error: Kernel Image not found in images/ or buildroot/output/images/"
    echo "Run ./scripts/build-image.sh first."
    exit 1
fi

if [ -z "$ROOTFS" ]; then
    echo "Error: rootfs.ext4 not found in images/ or buildroot/output/images/"
    echo "Run ./scripts/build-image.sh first."
    exit 1
fi

echo "=== NanoOS QEMU Launcher ==="
echo "  Kernel:  $KERNEL"
echo "  Rootfs:  $ROOTFS"
echo "  Mode:    $(if $SERIAL; then echo serial; else echo graphical; fi)"
echo

QEMU_ARGS=(
    -machine virt
    -cpu cortex-a72
    -m 2G
    -smp 4

    # Kernel
    -kernel "$KERNEL"
    -append "root=/dev/vda rw console=ttyAMA0 loglevel=4 systemd.unit=graphical.target"

    # Root filesystem
    -drive "file=$ROOTFS,format=raw,if=none,id=hd0"
    -device "virtio-blk-pci,drive=hd0"

    # Graphics
    -device virtio-gpu-pci

    # Networking with SSH port forward
    -device virtio-net-pci,netdev=net0
    -netdev "user,id=net0,hostfwd=tcp::2222-:22"

    # RNG
    -device virtio-rng-pci

    # Keyboard/Mouse
    -device virtio-keyboard-pci
    -device virtio-mouse-pci
)

if $SERIAL; then
    QEMU_ARGS+=(-nographic)
else
    QEMU_ARGS+=(-display gtk,gl=on)
fi

# Append any extra args
if [ ${#EXTRA_ARGS[@]} -gt 0 ]; then
    QEMU_ARGS+=("${EXTRA_ARGS[@]}")
fi

echo "SSH will be available at: ssh -p 2222 root@localhost"
echo "Login credentials: root / nano"
echo
echo "Starting QEMU..."
echo

exec qemu-system-aarch64 "${QEMU_ARGS[@]}"
