#pragma once
#include "ICommandExecutor.h"
#include "CommandWindow.h"
#include <memory>
#include <string>

class WslExecutor : public ICommandExecutor {
public:
    explicit WslExecutor(const std::string& distro, bool showWindow = false);
    ~WslExecutor() override;

    CommandResult Execute(const std::string& commandLine,
                          const std::string& stdinInput = "",
                          DWORD timeoutMs = 30000) override;

    HANDLE StartBackground(const std::string& commandLine) override;
    bool IsAvailable() override;

    std::string GetDistro() const { return distro_; }

    void SetPersistentWindow(CommandWindow* cw) { externalWindow_ = cw; }

private:
    CommandResult ExecuteInternal(const std::string& wslCommand,
                                  const std::string& stdinInput,
                                  DWORD timeoutMs);
    bool EnsureStarted();
    CommandResult ExecuteWithMarkers(const std::string& cmdLine,
                                     const std::string& stdinInput,
                                     DWORD timeoutMs);
    CommandWindow* ActiveWindow() { return externalWindow_ ? externalWindow_ : window_.get(); }

    std::string distro_;
    bool showWindow_;
    std::unique_ptr<CommandWindow> window_;
    CommandWindow* externalWindow_ = nullptr;
};
