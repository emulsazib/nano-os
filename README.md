# NanoOS

A custom Linux-based mobile operating system built from scratch. Features a Wayland compositor, touch-first mobile shell, modular D-Bus system services, and a full device abstraction layer — all running on a custom Linux kernel configuration.

## Architecture

```
┌─────────────────────────────────────────────────────┐
│                  Demo Applications                  │
│          Clock  ·  Calculator  ·  Notes             │
│                   (GTK4 apps)                       │
├─────────────────────────────────────────────────────┤
│                   nano-shell                        │
│    Launcher · Status Bar · Notifications · Gestures │
│              (GTK4 + libadwaita)                    │
├─────────────────────────────────────────────────────┤
│                nano-compositor                      │
│  XDG Shell · Layer Shell · Touch · Auto-maximize    │
│           (wlroots 0.17+ scene-graph)               │
├──────────────────────┬──────────────────────────────┤
│   System Services    │    Device Abstraction        │
│  settings · notif    │  backlight · LED · haptic    │
│  power   · network   │  orientation · sensors       │
│      (GDBus)         │       (GDBus + sysfs)        │
├──────────────────────┴──────────────────────────────┤
│                    PipeWire                          │
│              (audio subsystem)                      │
├─────────────────────────────────────────────────────┤
│                     systemd                         │
│    logind · networkd · resolved · udev · journald   │
├─────────────────────────────────────────────────────┤
│                  Linux Kernel                       │
│  DRM/KMS · VirtIO · ALSA · Input · IIO · Power     │
├─────────────────────────────────────────────────────┤
│          QEMU aarch64 virt  /  ARM Hardware         │
└─────────────────────────────────────────────────────┘
```

## Boot Flow

```
QEMU virt (aarch64)
  → Linux kernel (arm64 defconfig + mobile/wayland/base fragments)
    → systemd (PID 1)
      → graphical.target
        → nano-session.target
          ├── nano-compositor    (Wayland compositor)
          ├── nano-shell         (mobile home screen)
          ├── nano-settings      (settings daemon)
          ├── nano-notif         (notification daemon)
          ├── nano-power         (battery/power)
          ├── nano-net           (network status)
          ├── nano-device        (backlight/LED/haptic/sensors)
          └── pipewire           (audio)
```

## Components

| Component | Type | LOC | Description |
|-----------|------|-----|-------------|
| `nano-compositor` | UI | ~1200 | wlroots Wayland compositor — XDG shell, layer shell, touch input, mobile auto-maximize, cursor/pointer, scene-graph rendering |
| `nano-shell` | UI | ~1200 | GTK4 mobile shell — app launcher with icon grid, status bar (clock/wifi/battery), notification overlay with swipe-to-dismiss, home indicator |
| `nano-settings` | Service | ~400 | D-Bus settings daemon — INI file backend, Get/Set/GetAll/Save methods, SettingChanged signals |
| `nano-notif` | Service | ~360 | Freedesktop Notifications 1.2 daemon — Notify, CloseNotification, auto-expiry |
| `nano-power` | Service | ~310 | Battery monitoring via sysfs, power profiles, shutdown/reboot |
| `nano-net` | Service | ~335 | Network interface monitoring, IP detection, WiFi scanning placeholder |
| `nano-device` | Service | ~530 | Device abstraction — backlight control, LED blink, haptic vibration, screen orientation, ambient light/proximity sensors |
| `nano-clock` | App | ~440 | Clock with stopwatch and countdown timer tabs |
| `nano-calculator` | App | ~880 | Calculator with unit converter, scratchpad, and history |
| `nano-notes` | App | ~455 | Note-taking app with file-backed storage |

## Project Structure

