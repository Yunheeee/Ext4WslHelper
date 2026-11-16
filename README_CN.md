# Ext4WslHelper

> 在 Windows 上通过 WSL2 + usbipd 访问 EXT4 文件系统格式的 SD 卡。免安装，单文件运行。

[English](README.md)

packages/ 目录下为所有依赖的安装包，可离线使用。

## 使用方法
直接运行 Ext4WslHelper.exe，按提示操作即可：

1. 初始化环境  →  自动安装依赖包、配置 WSL2 内核、安装 Alpine 发行版
2. 挂载 SD 卡  →  通过 usbipd 连接 USB 设备并挂载 EXT4 分区
3. 卸载 SD 卡  →  安全卸载并断开设备连接
挂载成功后，复制程序给出的 Windows 网络路径（如 \\wsl.localhost\Alpine\mnt\sdcard），粘贴到浏览器地址栏即可访问 SD 卡文件 📂

>⚠️ 首次使用需要管理员权限运行，用于安装驱动和 WSL2 内核更新。

## 文件说明
| 文件 | 说明 |
|------|------|
| Ext4WslHelper.exe| 🚀 主程序 — 直接运行，自动处理所有依赖 |
| packages/wsl.2.2.1.0.x64.msi |	WSL 2.2.1 安装包（内核 5.15.150.1-2，≥ 5.10.60.1）|
| packages/usbipd-win_5.3.0_x64.msi | USB/IP 工具，用于 WSL2 共享 USB 设备 |
| packages/Alpine.zip |	Alpine Linux WSL 发行版（挂载环境）|

## 原理

```
SD 卡 (EXT4)  ──usbipd──▶  WSL2 虚拟机  ──mount──▶  \\wsl.localhost\（网络路径）
```

1. `usbipd` 将物理 SD 卡从 Windows 侧绑定到 WSL2 虚拟机
2. WSL2 内 Alpine 发行版挂载 EXT4 分区
3. 通过 `\\wsl.localhost\` 网络路径在 Windows 端透明访问

## 环境要求

| 组件 | 说明 |
|------|------|
| Windows 10 2004+ (x64/ARM64) | WSL 2.x 安装包要求 |
| 管理员权限 | usbipd 设备绑定需要 |

程序启动后自动检测环境状态，缺少的组件可通过 **一键初始化** 离线安装。

## License

MIT
