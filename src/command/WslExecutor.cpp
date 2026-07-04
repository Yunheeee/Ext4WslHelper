#include "WslExecutor.h"
#include "../common/Logger.h"
#include "../common/Utils.h"
#include "CommandWindow.h"
#include <sstream>
#include <cstring>

WslExecutor::WslExecutor(const std::string& distro, bool showWindow)
    : distro_(distro), showWindow_(showWindow) {
}

WslExecutor::~WslExecutor() {
    if (window_) {
        window_->WriteLine("exit");
        window_->WaitForExit(3000);
        if (window_->IsRunning()) {
            window_->Terminate();
            window_->WaitForExit(1000);
        }
        window_->Close();
    }
}

// ─── 公共接口 ────────────────────────────────────────────────────────────────

CommandResult WslExecutor::Execute(const std::string& commandLine,
                                   const std::string& stdinInput,
                                   DWORD timeoutMs) {
    if (showWindow_) {
        return ExecuteInternal(commandLine, stdinInput, timeoutMs);
    }
    if (!EnsureStarted()) {
        CommandResult cr = { (DWORD)-1, "", "" };
        cr.stdErr = "Failed to start persistent WSL process";
        Logger::Instance().Error("[WslExecutor] " + cr.stdErr);
        return cr;
    }
    return ExecuteWithMarkers(commandLine, stdinInput, timeoutMs);
}

HANDLE WslExecutor::StartBackground(const std::string& commandLine) {
    std::string fullCmd = "wsl.exe -d " + distro_ + " -- " + commandLine;
    std::wstring wCmd = utils::Utf8ToWString(fullCmd);

    CommandWindow window;
    if (!window.Create(wCmd, showWindow_, false)) {
        return INVALID_HANDLE_VALUE;
    }
    return window.GetProcessHandle();
}

bool WslExecutor::IsAvailable() {
    auto result = Execute("echo available", "", 10000);
    return result.exitCode == 0;
}

// ─── 长驻进程管理 ────────────────────────────────────────────────────────────

bool WslExecutor::EnsureStarted() {
    if (externalWindow_) {
        if (externalWindow_->IsRunning()) return true;
        Logger::Instance().Error("[WslExecutor] external window not running");
        return false;
    }

    if (window_) {
        if (window_->IsRunning()) return true;
        window_->Close();
        window_.reset();
    }

    // 检测 --cd ~ 是否受支持（缓存结果）
    static int s_cdSupported = -1;
    if (s_cdSupported == -1) {
        std::wstring testDistro = utils::Utf8ToWString(distro_);
        auto testResult = utils::RunProcess(L"wsl.exe",
            L"--cd ~ -d " + testDistro + L" -- echo ok", "", 5000);
        s_cdSupported = (testResult.exitCode == 0) ? 1 : 0;
        Logger::Instance().Debug("[WslExecutor] --cd ~ " +
            std::string(s_cdSupported ? "supported" : "not supported, using default CWD"));
    }

    window_ = std::make_unique<CommandWindow>();
    std::string wslCmd = s_cdSupported
        ? "wsl.exe --cd ~ -d " + distro_
        : "wsl.exe -d " + distro_;
    std::wstring wCmd = utils::Utf8ToWString(wslCmd);
    if (!window_->Create(wCmd, false, true)) {
        Logger::Instance().Error("[WslExecutor] Failed to create persistent process");
        window_.reset();
        return false;
    }
    // 确保 shell 已就绪
    window_->WriteLine("echo ready");
    Sleep(300);
    window_->ReadAllRaw(1000);
    Logger::Instance().Debug("[WslExecutor] Persistent process started: " + wslCmd);
    return true;
}

// ─── 标记协议 ────────────────────────────────────────────────────────────────