```
nano-os/
├── buildroot/                     # Buildroot submodule (kernel + rootfs)
├── configs/
│   ├── buildroot/
│   │   └── nano_qemu_aarch64_defconfig
│   └── kernel/fragments/
│       ├── base.config            # systemd, cgroups, VirtIO, crypto, filesystems
│       ├── mobile.config          # touch, backlight, LEDs, battery, WiFi/BT, sensors
│       └── wayland.config         # DRM/KMS, DMA-BUF, ALSA, GPU drivers
├── external/                      # BR2_EXTERNAL layer (10 packages)
│   ├── Config.in
│   ├── external.mk
│   ├── external.desc
│   └── package/
│       ├── nano-compositor/
│       ├── nano-shell/
│       ├── nano-settings/
│       ├── nano-notif/
│       ├── nano-power/
│       ├── nano-net/
│       ├── nano-device/
│       ├── nano-clock/
│       ├── nano-calculator/
│       └── nano-notes/
├── overlays/rootfs/               # Root filesystem overlay
│   └── etc/
│       ├── nano/                  # compositor.conf, settings.conf
│       ├── systemd/
│       │   ├── system/            # 9 service units + session target
│       │   ├── logind.conf.d/     # mobile power/idle config
│       │   ├── network/           # DHCP for wired + wireless
│       │   ├── resolved.conf.d/   # DNS fallback config
│       │   └── sleep.conf.d/      # suspend-to-RAM config
│       ├── dbus-1/system.d/       # 5 D-Bus policy files
│       ├── udev/rules.d/         # mobile device permissions
│       ├── tmpfiles.d/           # runtime directory setup
│       ├── pipewire/             # PipeWire audio config
│       ├── profile.d/            # shell environment (nano-env.sh)
│       └── environment           # system-wide env vars
├── ui/
│   ├── nano-compositor/           # wlroots compositor source
│   │   ├── src/ (main, server, output, input, xdg, layer)
│   │   ├── include/nano-compositor.h
│   │   └── meson.build
│   └── nano-shell/                # GTK4 shell source
│       ├── src/ (main, shell, statusbar, launcher, app-entry, notifications, clock)
│       ├── data/ (style.css, gresource.xml)
│       └── meson.build
├── services/
│   ├── nano-settings/             # Settings D-Bus service
│   ├── nano-notif/                # Notification D-Bus service
│   ├── nano-power/                # Power D-Bus service
│   ├── nano-net/                  # Network D-Bus service
│   └── nano-device/               # Device abstraction D-Bus service
├── apps/
│   ├── nano-clock/                # Clock/stopwatch/timer
│   ├── nano-calculator/           # Calculator + unit converter
│   └── nano-notes/                # Note-taking app
├── scripts/
│   ├── setup-host.sh              # Host dependency checker/installer
│   ├── build-image.sh             # Buildroot build orchestrator
│   ├── run-qemu.sh                # QEMU launcher (graphical + serial modes)
│   ├── post-build.sh              # Rootfs post-build hook (systemd wiring)
│   └── enter-rootfs.sh            # Chroot into built rootfs
├── docs/                          # Architecture documentation
└── images/                        # Build outputs (gitignored)
```

## Prerequisites

**Host:** Ubuntu 22.04+ on aarch64 (native ARM64). Also works on x86_64 with cross-compilation.

```bash
# Check host readiness
bash scripts/setup-host.sh

# Install all dependencies
bash scripts/setup-host.sh --install
```

Or install manually:

```bash
sudo apt install -y \
    build-essential git wget curl rsync bc cpio python3 file \
    libssl-dev libelf-dev flex bison libncurses-dev \
    qemu-system-aarch64 qemu-utils \
    libwayland-dev wayland-protocols libwlroots-dev \
    libgtk-4-dev libadwaita-1-dev \
    libinput-dev libxkbcommon-dev libpixman-1-dev \
    libdrm-dev libgbm-dev libegl1-mesa-dev \
    libdbus-1-dev libglib2.0-dev \
    device-tree-compiler meson ninja-build pkg-config ccache
```

## Building

### Full System Image

```bash
# 1. Initialize Buildroot submodule
git submodule update --init --recursive

# 2. Build complete image (first build: 30-90 min)
./scripts/build-image.sh

# 3. Boot in QEMU with graphical display
./scripts/run-qemu.sh

# Or boot in serial/headless mode
./scripts/run-qemu.sh --serial
```

**Login:** `root` / `nano`
**SSH:** `ssh -p 2222 root@localhost`

### Build Options

```bash
# Clean build
./scripts/build-image.sh --clean

# Open Buildroot menuconfig
./scripts/build-image.sh --menuconfig

# Set parallel jobs
./scripts/build-image.sh --jobs 8
```

### Individual Components (Host Development)

Build components on the host for faster iteration:

```bash
# Compositor
cd ui/nano-compositor && meson setup build && ninja -C build

# Shell
cd ui/nano-shell && meson setup build && ninja -C build

# Services
cd services/nano-settings && meson setup build && ninja -C build
cd services/nano-device && meson setup build && ninja -C build

# Apps
cd apps/nano-clock && meson setup build && ninja -C build
cd apps/nano-calculator && meson setup build && ninja -C build
cd apps/nano-notes && meson setup build && ninja -C build
```

