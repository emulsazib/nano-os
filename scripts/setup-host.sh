#!/usr/bin/env bash
set -euo pipefail

INSTALL=false

for arg in "$@"; do
    case "$arg" in
        --install) INSTALL=true ;;
        -h|--help)
            echo "Usage: $0 [--install]"
            echo "  --install   Install missing packages via apt"
            exit 0
            ;;
    esac
done

# Map command -> Debian package
declare -A CMD_PKG=(
    [qemu-system-aarch64]="qemu-system-arm"
    [aarch64-linux-gnu-gcc]="gcc-aarch64-linux-gnu"
    [meson]="meson"
    [ninja]="ninja-build"
    [git]="git"
    [python3]="python3"
    [dtc]="device-tree-compiler"
    [pkg-config]="pkg-config"
    [wayland-scanner]="libwayland-dev"
)

REQUIRED_CMDS=(qemu-system-aarch64 aarch64-linux-gnu-gcc meson ninja git python3 dtc pkg-config wayland-scanner)

MISSING_PKGS=()
FOUND=()
NOT_FOUND=()

echo "=== NanoOS Host Dependency Check ==="
echo

for cmd in "${REQUIRED_CMDS[@]}"; do
    if command -v "$cmd" &>/dev/null; then
        FOUND+=("$cmd")
    else
        NOT_FOUND+=("$cmd")
        MISSING_PKGS+=("${CMD_PKG[$cmd]}")
    fi
done

if [ ${#NOT_FOUND[@]} -gt 0 ]; then
    echo "Missing commands:"
    for cmd in "${NOT_FOUND[@]}"; do
        echo "  - $cmd (package: ${CMD_PKG[$cmd]})"
    done
    echo

    if $INSTALL; then
        echo "Installing missing packages..."
        sudo apt-get update -qq
        sudo apt-get install -y "${MISSING_PKGS[@]}"
        echo
        echo "Installation complete."
    else
        echo "Run with --install to install missing packages automatically."
        echo "  Or manually: sudo apt-get install ${MISSING_PKGS[*]}"
    fi
else
    echo "All required commands found."
fi

echo
echo "=== Version Summary ==="
for cmd in "${REQUIRED_CMDS[@]}"; do
    if command -v "$cmd" &>/dev/null; then
        case "$cmd" in
            qemu-system-aarch64) ver=$("$cmd" --version 2>/dev/null | head -1) ;;
            aarch64-linux-gnu-gcc) ver=$("$cmd" --version 2>/dev/null | head -1) ;;
            meson) ver=$("$cmd" --version 2>/dev/null) ;;
            ninja) ver=$("$cmd" --version 2>/dev/null) ;;
            git) ver=$("$cmd" --version 2>/dev/null) ;;
            python3) ver=$("$cmd" --version 2>/dev/null) ;;
            dtc) ver=$("$cmd" --version 2>/dev/null) ;;
            pkg-config) ver=$("$cmd" --version 2>/dev/null) ;;
            wayland-scanner) ver=$("$cmd" --version 2>/dev/null 2>&1 || echo "installed") ;;
            *) ver="unknown" ;;
        esac
        printf "  %-30s %s\n" "$cmd" "$ver"
    else
        printf "  %-30s %s\n" "$cmd" "NOT FOUND"
    fi
done

echo
echo "=== Done ==="
