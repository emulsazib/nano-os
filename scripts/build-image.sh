#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

CLEAN=false
MENUCONFIG=false
JOBS=$(nproc)
DEFCONFIG="nano_qemu_aarch64_defconfig"

usage() {
    echo "Usage: $0 [--clean] [--menuconfig] [--jobs N]"
    echo "  --clean       Clean buildroot output before building"
    echo "  --menuconfig  Run menuconfig before building"
    echo "  --jobs N      Number of parallel jobs (default: $(nproc))"
    exit 0
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --clean) CLEAN=true; shift ;;
        --menuconfig) MENUCONFIG=true; shift ;;
        --jobs)
            JOBS="$2"
            shift 2
            ;;
        -h|--help) usage ;;
        *) echo "Unknown option: $1"; usage ;;
    esac
done

BR_DIR="$PROJECT_ROOT/buildroot"
BR_EXTERNAL="$PROJECT_ROOT/external"
BR_OUTPUT="$BR_DIR/output"
DEFCONFIG_SRC="$PROJECT_ROOT/configs/buildroot/$DEFCONFIG"
IMAGES_DIR="$PROJECT_ROOT/images"

if [ ! -d "$BR_DIR/Makefile" ] && [ ! -f "$BR_DIR/Makefile" ]; then
    echo "Error: Buildroot directory not found or not initialized."
    echo "Run: git submodule update --init --recursive"
    exit 1
fi

echo "=== NanoOS Build ==="
echo "  Project root:  $PROJECT_ROOT"
echo "  Buildroot:     $BR_DIR"
echo "  BR2_EXTERNAL:  $BR_EXTERNAL"
echo "  Defconfig:     $DEFCONFIG"
echo "  Jobs:          $JOBS"
echo

# Clean if requested
if $CLEAN; then
    echo "Cleaning buildroot output..."
    make -C "$BR_DIR" clean
    echo
fi

# Copy defconfig into buildroot configs directory
if [ -f "$DEFCONFIG_SRC" ]; then
    echo "Copying defconfig to buildroot..."
    cp "$DEFCONFIG_SRC" "$BR_DIR/configs/$DEFCONFIG"
else
    echo "Warning: Defconfig not found at $DEFCONFIG_SRC"
    echo "Make sure configs/buildroot/$DEFCONFIG exists."
    exit 1
fi

# Set BR2_EXTERNAL and load defconfig
echo "Loading defconfig..."
make -C "$BR_DIR" BR2_EXTERNAL="$BR_EXTERNAL" "$DEFCONFIG"

# Run menuconfig if requested
if $MENUCONFIG; then
    echo "Running menuconfig..."
    make -C "$BR_DIR" BR2_EXTERNAL="$BR_EXTERNAL" menuconfig
fi

# Build
echo "Starting build with $JOBS jobs..."
make -C "$BR_DIR" BR2_EXTERNAL="$BR_EXTERNAL" -j"$JOBS"

# Copy output images
echo "Copying images to $IMAGES_DIR..."
mkdir -p "$IMAGES_DIR"

for img in Image rootfs.ext4 rootfs.ext2; do
    src="$BR_OUTPUT/images/$img"
    if [ -f "$src" ]; then
        cp "$src" "$IMAGES_DIR/"
        echo "  Copied: $img"
    fi
done

# Also copy DTBs if present
if ls "$BR_OUTPUT/images/"*.dtb &>/dev/null; then
    cp "$BR_OUTPUT/images/"*.dtb "$IMAGES_DIR/"
    echo "  Copied: DTB files"
fi

echo
echo "=== Build Summary ==="
echo "  Output directory: $BR_OUTPUT"
echo "  Images copied to: $IMAGES_DIR"
echo
ls -lh "$IMAGES_DIR/" 2>/dev/null || echo "  (no images found)"
echo
echo "=== Build Complete ==="
echo "Run ./scripts/run-qemu.sh to start the VM."
