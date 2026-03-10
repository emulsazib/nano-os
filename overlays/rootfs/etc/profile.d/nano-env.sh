#!/usr/bin/env bash
# NanoOS environment setup — sourced on interactive login

# ── XDG directories ──────────────────────────────────────────
export XDG_RUNTIME_DIR="/run/user/$(id -u)"
export XDG_SESSION_TYPE="wayland"
export XDG_CURRENT_DESKTOP="NanoOS"
export XDG_SESSION_DESKTOP="NanoOS"
export XDG_DATA_DIRS="/usr/share:/usr/local/share"
export XDG_CONFIG_DIRS="/etc/xdg"
export XDG_CONFIG_HOME="$HOME/.config"
export XDG_CACHE_HOME="$HOME/.cache"
export XDG_DATA_HOME="$HOME/.local/share"

# ── Wayland ──────────────────────────────────────────────────
export WAYLAND_DISPLAY="wayland-0"
export GDK_BACKEND="wayland"
export CLUTTER_BACKEND="wayland"
export SDL_VIDEODRIVER="wayland"
export QT_QPA_PLATFORM="wayland"
export MOZ_ENABLE_WAYLAND=1
export _JAVA_AWT_WM_NONREPARENTING=1

# ── PipeWire audio ───────────────────────────────────────────
export PIPEWIRE_RUNTIME_DIR="$XDG_RUNTIME_DIR"

# ── NanoOS ───────────────────────────────────────────────────
export NANO_OS_VERSION="0.1.0"
export NANO_CONFIG_DIR="/etc/nano"
export NANO_HOME="/etc/nano"

# ── Ensure runtime directory exists ──────────────────────────
if [ ! -d "$XDG_RUNTIME_DIR" ]; then
    mkdir -p "$XDG_RUNTIME_DIR"
    chmod 0700 "$XDG_RUNTIME_DIR"
fi

# ── Ensure user data directories exist ───────────────────────
mkdir -p "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" "$XDG_DATA_HOME" 2>/dev/null || true
