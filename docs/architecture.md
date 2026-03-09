# NanoOS Architecture

## Overview

NanoOS is a custom Linux-based mobile operating system designed for touch-first interaction. It runs on ARM64 hardware (QEMU virtual machine for development, Raspberry Pi or similar for production).

## System Layers

```
┌─────────────────────────────────────────────┐
│                Demo Apps                     │
│  nano-clock  nano-calculator  nano-notes     │
├─────────────────────────────────────────────┤
│              nano-shell (GTK4)               │
│  launcher │ statusbar │ notifications        │
├─────────────────────────────────────────────┤
│           System Services (D-Bus)            │
│  settings │ notifications │ power │ network  │
├─────────────────────────────────────────────┤
│       nano-compositor (wlroots 0.17+)        │
│  XDG shell │ layer shell │ scene graph       │
├─────────────────────────────────────────────┤
│              Wayland Protocol                │
│  wayland-server │ wayland-protocols          │
├─────────────────────────────────────────────┤
│         Linux Display Stack                  │
│  DRM/KMS │ libinput │ Mesa (virgl) │ EGL     │
├─────────────────────────────────────────────┤
│            systemd (PID 1)                   │
│  logind │ udev │ journald │ networkd         │
├─────────────────────────────────────────────┤
│           Linux Kernel (arm64)               │
│  virtio │ DRM │ input │ cgroups │ namespaces │
├─────────────────────────────────────────────┤
│           QEMU virt / ARM Hardware           │
└─────────────────────────────────────────────┘
```

## Boot Flow

1. QEMU loads kernel Image directly (no bootloader for dev)
2. Kernel initializes, mounts rootfs from virtio-blk
3. systemd starts as PID 1
4. systemd reaches `graphical.target`
5. `nano-session.target` starts:
   - `nano-compositor.service` (Wayland compositor)
   - `nano-shell.service` (GTK4 mobile shell)
   - `nano-settings.service` (D-Bus settings daemon)
   - `nano-notif.service` (freedesktop notification daemon)
   - `nano-power.service` (battery/power manager)

## Component Details

### nano-compositor

A wlroots-based Wayland compositor implementing:
- **XDG shell**: Application window management with mobile-first auto-maximize
- **Layer shell**: Shell panels (status bar, overlays) via `wlr-layer-shell-unstable-v1`
- **Scene graph**: Modern `wlr_scene` API for rendering (no manual damage tracking)
- **Input**: Keyboard, pointer, and touch support via `wlr_cursor` + `wlr_seat`
- **Mobile behavior**: New windows auto-maximize to fill the screen

Source: `ui/nano-compositor/`

### nano-shell

GTK4 + libadwaita mobile shell providing:
- **Status bar**: Time, battery, WiFi indicators (48px, semi-transparent)
- **App launcher**: Grid of application icons from `.desktop` files, with fallback built-in entries
- **Clock widget**: Large centered time/date display on home screen
- **Notifications**: Slide-in banners with auto-dismiss and swipe-to-dismiss
- **Home indicator**: Bottom pill-shaped gesture area
- **D-Bus integration**: Owns `org.freedesktop.Notifications` on session bus for in-shell notification display

Source: `ui/nano-shell/`

### System Services

All services use GLib/GIO with GDBus, registered on the system bus:

| Service | Bus Name | Purpose |
|---------|----------|---------|
| nano-settings | `org.nano.Settings` | INI-based settings store with Get/Set/GetAll/Save |
| nano-notif | `org.freedesktop.Notifications` | Freedesktop Notifications spec (1.2) implementation |
| nano-power | `org.nano.Power` | Battery monitoring, power profiles, shutdown/reboot |
| nano-net | `org.nano.Network` | Network interface status, WiFi scanning (placeholder) |

Source: `services/`

### Demo Apps

| App | Description |
|-----|-------------|
| nano-clock | Clock, stopwatch, timer with tabbed interface |
| nano-calculator | Four-function calculator with phone-style layout |
| nano-notes | Simple notes app with list/editor views, file-backed storage |

Source: `apps/`

## Build System

- **Buildroot** generates the root filesystem with all system libraries
- **BR2_EXTERNAL** (`external/`) packages NanoOS custom components
- All custom components use **Meson** build system
- Kernel config fragments in `configs/kernel/fragments/` enable required features
- Root filesystem overlay in `overlays/rootfs/` provides systemd units, configs, and D-Bus policies

## IPC Architecture

```
nano-shell ──D-Bus session──► nano-notif (notifications display)
    │
    ├──D-Bus system──► nano-settings (read/write settings)
    ├──D-Bus system──► nano-power (battery level, shutdown)
    └──D-Bus system──► nano-net (network status)

Demo apps ──Wayland──► nano-compositor (window management)
         ──D-Bus──► system services (optional integration)
```

## Configuration Files

| File | Purpose |
|------|---------|
| `/etc/nano/compositor.conf` | Compositor backend, output, input settings |
| `/etc/nano/settings.conf` | System settings (display, sound, network, theme) |
| `/etc/environment` | Wayland session environment variables |
| `/etc/profile.d/nano-env.sh` | XDG and NanoOS environment setup on login |

## Security Model (Planned)

- D-Bus policy files restrict service ownership to root
- systemd service isolation via `User=` and `ProtectSystem=`
- Wayland protocol-level isolation (clients cannot spy on each other)
- Future: AppArmor/SELinux profiles, sandboxed app execution

## Porting Strategy

1. **QEMU aarch64** (current target): virtio-gpu, virtio-net, virtio-blk
2. **Raspberry Pi 4**: vc4/v3d DRM driver, USB input, SD card rootfs
3. **Generic ARM64**: DRM/KMS backend auto-detection, device tree overlays