## QEMU Configuration

The QEMU launcher (`scripts/run-qemu.sh`) provides:

| Feature | Configuration |
|---------|--------------|
| Machine | `virt` with Cortex-A72 CPU |
| Memory | 2 GB, 4 SMP cores |
| Graphics | `virtio-gpu-pci` with GTK+GL display |
| Network | User-mode, SSH forwarding (host 2222 → guest 22) |
| Input | `virtio-keyboard-pci` + `virtio-mouse-pci` |
| Storage | `virtio-blk-pci` (rootfs.ext4) |
| RNG | `virtio-rng-pci` |

## Device Configuration

### Host Device

The host machine is used for development, cross-compilation, and running NanoOS in QEMU emulation.

| Property | Specification |
|----------|---------------|
| Architecture | x86_64 or aarch64 (native ARM64) |
| OS | Ubuntu 22.04+ / Debian-based Linux |
| Role | Development, cross-compilation, QEMU emulation |
| CPU | Multi-core (4+ cores recommended) |
| RAM | 8 GB minimum (16 GB recommended for parallel builds) |
| Disk | 20 GB+ free space for Buildroot toolchain and build artifacts |
| Emulation | QEMU `aarch64 virt` machine with Cortex-A72, VirtIO peripherals |
| Display | GTK+GL for graphical QEMU output, or headless via serial console |
| Network | User-mode networking with SSH port forwarding (host 2222 → guest 22) |

### Clinic Device (Mobile / ARM Architecture)

NanoOS targets mobile and embedded ARM devices used in clinical and field environments.

#### Mobile Device

| Property | Specification |
|----------|---------------|
| Architecture | ARM64 (aarch64) |
| Form Factor | Handheld / tablet / mobile terminal |
| Display | Touchscreen with DRM/KMS, auto-rotate via IIO accelerometer |
| Input | Multi-touch capacitive touchscreen, on-screen keyboard |
| GPU | Mali (Panfrost/Lima) or Broadcom VideoCore (VC4) via DRM |
| Audio | PipeWire at 48 kHz / 1024 quantum for low-latency playback |
| Connectivity | WiFi (802.11), Bluetooth, USB gadget mode |
| Power | Battery-backed with sysfs monitoring, suspend-to-RAM, idle timeout (5 min) |
| Sensors | Ambient light (IIO), proximity, accelerometer (orientation) |
| Feedback | LED indicator (blink control), haptic vibration motor |
| Backlight | sysfs-based brightness control (0–255) |

#### ARM Architecture Device (Raspberry Pi 4 / SBC)

| Property | Specification |
|----------|---------------|
| Architecture | ARM64 (aarch64), Cortex-A72 quad-core |
| Target Board | Raspberry Pi 4 Model B (primary), generic ARM64 SBCs |
| RAM | 2 GB – 8 GB (depending on board variant) |
| Storage | microSD card or eMMC with ext4 root filesystem |
| Display | HDMI or DSI touchscreen panel via DRM/KMS |
| GPU | Broadcom VideoCore VI (VC4 driver) |
| Network | Gigabit Ethernet, WiFi 802.11ac, Bluetooth 5.0 |
| USB | USB 3.0 / USB 2.0 host ports, USB-C power |
| Boot | U-Boot or direct kernel boot with device tree |
| Peripherals | GPIO, I2C, SPI for external sensors and actuators |
| Power | 5V / 3A USB-C supply, no battery (external UPS optional) |
| Kernel Fragments | `base.config` + `mobile.config` + `wayland.config` merged with board defconfig |

## Kernel Configuration

The kernel is built from the standard arm64 `defconfig` with three config fragment overlays merged automatically by Buildroot:

| Fragment | Options | Purpose |
|----------|---------|---------|
| `base.config` | ~100 | Preemption, cgroups, namespaces, VirtIO, DRM, crypto, filesystems, networking, systemd requirements |
| `mobile.config` | ~60 | Touchscreen, backlight, LEDs, battery/PSU, WiFi/BT, USB gadget, CPU freq governors, IIO sensors, thermal |
| `wayland.config` | ~30 | DRM/KMS, DMA-BUF heaps, ALSA sound core, virtio-sound, V4L2 camera, Panfrost/Lima/VC4 GPU drivers |

