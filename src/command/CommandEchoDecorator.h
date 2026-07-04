#pragma once
#include "ICommandExecutor.h"
#include "../common/Logger.h"
#include <string>

// ICommandExecutor 装饰器 — 在执行命令前通过 Logger 记录带标签的命令行。
class CommandEchoDecorator : public ICommandExecutor {
public:
    explicit CommandEchoDecorator(ICommandExecutor* inner,
                                   const std::string& label = "")
        : inner_(inner), label_(label) {}

    CommandResult Execute(const std::string& commandLine,
                          const std::string& stdinInput = "",
                          DWORD timeoutMs = 30000) override {
        Logger::Instance().Info("==== " + label_ + " ==== # " + commandLine);
        return inner_->Execute(commandLine, stdinInput, timeoutMs);
    }

    HANDLE StartBackground(const std::string& commandLine) override {
        Logger::Instance().Info("==== " + label_ + " ==== # " + commandLine + " (bg)");
        return inner_->StartBackground(commandLine);
    }

    bool IsAvailable() override { return inner_->IsAvailable(); }

private:
    ICommandExecutor* inner_;
    std::string label_;
};
