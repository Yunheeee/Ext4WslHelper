#include "PowershellExecutor.h"
#include "../common/Logger.h"
#include "../common/Utils.h"
#include "CommandWindow.h"
#include <sstream>
#include <cstring>

PowershellExecutor::PowershellExecutor(bool showWindow)
    : showWindow_(showWindow) {
}

PowershellExecutor::~PowershellExecutor() {
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

CommandResult PowershellExecutor::Execute(const std::string& commandLine,
                                          const std::string& stdinInput,
                                          DWORD timeoutMs) {
    if (showWindow_) {
        return ExecuteInternal(commandLine, stdinInput, timeoutMs);
    }

    // 外部命令：直接调用，绕过 PS 的编码问题
    static std::vector<std::string> kDirect = {"usbipd", "wsl", "wsl.exe", "winget", "sc", "dism"};
    bool isDirect = false;
    for (const auto& prefix : kDirect) {
        if (commandLine.find(prefix) == 0) { isDirect = true; break; }
    }

    if (isDirect) {
        auto space = commandLine.find(' ');
        std::string exe = (space != std::string::npos) ? commandLine.substr(0, space) : commandLine;
        std::string args = (space != std::string::npos) ? commandLine.substr(space + 1) : "";
        // 如果 exe 已包含 .exe 扩展名，不再追加
        std::string exePath = (exe.size() >= 4 && exe.substr(exe.size() - 4) == ".exe")
            ? exe : exe + ".exe";
        std::wstring wExe = utils::Utf8ToWString(exePath);
        std::wstring wArgs = utils::Utf8ToWString(args);

        // 先记录命令，debug 窗口和日志立即可见
        Logger::Instance().Info("> " + commandLine);
        if (onLine_) onLine_("> " + commandLine);

        // winget install 等长命令用流式版本（实时显示下载进度行），
        // 其余命令用同步 RunProcess（已验证稳定）
        bool isLongInstall = (exe == "winget" && args.find("install") != std::string::npos);

        if (isLongInstall) {
            auto r = utils::RunProcessStreaming(wExe, wArgs, stdinInput, timeoutMs,
                [this](const std::string& line) {
                    Logger::Instance().Info(line);
                    if (onLine_) onLine_(line);
                });
            return { r.exitCode, r.stdOut, r.stdErr };
        }

        auto r = utils::RunProcess(wExe, wArgs, stdinInput, timeoutMs);
        CommandResult cr = { r.exitCode, r.stdOut, r.stdErr };

        if (!cr.stdOut.empty()) {
            std::istringstream ss(cr.stdOut);
            std::string line;
            while (std::getline(ss, line)) {
                Logger::Instance().Info(line);
                if (onLine_) onLine_(line);
            }
        }
        if (!cr.stdErr.empty()) {
            std::istringstream ss(cr.stdErr);
            std::string line;
            while (std::getline(ss, line)) {
                Logger::Instance().Info(line);
                if (onLine_) onLine_(line);
            }
        }

        return cr;
    }

    // PS 内部命令
    if (!EnsureStarted()) {
        CommandResult cr = { (DWORD)-1, "", "" };
        cr.stdErr = "Failed to start persistent PowerShell process";
        Logger::Instance().Error("[PowershellExecutor] " + cr.stdErr);
        return cr;
    }
    return ExecuteWithMarkers(commandLine, stdinInput, timeoutMs);
}

HANDLE PowershellExecutor::StartBackground(const std::string& commandLine) {
    std::string fullCmd = "powershell.exe -Command \"" + commandLine + "\"";
    std::wstring wCmd = utils::Utf8ToWString(fullCmd);

    CommandWindow window;
    if (!window.Create(wCmd, showWindow_, false)) {
        return INVALID_HANDLE_VALUE;
    }
    return window.GetProcessHandle();
}

bool PowershellExecutor::IsAvailable() {
    auto result = Execute("echo available", "", 5000);
    return result.exitCode == 0;
}

// ─── 长驻进程管理 ────────────────────────────────────────────────────────────

bool PowershellExecutor::EnsureStarted() {

    if (window_) {
        if (window_->IsRunning()) return true;
        window_->Close();
        window_.reset();
    }

    window_ = std::make_unique<CommandWindow>();
    std::string psCmd = "powershell.exe -NoProfile -NoExit";
    std::wstring wCmd = utils::Utf8ToWString(psCmd);
    if (!window_->Create(wCmd, false, true)) {
        Logger::Instance().Error("[PowershellExecutor] Failed to create persistent process");
        window_.reset();
        return false;
    }
    // 禁用 PSReadLine + 设置正确的输出编码（子进程如 usbipd/wsl 输出 GBK）
    window_->WriteLine("Remove-Module PSReadLine -ErrorAction SilentlyContinue");
    Sleep(300);
    window_->ReadAllRaw(1000);
    Logger::Instance().Debug("[PowershellExecutor] Persistent process started");
    return true;
}

// ─── 标记协议 ────────────────────────────────────────────────────────────────

CommandResult PowershellExecutor::ExecuteWithMarkers(const std::string& cmdLine,
                                                      const std::string& stdinInput,
                                                      DWORD timeoutMs) {
    CommandResult result = { (DWORD)-1, "", "" };

    // 构建带标记的包裹命令
    const char* kStart = "###X4W_CSTART###";
    const char* kEnd = "###X4W_CEND###";
    std::string wrappedCmd =
        std::string(kStart) + "\n" +
        cmdLine + "\n" +
        "$r=$LASTEXITCODE; Write-Output \"###X4W_CEND###$r\"\n";

    // 日志 + debug 窗口：记录用户实际命令（与直接路径格式一致）
    Logger::Instance().Info("> " + cmdLine);
    if (onLine_) onLine_("> " + cmdLine);

    // WriteLine 本身会记录 "> wrappedCmd" 到日志，但 wrappedCmd 含标记协议数据
    // 对用户无意义。临时关闭 CommandWindow 日志避免冗余。
    window_->SetLogging(false);
    window_->WriteLine(wrappedCmd);
    window_->SetLogging(true);

    if (!stdinInput.empty()) {
        window_->WriteLine(stdinInput);
    }

    std::string raw = window_->ReadAllRaw(timeoutMs);
    if (raw.empty()) {
        result.stdErr = "Command timed out or no data";
        return result;
    }

    // PS 进程在分配控制台后可能以 UTF-16LE 输出标记，构建两种编码的标记模式
    static const size_t kStartLen = strlen(kStart); // 16
    static const size_t kEndLen   = strlen(kEnd);   // 14
    static const std::string kStart16 = []() -> std::string {
        std::string s; for (char c : std::string("###X4W_CSTART###")) { s += c; s += '\0'; } return s;
    }();
    static const std::string kEnd16 = []() -> std::string {
        std::string s; for (char c : std::string("###X4W_CEND###")) { s += c; s += '\0'; } return s;
    }();

    // 先搜 ANSI，再搜 UTF-16LE（用 rfind 避免 PS 回显行中的假标记）
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
        // UTF-16LE 在 \r\n 之间嵌入了 \0，一并跳过
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
        if (onLine_) onLine_(line);
    }

    // 解析退出码（CEND 标记后的数字）
    if (endPos != std::string::npos) {
        std::string after = raw.substr(endPos + endMarkerLen);
        int code = -1;
        if (endIs16) {
            // UTF-16LE 退出码：数字在偶数位，\0 在奇数位
            size_t p = 0;
            while (p < after.size() && !(after[p] >= '0' && after[p] <= '9')) p++;
            if (p < after.size()) {
                code = 0;
                while (p < after.size() && after[p] >= '0' && after[p] <= '9') {
                    code = code * 10 + (after[p] - '0');
                    p += 2; // 跳过 \0
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

CommandResult PowershellExecutor::ExecuteInternal(const std::string& cmdLine,
                                                   const std::string& stdinInput,
                                                   DWORD timeoutMs) {
    CommandResult result = { (DWORD)-1, "", "" };

    std::string fullCmd = "powershell.exe -NoProfile";
    if (showWindow_) fullCmd += " -NoExit";
    fullCmd += " -Command \"" + cmdLine + "\"";

    std::wstring wCmd = utils::Utf8ToWString(fullCmd);

    CommandWindow window;
    if (!window.Create(wCmd, showWindow_, !showWindow_)) {
        result.stdErr = "CreateProcess failed: " + std::to_string(GetLastError());
        Logger::Instance().Error("[PowershellExecutor] " + result.stdErr);
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
        Logger::Instance().Warning("[PowershellExecutor] " + result.stdErr);
        return result;
    }

    auto outLines = window.ReadAllLines(2000);
    for (const auto& line : outLines) {
        result.stdOut += line + "\n";
    }

    return result;
}