## System Services

All services run under `nano-session.target` and communicate via D-Bus:

| Service | Bus Name | Bus Type | D-Bus Interface |
|---------|----------|----------|-----------------|
| Settings | `org.nano.Settings` | System | Get, Set, GetAll, Save |
| Notifications | `org.freedesktop.Notifications` | Session | Notify, CloseNotification, GetCapabilities |
| Power | `org.nano.Power` | System | GetBatteryLevel, GetBatteryState, SetPowerProfile, Shutdown, Reboot |
| Network | `org.nano.Network` | System | GetStatus, GetInterfaces, GetWifiNetworks |
| Device | `org.nano.Device` | System | Backlight, LED, Haptic, Orientation, Sensors (5 sub-interfaces) |

### Testing D-Bus Services

```bash
# Read a setting
dbus-send --system --print-reply --dest=org.nano.Settings \
    /org/nano/Settings org.nano.Settings.Get \
    string:"display" string:"brightness"

# Set brightness via device daemon
dbus-send --system --print-reply --dest=org.nano.Device \
    /org/nano/Device org.nano.Device.Backlight.SetBrightness \
    int32:128

# Send a notification
dbus-send --session --print-reply --dest=org.freedesktop.Notifications \
    /org/freedesktop/Notifications org.freedesktop.Notifications.Notify \
    string:"Test" uint32:0 string:"" string:"Hello" string:"World" \
    array:string:"" dict:string:variant: int32:5000

# Check battery level
dbus-send --system --print-reply --dest=org.nano.Power \
    /org/nano/Power org.nano.Power.GetBatteryLevel

# Get screen orientation
dbus-send --system --print-reply --dest=org.nano.Device \
    /org/nano/Device org.nano.Device.Orientation.GetOrientation

# Vibrate for 200ms
dbus-send --system --print-reply --dest=org.nano.Device \
    /org/nano/Device org.nano.Device.Haptic.Vibrate int32:200
```

## Mobile Stack

The rootfs overlay configures a complete mobile Linux stack:

| Layer | Config Files | Purpose |
|-------|-------------|---------|
| logind | `logind.conf.d/nano-mobile.conf` | Power key handling, idle suspend after 5 min, single VT |
| Autologin | `serial-getty@ttyAMA0.service.d/autologin.conf` | Root auto-login on QEMU serial console |
| udev | `udev/rules.d/90-nano-mobile.rules` | DRM/GPU, backlight, LED, sound, IIO sensor, touchscreen permissions |
| tmpfiles | `tmpfiles.d/nano-os.conf` | `/run/user/0`, `/run/nano`, `/var/lib/nano` runtime dirs |
| Audio | `pipewire/pipewire.conf.d/nano-audio.conf` | Low-latency PipeWire config (48kHz, 1024 quantum) |
| Network | `systemd/network/{20-wired,25-wireless}.network` | DHCP on Ethernet and WiFi interfaces |
| DNS | `resolved.conf.d/nano-dns.conf` | Fallback DNS (1.1.1.1, 8.8.8.8), mDNS enabled |
| Sleep | `sleep.conf.d/nano-sleep.conf` | Suspend-to-RAM, no hibernate |

## Project Stats

- **30 C/H source files** — 6,729 lines of C code
- **10 meson.build files** — all components use Meson + Ninja
- **10 Buildroot packages** — integrated via BR2_EXTERNAL
- **9 systemd service units** + 1 session target
- **27 rootfs overlay config files**
- **3 kernel config fragments** — ~190 kernel options

## Development Roadmap

- [x] Phase 0: Architecture design
- [x] Phase 1: Host environment setup
- [x] Phase 2: Bootable QEMU Linux image (Buildroot + kernel config fragments)
- [x] Phase 3: Custom rootfs (systemd units, udev, tmpfiles, logind, networking)
- [x] Phase 4: Wayland compositor (nano-compositor with wlroots)
- [x] Phase 5: Mobile shell MVP (nano-shell with GTK4)
- [x] Phase 6: System services (settings, notifications, power, network, device)
- [x] Phase 7: Audio stack (PipeWire)
- [x] Phase 8: Demo applications (clock, calculator, notes)
- [ ] Phase 9: Performance and security hardening
- [ ] Phase 10: ARM device porting (Raspberry Pi 4)

## License

MIT
