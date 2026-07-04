#pragma once
#include <string>
#include <windows.h>

struct CommandResult {
    DWORD exitCode;
    std::string stdOut;
    std::string stdErr;
};

class ICommandExecutor {
public:
    virtual ~ICommandExecutor() = default;

    // 同步执行命令，可通过 stdinInput 向子进程标准输入写入内容（用于 sudo -S）
    virtual CommandResult Execute(
        const std::string& commandLine,
        const std::string& stdinInput = "",
        DWORD timeoutMs = 30000) = 0;

    // 启动后台进程，返回进程句柄（不等待）
    virtual HANDLE StartBackground(const std::string& commandLine) = 0;

    // 检查执行环境是否可用
    virtual bool IsAvailable() = 0;
};
