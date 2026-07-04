#pragma once
#include "../command/ICommandExecutor.h"
#include <string>
#include <vector>

// usbipd 状态枚举
enum class UsbipdState {
    Unknown,            // 初始状态，未检测
    NotInstalled,       // usbipd 未安装
    ServiceStopped,     // 服务已安装但未运行
    ServiceRunning,     // 服务运行中，无设备操作
    DeviceRecognized,   // 已识别到 USB 设备（list 中有设备）
    DeviceBound,        // 设备已 bind（跨重启持久化）
    DeviceAttached,     // 设备已 attach 到 WSL
    DeviceUnbound,      // 设备已 unbind
    Error               // 操作出错
};

inline const char* UsbipdStateToString(UsbipdState s) {
    switch (s) {
        case UsbipdState::Unknown:          return "未知";
        case UsbipdState::NotInstalled:     return "未安装";
        case UsbipdState::ServiceStopped:   return "服务已停止";
        case UsbipdState::ServiceRunning:   return "服务运行中";
        case UsbipdState::DeviceRecognized: return "已识别设备";
        case UsbipdState::DeviceBound:      return "已绑定(bind)";
        case UsbipdState::DeviceAttached:   return "已附加(attach)";
        case UsbipdState::DeviceUnbound:    return "已解绑(unbind)";
        case UsbipdState::Error:            return "出错";
    }
    return "未知";
}

// 分类后的 usbipd 错误信息
struct UsbipdErrorInfo {
    std::string errorType;       // 错误类型描述
    std::string matchedKeyword;  // 匹配到的关键字
    std::string suggestion;      // 建议的下一步操作
    std::string rawOutput;       // 原始输出（便于调试）
};

// 错误关键字表条目
struct UsbipdErrorEntry {
    const char* keyword;         // 在输出中搜索的关键字
    const char* errorType;       // 错误类型
    const char* suggestion;      // 建议操作
};

// 错误关键字表在 UsbipdManager.cpp 中定义（人为可审阅、可变更）
struct UsbipdErrorEntry;
extern const UsbipdErrorEntry kUsbipdErrorTable[];
extern const int kUsbipdErrorTableSize;

struct UsbipdDeviceInfo {
    std::string busId;         // "5-2"
    std::string vidPid;        // "1234:5678"
    std::string description;   // "USB Mass Storage Device (SD Card Reader)"
    std::string state;         // "Not attached", "Attached - Ubuntu"
    std::string wslDistro;     // parsed from state if attached

    bool IsAttached() const { return state.find("Attached") != std::string::npos; }
};

class UsbipdManager {
public:
    explicit UsbipdManager(ICommandExecutor* executor);

    // 枚举所有 USB 设备（含已 attach 到 WSL 的）
    std::vector<UsbipdDeviceInfo> List();

    // attach 设备到指定 WSL 发行版
    bool Attach(const std::string& busId, const std::string& distro = "");

    // 从 WSL detach 设备
    bool Detach(const std::string& busId);

    // bind 设备到 usbipd（跨重启持久化）
    bool Bind(const std::string& busId);

    // unbind 设备
    bool Unbind(const std::string& busId);

    // === 错误分类与状态查询 ===

    // 对命令执行结果进行错误分类，返回结构化的错误信息
    static UsbipdErrorInfo ClassifyError(const CommandResult& result);

    // 获取当前 usbipd 总体状态
    UsbipdState GetState() const { return state_; }

    // 获取可读的状态描述
    std::string GetStateString() const { return UsbipdStateToString(state_); }

    // 获取上次操作错误信息
    UsbipdErrorInfo GetLastError() const { return lastError_; }

    // 解析 usbipd list 输出的单行（共享解析入口，DeviceManager 也使用）
    static UsbipdDeviceInfo ParseDeviceLine(const std::string& line);

private:
    ICommandExecutor* executor_;

    UsbipdState state_ = UsbipdState::Unknown;
    UsbipdErrorInfo lastError_;
};
