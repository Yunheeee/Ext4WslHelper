#pragma once
#include "ICommandExecutor.h"
#include "CommandWindow.h"
#include <memory>
#include <functional>

class PowershellExecutor : public ICommandExecutor {
public:
    explicit PowershellExecutor(bool showWindow = false);
    ~PowershellExecutor() override;

    CommandResult Execute(const std::string& commandLine,
                          const std::string& stdinInput = "",
                          DWORD timeoutMs = 30000) override;

    HANDLE StartBackground(const std::string& commandLine) override;
    bool IsAvailable() override;

    // 每条解码行回调（debug 窗口通过此接口接收 executor 命令输出）
    void SetOutputSink(std::function<void(const std::string&)> cb) { onLine_ = std::move(cb); }

private:
    CommandResult ExecuteInternal(const std::string& cmdLine,
                                  const std::string& stdinInput, DWORD timeoutMs);
    bool EnsureStarted();
    CommandResult ExecuteWithMarkers(const std::string& cmdLine,
                                     const std::string& stdinInput, DWORD timeoutMs);

    std::unique_ptr<CommandWindow> window_;
    bool showWindow_;
    std::function<void(const std::string&)> onLine_;
};
