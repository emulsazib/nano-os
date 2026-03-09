# NanoOS

A custom Linux-based mobile operating system built from scratch, featuring a Wayland compositor, touch-first mobile shell, and modular system services.

## Architecture

```
Apps (Clock, Calculator, Notes) → GTK4 Wayland clients
         ↓
nano-shell (launcher, statusbar, notifications) → GTK4 + libadwaita
         ↓
nano-compositor → wlroots 0.17+ scene-graph compositor
         ↓
Wayland → DRM/KMS → Linux kernel → QEMU virt / ARM hardware
         ↓
System services (settings, notifications, power, network) → GDBus
         ↓
systemd → logind, journald, networkd, udev
```

## Components

| Component | Language | Description |
|-----------|----------|-------------|
| `nano-compositor` | C | wlroots-based Wayland compositor with mobile auto-maximize, touch input, layer shell |
| `nano-shell` | C | GTK4 mobile home screen with app launcher, status bar, notification overlay |
| `nano-settings` | C | D-Bus settings daemon backed by INI config files |
| `nano-notif` | C | Freedesktop Notifications 1.2 compliant notification daemon |
| `nano-power` | C | Battery monitoring, power profiles, shutdown/reboot via D-Bus |
| `nano-net` | C | Network interface monitoring via D-Bus |
| `nano-clock` | C | Clock, stopwatch, and timer GTK4 application |
| `nano-calculator` | C | Touch-friendly calculator GTK4 application |
| `nano-notes` | C | Simple note-taking GTK4 application with file-backed storage |

## Project Structure

```
nano-os/
├── buildroot/                  # Buildroot submodule (generates rootfs + kernel)
├── external/                   # BR2_EXTERNAL layer
│   ├── package/                # Buildroot package definitions for NanoOS components
│   ├── Config.in
│   ├── external.mk
│   └── external.desc
├── configs/
│   ├── buildroot/              # Buildroot defconfigs
│   └── kernel/fragments/       # Kernel config fragments (base, wayland, mobile)
├── overlays/rootfs/            # Root filesystem overlay
│   └── etc/
│       ├── nano/               # NanoOS configuration files
│       ├── systemd/system/     # systemd service units
│       ├── dbus-1/system.d/    # D-Bus policy files
│       └── profile.d/          # Shell environment setup
├── ui/
│   ├── nano-compositor/        # Wayland compositor (wlroots)
│   └── nano-shell/             # Mobile shell (GTK4)
├── services/
│   ├── nano-settings/          # Settings service
│   ├── nano-notif/             # Notification service
│   ├── nano-power/             # Power service
│   └── nano-net/               # Network service
├── apps/
│   ├── nano-clock/             # Clock app
│   ├── nano-calculator/        # Calculator app
│   └── nano-notes/             # Notes app
├── scripts/                    # Build and utility scripts
├── docs/                       # Architecture documentation
└── images/                     # Build output images (gitignored)
```

## Prerequisites

Ubuntu 22.04+ on aarch64 (native ARM64 host recommended). Install dependencies:

```bash
bash scripts/setup-host.sh --install
```

Or manually:

```bash
sudo apt install build-essential git wget curl rsync bc \
    libssl-dev libelf-dev flex bison cpio python3 file \
    qemu-system-aarch64 qemu-utils \
    libncurses-dev libwayland-dev wayland-protocols \
    libwlroots-dev libgtk-4-dev libadwaita-1-dev \
    libinput-dev libdrm-dev libgbm-dev libegl1-mesa-dev \
    libdbus-1-dev libglib2.0-dev device-tree-compiler \
    meson ninja-build pkg-config ccache
```

## Building

### Full System Image

```bash
# Initialize Buildroot submodule
git submodule update --init --recursive

# Build the complete image (first build takes 30-90 minutes)
bash scripts/build-image.sh

# Run in QEMU with graphical display
bash scripts/run-qemu.sh

# Or run in serial/headless mode
bash scripts/run-qemu.sh --serial
```

Login credentials: `root` / `nano`

SSH access: `ssh -p 2222 root@localhost`

### Individual Components (Host Build)

Build individual components on the host for development:

```bash
# Compositor
cd ui/nano-compositor && meson setup build && ninja -C build

# Shell
cd ui/nano-shell && meson setup build && ninja -C build

# Services
cd services/nano-settings && meson setup build && ninja -C build
cd services/nano-notif && meson setup build && ninja -C build
cd services/nano-power && meson setup build && ninja -C build
cd services/nano-net && meson setup build && ninja -C build

# Apps
cd apps/nano-clock && meson setup build && ninja -C build
cd apps/nano-calculator && meson setup build && ninja -C build
cd apps/nano-notes && meson setup build && ninja -C build
```

## QEMU Configuration

The QEMU launch script (`scripts/run-qemu.sh`) configures:
- Machine: `virt` with Cortex-A72 CPU
- Memory: 2GB, 4 SMP cores
- Graphics: `virtio-gpu-pci` with GTK display (GL acceleration)
- Network: User-mode with SSH port forwarding (host 2222 → guest 22)
- Input: `virtio-keyboard-pci` + `virtio-mouse-pci`

## System Services

All services communicate via D-Bus:

| Service | Bus Name | Bus Type |
|---------|----------|----------|
| Settings | `org.nano.Settings` | System |
| Notifications | `org.freedesktop.Notifications` | Session |
| Power | `org.nano.Power` | System |
| Network | `org.nano.Network` | System |

### Testing D-Bus Services

```bash
# Query settings
dbus-send --system --print-reply --dest=org.nano.Settings \
    /org/nano/Settings org.nano.Settings.Get \
    string:"display" string:"brightness"

# Send a notification
dbus-send --session --print-reply --dest=org.freedesktop.Notifications \
    /org/freedesktop/Notifications org.freedesktop.Notifications.Notify \
    string:"Test" uint32:0 string:"" string:"Hello" string:"World" \
    array:string:"" dict:string:variant: int32:5000

# Check battery
dbus-send --system --print-reply --dest=org.nano.Power \
    /org/nano/Power org.nano.Power.GetBatteryLevel
```

## Development Roadmap

- [x] Phase 0: Architecture design
- [x] Phase 1: Host environment setup
- [x] Phase 2: Buildroot configuration for QEMU aarch64
- [x] Phase 3: Custom rootfs with systemd service units
- [x] Phase 4: Wayland compositor (nano-compositor)
- [x] Phase 5: Mobile shell MVP (nano-shell)
- [x] Phase 6: System services (settings, notifications, power, network)
- [x] Phase 8: Demo applications (clock, calculator, notes)
- [ ] Phase 7: Packaging, OTA updates, logging
- [ ] Phase 9: Performance and security hardening
- [ ] Phase 10: ARM device porting (Raspberry Pi 4)

## License

MIT
