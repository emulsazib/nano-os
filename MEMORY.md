# NanoOS Project Memory

## Project: nano-os
Path: /Users/sajibmacmini/Documents/GitHub/nano-os
Goal: Custom mobile OS (Linux + Wayland + GTK4 shell) for QEMU aarch64 first, ARM hardware later

## Host Environment
- OS: macOS (darwin) on Apple Silicon
- Build target: Ubuntu aarch64 VM (for actual builds)
- QEMU: qemu-system-aarch64 (on build host)
- Buildroot: git submodule at buildroot/

## Key Architecture Decisions
- Init: systemd (required for Wayland seat/logind)
- Compositor: wlroots 0.17+ based custom (nano-compositor) with scene graph API
- Shell: GTK4 + libadwaita (nano-shell)
- Rootfs builder: Buildroot (submodule at buildroot/)
- Target: QEMU aarch64 virt machine first
- IPC: D-Bus (GDBus / GLib)
- All custom components: Meson build system

## Repository Structure
- buildroot/              → Buildroot submodule
- external/               → BR2_EXTERNAL layer (custom packages)
  - package/nano-*/       → Buildroot package definitions
- configs/buildroot/      → Buildroot defconfigs
- configs/kernel/         → Kernel config fragments
- overlays/rootfs/        → Rootfs overlay (systemd units, configs, D-Bus policies)
- ui/nano-compositor/     → wlroots compositor source (C)
- ui/nano-shell/          → GTK4 mobile shell source (C)
- services/nano-settings/ → Settings D-Bus service
- services/nano-notif/    → Notification daemon (freedesktop spec)
- services/nano-power/    → Power/battery service
- services/nano-net/      → Network status service
- apps/nano-clock/        → Clock/stopwatch/timer GTK4 app
- apps/nano-calculator/   → Calculator GTK4 app
- apps/nano-notes/        → Notes GTK4 app
- scripts/                → Build and utility scripts
- docs/                   → Architecture documentation

## Build Commands
- Host setup: bash scripts/setup-host.sh --install
- Build image: bash scripts/build-image.sh
- Run QEMU: bash scripts/run-qemu.sh
- Run serial: bash scripts/run-qemu.sh --serial
- SSH into guest: ssh -p 2222 root@localhost (password: nano)
- Build compositor on host: cd ui/nano-compositor && meson setup build/ && ninja -C build/
- Build shell on host: cd ui/nano-shell && meson setup build/ && ninja -C build/

## Current Status
- Phase 0-1: Architecture and environment ✓
- Phase 2-3: Buildroot config, rootfs overlay ✓ (files created, needs build)
- Phase 4: Compositor source complete ✓
- Phase 5: Shell source complete ✓
- Phase 6: All system services complete ✓
- Phase 8: Demo apps complete ✓
- Phase 7,9,10: Packaging, hardening, ARM porting (future)

## Important Notes
- external/buildroot/ was a stray clone (from original dev host) - should not exist
- The build must run on the aarch64 Ubuntu host, not macOS
- QEMU graphics: -device virtio-gpu-pci with Mesa virgl
- Kernel must have CONFIG_CGROUPS=y for systemd
- nano-shell uses GResource for CSS embedding
