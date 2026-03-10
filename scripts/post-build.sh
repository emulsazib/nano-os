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

SYSTEMD_DIR="$TARGET_DIR/etc/systemd/system"

# ── 1. Set default systemd target to graphical ──────────────
ln -sf /usr/lib/systemd/system/graphical.target "$SYSTEMD_DIR/default.target"

# ── 2. Enable nano-session.target under graphical.target ────
mkdir -p "$SYSTEMD_DIR/graphical.target.wants"
ln -sf ../nano-session.target "$SYSTEMD_DIR/graphical.target.wants/nano-session.target"

# ── 3. Enable all NanoOS services under nano-session.target ─
mkdir -p "$SYSTEMD_DIR/nano-session.target.wants"

NANO_SERVICES=(
    nano-compositor.service
    nano-shell.service
    nano-settings.service
    nano-notif.service
    nano-power.service
    nano-net.service
    nano-device.service
    pipewire.service
)

for svc in "${NANO_SERVICES[@]}"; do
    if [ -f "$SYSTEMD_DIR/$svc" ]; then
        ln -sf "../$svc" "$SYSTEMD_DIR/nano-session.target.wants/$svc"
        echo "  Enabled: $svc"
    else
        echo "  Skipped (not found): $svc"
    fi
done

# ── 4. Enable serial console autologin ──────────────────────
# This allows QEMU serial mode to auto-login as root
GETTY_OVERRIDE="$SYSTEMD_DIR/serial-getty@ttyAMA0.service.d"
if [ -d "$GETTY_OVERRIDE" ]; then
    echo "  Autologin configured for ttyAMA0"
fi

# ── 5. Create runtime directory structure ───────────────────
# These are tmpfs at runtime but we ensure overlay has the dirs
mkdir -p "$TARGET_DIR/run/user/0"
mkdir -p "$TARGET_DIR/var/lib/nano/settings"
mkdir -p "$TARGET_DIR/var/lib/nano/notes"
mkdir -p "$TARGET_DIR/var/log/nano"
mkdir -p "$TARGET_DIR/var/cache/nano"

# ── 6. Install desktop files for demo apps ──────────────────
APPS_DIR="$TARGET_DIR/usr/share/applications"
mkdir -p "$APPS_DIR"
# Desktop files are installed by meson, but create the dir just in case

# ── 7. Set permissions ──────────────────────────────────────
# Ensure scripts are executable
chmod 0644 "$TARGET_DIR/etc/environment" 2>/dev/null || true
chmod 0644 "$TARGET_DIR/etc/nano/"*.conf 2>/dev/null || true
chmod 0755 "$TARGET_DIR/etc/profile.d/"*.sh 2>/dev/null || true

# ── 8. Create machine-id (systemd needs it) ─────────────────
if [ ! -f "$TARGET_DIR/etc/machine-id" ]; then
    # Will be regenerated on first boot by systemd
    echo "uninitialized" > "$TARGET_DIR/etc/machine-id"
fi

# ── 9. Set hostname ─────────────────────────────────────────
echo "nano-os" > "$TARGET_DIR/etc/hostname"

# ── 10. Configure locale ────────────────────────────────────
cat > "$TARGET_DIR/etc/locale.conf" << 'LOCALE'
LANG=en_US.UTF-8
LC_ALL=en_US.UTF-8
LOCALE

echo "=== Post-Build Complete ==="
