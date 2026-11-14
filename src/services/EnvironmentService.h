#pragma once
#include <string>
#include "../command/ICommandExecutor.h"

// ── 环境检测结果类型 ──────────────────────────────────────────────────────

enum class CheckState {
    Unknown,         // 尚未检测
    NotInstalled,    // 未安装
    VersionMismatch, // 已安装但版本不满足
    Ready            // 已就绪
};

// dism /get-featureinfo 报告的功能状态
enum class DismFeatureState {
    Unknown,
    Disabled,
    Enabled,
    EnablePending,   // 已启用但等待重启（State: Enable Pending / 状态: 启用挂起）
    DisablePending
};

// 虚拟化检查（VT-x/AMD-V 是否在固件中启用）
struct VirtCheckResult {
    CheckState state = CheckState::Unknown;
    bool isEnabled = false;
    bool needsReboot = false;   // 功能已启用，但组件需要重启后才生效
    std::string detail;
    bool isReady() const { return state == CheckState::Ready; }
};

// WSL Windows 功能检查
struct WslFeatureCheckResult {
    CheckState state = CheckState::Unknown;
    bool isEnabled = false;
    bool needsReboot = false;   // 功能已启用，但组件需要重启后才生效
    std::string detail;
    bool isReady() const { return state == CheckState::Ready; }
};

struct WslCheckResult {
    CheckState state = CheckState::Unknown;
    std::string version;        // e.g. "2.0.9"
    std::string kernelVersion;  // e.g. "5.15.133.1-1"
    std::string detail;         // 展示文本

    bool isReady() const { return state == CheckState::Ready; }
};

struct AlpineCheckResult {
    CheckState state = CheckState::Unknown;
    std::string version;  // e.g. "3.20"
    std::string detail;

    bool isReady() const { return state == CheckState::Ready; }
};

struct UsbipdCheckResult {
    CheckState state = CheckState::Unknown;
    std::string version;
    std::string detail;

    bool isReady() const { return state == CheckState::Ready; }
};

/// 环境检测结果（一次性返回五项状态）
struct EnvironmentStatus {
    VirtCheckResult      virt;         // 虚拟化使能
    WslFeatureCheckResult wslFeature;  // WSL 功能使能
    WslCheckResult       wsl;          // WSL2 内核
    AlpineCheckResult    alpine;       // Alpine 发行版
    UsbipdCheckResult    usbipd;       // usbipd-win

    bool allReady() const {
        return virt.isReady() && wslFeature.isReady() &&
               wsl.isReady() && alpine.isReady() && usbipd.isReady();
    }
};

/// 环境检测服务接口（可 mock）
class IEnvironmentService {
public:
    virtual ~IEnvironmentService() = default;
    virtual EnvironmentStatus CheckAll() = 0;
};

/// 环境检测服务实现
/// 注入 ICommandExecutor*，通过外部进程检测各项环境状态
class EnvironmentService : public IEnvironmentService {
public:
    explicit EnvironmentService(ICommandExecutor* executor);
    EnvironmentStatus CheckAll() override;

    // 版本/输出解析（public static 便于单元测试）
    static DismFeatureState ParseDismFeatureState(const std::string& dismOutput);
    static bool IsWslVersionSupported(const std::string& version);
    static bool IsKernelVersionSupported(const std::string& kernelVersion);
    static bool LooksLikeWslHelpOutput(const std::string& output);

private:
    VirtCheckResult      CheckVirtualization();
    WslFeatureCheckResult CheckWslFeature();
    WslCheckResult       CheckWsl();
    AlpineCheckResult    CheckAlpine();
    UsbipdCheckResult    CheckUsbipd();

    // WSL 首次调用（VM 冷启动）可能远超 10s：首次 60s，后续 15s，并做一次超时重试
    CommandResult RunWsl(const std::string& commandLine);

    ICommandExecutor* executor_;
    bool wslWarmedUp_ = false;
};
