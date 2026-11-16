## Release Notes / 发布说明

### v0.2.0-beta.1

> Pre-release for validating the new offline WSL 2.2.1 package and the Windows 10 2004+ requirement.

- **WSL offline package** switched to **WSL 2.2.1** (kernel 5.15.150.1-2, ~128 MB) — meets usbipd's kernel >= 5.10.60.1 requirement with a much smaller download than WSL 3.0.5
- **Minimum Windows version** raised to **Windows 10 2004 (Build 19041)** / Windows 11 and enforced at startup
- **Readiness detection** no longer reports a false "reboot required" when WSL/VirtualMachinePlatform are already enabled; "Enable Pending" is detected correctly
- **MSI error reporting** now explains 1603/3010/1638 failures and gives actionable reboot guidance
- **WSL command handling** uses longer first-run timeouts and retries
- **Duplicate Alpine imports** are detected via `wsl -l` and treated as success
- **Log decoding** handles UTF-16LE, OEM/GBK and UTF-8 output instead of dumping hex
- **Tests/docs** updated for WSL/kernel version checks, DISM states and old `wsl.exe` help output

> 预发布版本：用于验证新的 WSL 2.2.1 离线包和 Windows 10 2004+ 最低版本要求。

- **WSL 离线包**切换为 **WSL 2.2.1**（内核 5.15.150.1-2，约 128 MB），满足 usbipd 对内核 ≥ 5.10.60.1 的要求，且比 WSL 3.0.5 小约 223 MB
- **最低 Windows 版本**提升为 **Windows 10 2004（Build 19041）** / Windows 11，并在启动时强制检查
- **就绪检测**修复了 WSL/虚拟化平台已启用时误报“需要重启”的问题，并正确识别“启用挂起”状态
- **MSI 错误提示**改进 1603/3010/1638 的解释和重启引导
- **WSL 命令执行**放宽首次超时并增加重试
- **Alpine 重复导入**通过 `wsl -l` 预检查并视为成功
- **日志解码**支持 UTF-16LE、OEM/GBK 和 UTF-8，不再输出十六进制
- **测试与文档**补充 WSL/内核版本、DISM 状态和旧版 `wsl.exe` 帮助页识别

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
