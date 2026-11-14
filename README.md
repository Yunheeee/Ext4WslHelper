# Ext4WslHelper

> Access EXT4 SD cards on Windows via WSL2 + usbipd. No install — single exe.

[中文](README_CN.md)

The `packages/` directory contains all dependency installers for offline use.

## Usage

Run `Ext4WslHelper.exe` and follow the on-screen steps:

1. **Initialize** — auto-install dependencies, WSL2 kernel, and Alpine distro
2. **Mount** — connect the USB device via usbipd and mount the EXT4 partition
3. **Unmount** — safely unmount and disconnect

Once mounted, copy the Windows network path (e.g. `\\wsl.localhost\Alpine\mnt\sdcard`) into File Explorer's address bar to browse the SD card.

> ⚠️ First run requires administrator privileges for driver installation and WSL2 kernel updates.

## Package Contents

| File | Description |
|------|-------------|
| `Ext4WslHelper.exe` | Main program — just run it |
| `packages/wsl.3.0.5.0.x64.msi` | WSL 3.0.5 installer (kernel >= 5.10.60.1) |
| `packages/usbipd-win_5.3.0_x64.msi` | USB/IP tools for sharing USB devices with WSL2 |
| `packages/Alpine.zip` | Alpine Linux WSL distro (mount environment) |

## How It Works

```
SD card (EXT4)  ──usbipd──▶  WSL2 VM  ──mount──▶  \\wsl.localhost\  (network path)
```

1. `usbipd` binds the physical SD card from Windows to the WSL2 VM
2. Alpine inside WSL2 mounts the EXT4 partition
3. The mount point is exposed via `\\wsl.localhost\` for transparent access from Windows

## Requirements

| Component | Notes |
|-----------|-------|
| Windows 10 1903+ (x64) / 2004+ (ARM64) | Built-in WSL2 support |
| Administrator privileges | Required for usbipd device binding |

The program auto-detects your environment status on startup. Missing components can be installed offline with one click.

## License

MIT
