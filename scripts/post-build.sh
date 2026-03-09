#!/usr/bin/env bash
set -euo pipefail

# Buildroot post-build script
# Called by Buildroot after building, with TARGET_DIR as $1

TARGET_DIR="${1:-}"

if [ -z "$TARGET_DIR" ]; then
    echo "Error: TARGET_DIR not provided (should be passed by Buildroot)"
    exit 1
fi

echo "=== NanoOS Post-Build Script ==="
echo "  TARGET_DIR: $TARGET_DIR"

# Enable nano-session.target under graphical.target
SYSTEMD_DIR="$TARGET_DIR/etc/systemd/system"

mkdir -p "$SYSTEMD_DIR/graphical.target.wants"
ln -sf ../nano-session.target "$SYSTEMD_DIR/graphical.target.wants/nano-session.target"

# Enable nano services under nano-session.target
mkdir -p "$SYSTEMD_DIR/nano-session.target.wants"
for svc in nano-compositor.service nano-shell.service nano-settings.service nano-notif.service nano-power.service nano-net.service; do
    ln -sf "../$svc" "$SYSTEMD_DIR/nano-session.target.wants/$svc"
done

# Set default systemd target to graphical.target
ln -sf /usr/lib/systemd/system/graphical.target "$SYSTEMD_DIR/default.target"

# Create /run/user/0 directory structure (will be tmpfs at runtime, but ensure the path exists)
mkdir -p "$TARGET_DIR/run/user/0"

echo "=== Post-Build Complete ==="
