#pragma once
#include <string>

enum class ErrorCode {
    Success = 0,
    // 环境检测
    WslNotInstalled,
    WslVersionNot2,
    WslNoDistribution,
    UsbipdNotInstalled,
    // 命令执行
    CommandTimeout,
    CommandFailed,
    ProcessCreateFailed,
    // usbipd 操作
    UsbipdAttachFailed,
    UsbipdDetachFailed,
    UsbipdBindFailed,
    UsbipdUnbindFailed,
    DeviceAlreadyAttached,
    DeviceNotFound,
    // WSL 操作
    MountFailed,
    UnmountFailed,
    DeviceNotExt4,
    SudoAuthFailed,
    // 设备
    DeviceDisconnected,
    WslCrash,
    // 通用
    InvalidParameter,
    NotImplemented,
};

struct Result {
    ErrorCode code;
    std::string message;

    bool IsSuccess() const { return code == ErrorCode::Success; }
    static Result Ok() { return { ErrorCode::Success, "" }; }
    static Result Fail(ErrorCode ec, const std::string& msg = "") {
        return { ec, msg };
    }
};
