#pragma once
#include <string>
#include <vector>
#include <windows.h>

// 命令行窗口 — 通过 CreateProcess + 匿名管道执行命令，
// 支持 UTF-16LE/OEM 编码自动检测与转换。
//
// showWindow=true 时自动 AllocConsole 创建可见控制台窗口，
// 输出（编码转换后）同时转发到该窗口。
class CommandWindow {
public:
    CommandWindow();
    ~CommandWindow();

    CommandWindow(const CommandWindow&) = delete;
    CommandWindow& operator=(const CommandWindow&) = delete;

    // ─── 进程管理 ───────────────────────────────────────────────────────
    //
    // 一个 CommandWindow 对象管理一个进程实例。
    // Create 只能调用一次，后续通过 Write/ReadAllLines 在**同一个进程**中
    // 反复执行命令，最后发送 exit + WaitForExit + Close 结束。
    //
    //   Create("powershell.exe -NoProfile -NoExit")   ← 一次创建长驻 shell
    //     Write("echo 1") → ReadAllLines()                     ↓
    //     Write("echo 2") → ReadAllLines()             ← 同一进程反复执行
    //     Write("exit")   → WaitForExit()
    //     Close()
    //
    // shellPath: 要启动的长驻进程（如 powershell -NoExit / wsl -d Ubuntu）
    //            具体命令通过 Write() 发送，不在 Create 时指定
    // showWindow: true=弹出可见控制台窗口, false=后台隐藏
    // createPipe: true=启用 ConPTY I/O, false=仅启动进程无 I/O 管道
    //
    bool Create(const std::wstring& shellPath, bool showWindow, bool createPipe);

    // 等待进程退出，返回 false 表示超时
    bool WaitForExit(DWORD timeoutMs, DWORD* exitCode = nullptr);
    void Terminate();
    void Close();

    // ─── 管道 I/O ───────────────────────────────────────────────────────

    // 向 shell 发送一行命令（自动追加 \n，记录日志）
    bool WriteLine(const std::string& command);
    // 读取 shell 输出（已剥离 ANSI，按行分割返回）
    std::vector<std::string> ReadAllLines(DWORD timeoutMs = 30000);
    // 读取原始输出（含 ANSI 序列，不分割行）
    std::string ReadAllRaw(DWORD timeoutMs = 30000);
    // 读取管道中当前可用的所有数据（不等待，立即返回）
    std::string FlushRead();
    // 写入父进程控制台（仅 showWindow=true 时有效）
    void WriteToConsole(const std::string& text);


    // ─── 状态查询 ──────────────────────────────────────────────────────

    bool IsRunning() const;
    DWORD GetProcessId() const;
    HANDLE GetProcessHandle() const;

    void SetLogging(bool enable) { logging_ = enable; }
    bool IsLoggingEnabled() const { return logging_; }

    static std::vector<std::string> ProcessAndLog(const std::string& rawData);

private:
    bool AllocParentConsole();
    void FreeParentConsole();
    void ForwardToConsole(const std::string& text);

    DWORD ReadAvailable(HANDLE hPipe, char* buffer, DWORD bufSize);
    static void ProcessAndLogInto(const std::string& raw, std::vector<std::string>& lines);
    static void StripAnsi(std::string& s);
    static void StripBom(std::string& s);
    static bool IsLikelyUtf16Le(const std::string& s);
    static void ConvertFromUtf16Le(std::string& s);
    static bool IsValidUtf8(const std::string& s);
    static bool HasHighBytes(const std::string& s);
    static void OemFallback(std::string& s);
    static void TryBetterEncoding(std::string& s);
    static int CountCjkChars(const std::string& s);

    PROCESS_INFORMATION pi_;
    HANDLE hStdInWrite_;    // stdin 管道写端
    HANDLE hStdOutRead_;    // stdout 管道读端
    HANDLE hStdErrRead_;    // stderr 管道读端
    bool hasPipes_;
    bool logging_;
    bool hasParentConsole_;
};