CommandResult WslExecutor::ExecuteWithMarkers(const std::string& cmdLine,
                                               const std::string& stdinInput,
                                               DWORD timeoutMs) {
    CommandResult result = { (DWORD)-1, "", "" };

    // 构建带标记的包裹命令
    const char* kStart = "###X4W_CSTART###";
    const char* kEnd = "###X4W_CEND###";
    std::string wrappedCmd =
        std::string("echo ") + kStart + "\n" +
        cmdLine + "\n" +
        std::string("echo ") + kEnd + "$?\n";

    ActiveWindow()->WriteLine(wrappedCmd);

    if (!stdinInput.empty()) {
        ActiveWindow()->WriteLine(stdinInput);
    }

    std::string raw = ActiveWindow()->ReadAllRaw(timeoutMs);
    if (raw.empty()) {
        result.stdErr = "Command timed out or no data";
        return result;
    }

    // 构建两种编码的标记模式（统一处理 ANSI / UTF-16LE）
    static const size_t kStartLen = strlen(kStart); // 16
    static const size_t kEndLen   = strlen(kEnd);   // 14
    static const std::string kStart16 = []() -> std::string {
        std::string s; for (char c : std::string("###X4W_CSTART###")) { s += c; s += '\0'; } return s;
    }();
    static const std::string kEnd16 = []() -> std::string {
        std::string s; for (char c : std::string("###X4W_CEND###")) { s += c; s += '\0'; } return s;
    }();

    size_t startPos = raw.find(kStart);
    bool startIs16 = false;
    if (startPos == std::string::npos) {
        startPos = raw.find(kStart16);
        startIs16 = true;
    }
    size_t endPos = raw.rfind(kEnd);
    bool endIs16 = false;
    if (endPos == std::string::npos) {
        endPos = raw.rfind(kEnd16);
        endIs16 = true;
    }

    size_t startMarkerLen = startIs16 ? kStart16.size() : kStartLen;
    size_t endMarkerLen   = endIs16   ? kEnd16.size()   : kEndLen;

    std::string content;
    if (startPos != std::string::npos) {
        startPos += startMarkerLen;
        while (startPos < raw.size() &&
               (raw[startPos] == '\r' || raw[startPos] == '\n' || raw[startPos] == '\0'))
            startPos++;
        content = (endPos != std::string::npos)
            ? raw.substr(startPos, endPos - startPos)
            : raw.substr(startPos);
    } else if (endPos != std::string::npos) {
        content = raw.substr(0, endPos);
    } else {
        content = raw;
    }

    // 编码转换 + 分割行 + 日志
    auto lines = CommandWindow::ProcessAndLog(content);
    for (const auto& line : lines) {
        if (!result.stdOut.empty()) result.stdOut += "\n";
        result.stdOut += line;
    }

    // 解析退出码
    if (endPos != std::string::npos) {
        std::string after = raw.substr(endPos + endMarkerLen);
        int code = -1;
        if (endIs16) {
            size_t p = 0;
            while (p < after.size() && !(after[p] >= '0' && after[p] <= '9')) p++;
            if (p < after.size()) {
                code = 0;
                while (p < after.size() && after[p] >= '0' && after[p] <= '9') {
                    code = code * 10 + (after[p] - '0');
                    p += 2;
                }
            }
        } else {
            std::istringstream ss(after);
            ss >> code;
        }
        result.exitCode = (DWORD)code;
    }

    return result;
}

// ─── 原始单次执行（showWindow 模式） ─────────────────────────────────────────

CommandResult WslExecutor::ExecuteInternal(const std::string& wslCommand,
                                            const std::string& stdinInput,
                                            DWORD timeoutMs) {
    CommandResult result = { (DWORD)-1, "", "" };

    // 转义引号
    std::string escapedCmd = wslCommand;
    size_t pos = 0;
    while ((pos = escapedCmd.find('"', pos)) != std::string::npos) {
        escapedCmd.replace(pos, 1, "\\\"");
        pos += 2;
    }

    // --cd ~ 支持检测（与 EnsureStarted 共享缓存）
    static int s_cdSupported = -1;
    if (s_cdSupported == -1) {
        std::wstring testDistro = utils::Utf8ToWString(distro_);
        auto testResult = utils::RunProcess(L"wsl.exe",
            L"--cd ~ -d " + testDistro + L" -- echo ok", "", 5000);
        s_cdSupported = (testResult.exitCode == 0) ? 1 : 0;
    }

    std::string cdFlag = s_cdSupported ? "--cd ~ " : "";
    std::string suffix = showWindow_ ? "; exec bash" : "";
    std::string fullCmd = "wsl.exe " + cdFlag + "-d " + distro_ + " -- " + escapedCmd + suffix;
    std::wstring wCmd = utils::Utf8ToWString(fullCmd);

    CommandWindow window;
    if (!window.Create(wCmd, showWindow_, !showWindow_)) {
        result.stdErr = "CreateProcess failed: " + std::to_string(GetLastError());
        Logger::Instance().Error("[WslExecutor] " + result.stdErr);
        return result;
    }

    if (showWindow_) {
        window.WaitForExit(INFINITE, &result.exitCode);
        return result;
    }

    if (!stdinInput.empty()) {
        window.WriteLine(stdinInput);
    }

    if (!window.WaitForExit(timeoutMs, &result.exitCode)) {
        window.Terminate();
        result.exitCode = (DWORD)-1;
        result.stdErr = "Command timed out after " + std::to_string(timeoutMs) + "ms";
        Logger::Instance().Warning("[WslExecutor] " + result.stdErr);
        return result;
    }

    auto outLines = window.ReadAllLines(2000);
    for (const auto& line : outLines) {
        result.stdOut += line + "\n";
    }

    return result;
}
