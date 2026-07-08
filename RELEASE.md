## Release Notes / 发布说明

### v0.1.0

> First usable release. Deploy and mount EXT4 SD cards on Windows in fully offline environments.

- **Environment check** — 5-item health check on startup (virtualization, WSL feature, WSL2 kernel, Alpine distro, usbipd)
- **One-click offline setup** — place offline packages beside the exe and install all dependencies automatically
- **EXT4 mount orchestration** — usbipd bind → attach → kernel module load → block device detection → mount (rw first, fallback to ro)
- **Device hotplug detection** — polling-based USB device diffing with automatic UI refresh
- **Unified command execution** — long-running PowerShell process with marker protocol; all external processes injected via `ICommandExecutor`

---

首个可用版本，支持离线环境下一键部署并挂载 EXT4 格式 SD 卡。

- **环境检测** — 启动时自动检测五项状态（虚拟化、WSL、内核、Alpine、usbipd）
- **离线一键初始化** — exe 同目录放置离线包，自动安装所有依赖
- **EXT4 挂载编排** — bind → attach → 模块加载 → 设备检测 → mount，读写优先失败降级只读
- **设备热插拔检测** — 轮询比对 usbipd 设备列表，自动刷新 UI
