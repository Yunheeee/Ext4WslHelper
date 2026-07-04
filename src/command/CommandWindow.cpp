#include "CommandWindow.h"
#include "../common/Logger.h"
#include "../common/Utils.h"
#include <array>
#include <algorithm>

// ─── 构造 / 析构 ─────────────────────────────────────────────────────────

CommandWindow::CommandWindow()
    : hStdInWrite_(nullptr), hStdOutRead_(nullptr), hStdErrRead_(nullptr)
    , hasPipes_(false), logging_(true), hasParentConsole_(false) {
    pi_.hProcess = nullptr;
    pi_.hThread = nullptr;
    pi_.dwProcessId = 0;
    pi_.dwThreadId = 0;
}

CommandWindow::~CommandWindow() { Close(); }

// ─── 进程创建（管道模式） ──────────────────────────────────────────────────

bool CommandWindow::Create(const std::wstring& shellPath,
                            bool showWindow, bool createPipe) {
    if (pi_.hProcess) return false;

    if (createPipe) {
        // 创建管道
        SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
        HANDLE hStdInRd = nullptr, hStdInWr = nullptr;
        HANDLE hStdOutRd = nullptr, hStdOutWr = nullptr;
        HANDLE hStdErrRd = nullptr, hStdErrWr = nullptr;

        if (!CreatePipe(&hStdOutRd, &hStdOutWr, &sa, 0)) {
            Logger::Instance().Error("[CommandWindow] CreatePipe failed");
            return false;
        }
        if (!CreatePipe(&hStdErrRd, &hStdErrWr, &sa, 0)) {
            CloseHandle(hStdOutRd); CloseHandle(hStdOutWr);
            Logger::Instance().Error("[CommandWindow] CreatePipe failed");
            return false;
        }
        if (!CreatePipe(&hStdInRd, &hStdInWr, &sa, 0)) {
            CloseHandle(hStdOutRd); CloseHandle(hStdOutWr);
            CloseHandle(hStdErrRd); CloseHandle(hStdErrWr);
            Logger::Instance().Error("[CommandWindow] CreatePipe failed");
            return false;
        }

        SetHandleInformation(hStdOutRd, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(hStdErrRd, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(hStdInWr, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW si = { sizeof(si) };
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = hStdOutWr;
        si.hStdError = hStdErrWr;
        si.hStdInput = hStdInRd;

        DWORD flags = CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW;

        BOOL created = CreateProcessW(nullptr, const_cast<wchar_t*>(shellPath.c_str()),
                                       nullptr, nullptr, TRUE,
                                       flags, nullptr, nullptr, &si, &pi_);

        CloseHandle(hStdOutWr);
        CloseHandle(hStdErrWr);
        CloseHandle(hStdInRd);

        if (!created) {
            CloseHandle(hStdOutRd);
            CloseHandle(hStdErrRd);
            CloseHandle(hStdInWr);
            Logger::Instance().Error("[CommandWindow] CreateProcess failed: " +
                                     std::to_string(GetLastError()));
            return false;
        }

        hStdOutRead_ = hStdOutRd;
        hStdErrRead_ = hStdErrRd;
        hStdInWrite_ = hStdInWr;
        hasPipes_ = true;
    } else {
        // 无管道模式：直接启动进程
        DWORD flags = CREATE_UNICODE_ENVIRONMENT;
        if (showWindow) flags |= CREATE_NEW_CONSOLE;
        else flags |= CREATE_NO_WINDOW;

        STARTUPINFOW si = { sizeof(si) };
        if (!CreateProcessW(nullptr, const_cast<wchar_t*>(shellPath.c_str()),
                             nullptr, nullptr, TRUE,
                             flags, nullptr, nullptr, &si, &pi_)) {
            Logger::Instance().Error("[CommandWindow] CreateProcess failed: " +
                                     std::to_string(GetLastError()));
            return false;
        }
    }

    CloseHandle(pi_.hThread);
    pi_.hThread = nullptr;

    Logger::Instance().Info("[CommandWindow] Started: " +
                            utils::WStringToUtf8(shellPath));

    // showWindow 参数保留供未来使用，当前管道模式下子进程始终隐藏
}

// ─── 父进程控制台（showWindow 时显示） ───────────────────────────────────

bool CommandWindow::AllocParentConsole() {
    if (hasParentConsole_) return true;
    if (AllocConsole()) {
        hasParentConsole_ = true;
        SetConsoleTitleW(L"Ext4WslHelper - CommandWindow");
        return true;
    }
    return false;
}

void CommandWindow::FreeParentConsole() {
    if (hasParentConsole_) { FreeConsole(); hasParentConsole_ = false; }
}

void CommandWindow::ForwardToConsole(const std::string& text) {
    if (!hasParentConsole_ || text.empty()) return;
    HANDLE hCon = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hCon && hCon != INVALID_HANDLE_VALUE) {
        std::wstring wtext = utils::Utf8ToWString(text);
        WriteConsoleW(hCon, wtext.data(), (DWORD)wtext.size(), nullptr, nullptr);
    }
}

void CommandWindow::WriteToConsole(const std::string& text) {
    ForwardToConsole(text);
}

// ─── 等待 / 终止 / 关闭 ─────────────────────────────────────────────────────

bool CommandWindow::WaitForExit(DWORD timeoutMs, DWORD* exitCode) {
    if (!pi_.hProcess) return false;
    if (WaitForSingleObject(pi_.hProcess, timeoutMs) == WAIT_TIMEOUT) return false;
    if (exitCode) GetExitCodeProcess(pi_.hProcess, exitCode);
    return true;
}

void CommandWindow::Terminate() {
    if (pi_.hProcess) {
        TerminateProcess(pi_.hProcess, 1);
        WaitForSingleObject(pi_.hProcess, 1000);
    }
}

void CommandWindow::Close() {
    if (hStdInWrite_) { CloseHandle(hStdInWrite_); hStdInWrite_ = nullptr; }
    if (hStdOutRead_) { CloseHandle(hStdOutRead_); hStdOutRead_ = nullptr; }
    if (hStdErrRead_) { CloseHandle(hStdErrRead_); hStdErrRead_ = nullptr; }
    if (pi_.hThread)  { CloseHandle(pi_.hThread);  pi_.hThread  = nullptr; }
    if (pi_.hProcess) { CloseHandle(pi_.hProcess); pi_.hProcess = nullptr; }
    // 注意：不调 FreeConsole()，因为它是进程级操作。
    // 若有多个 CommandWindow 实例共享同一控制台，调用 FreeConsole
    // 会关闭所有实例的窗口。控制台在进程退出时由 OS 自动清理。
    hasPipes_ = false;
}

// ─── I/O ────────────────────────────────────────────────────────────────────

bool CommandWindow::WriteLine(const std::string& command) {
    if (logging_) Logger::Instance().Info("> " + command);
    if (!hStdInWrite_) return false;
    std::string line = command + "\n";
    DWORD written = 0;
    return WriteFile(hStdInWrite_, line.data(), (DWORD)line.size(),
                     &written, nullptr) != FALSE;
}

// ─── 读取 stdout ────────────────────────────────────────────────────────────

std::vector<std::string> CommandWindow::ReadAllLines(DWORD timeoutMs) {
    std::string raw = ReadAllRaw(timeoutMs);
    std::vector<std::string> lines;
    ProcessAndLogInto(raw, lines);

    if (hasParentConsole_ && !raw.empty()) {
        // 转发到父控制台（先编码转换，不转发原始 UTF-16LE）
        std::string display = raw;
        StripBom(display);
        if (IsLikelyUtf16Le(display)) ConvertFromUtf16Le(display);
        if (HasHighBytes(display)) TryBetterEncoding(display);
        StripAnsi(display);
        ForwardToConsole(display);
    }
    return lines;
}

// ─── 非阻塞读取管道当前可用数据 ──────────────────────────────────────────

std::string CommandWindow::FlushRead() {
    std::string raw;
    if (!hStdOutRead_) return raw;
    std::array<char, 16384> buf;

    // 读 stdout
    while (true) {
        DWORD avail = 0;
        if (!PeekNamedPipe(hStdOutRead_, nullptr, 0, nullptr, &avail, nullptr) || avail == 0)
            break;
        DWORD n = (std::min)(avail, (DWORD)buf.size() - 1);
        DWORD bytesRead = 0;
        if (!ReadFile(hStdOutRead_, buf.data(), n, &bytesRead, nullptr) || bytesRead == 0)
            break;
        buf[bytesRead] = '\0';
        raw.append(buf.data(), bytesRead);
    }
    // 读 stderr
    if (hStdErrRead_) {
        while (true) {
            DWORD avail = 0;
            if (!PeekNamedPipe(hStdErrRead_, nullptr, 0, nullptr, &avail, nullptr) || avail == 0)
                break;
            DWORD n = (std::min)(avail, (DWORD)buf.size() - 1);
            DWORD bytesRead = 0;
            if (!ReadFile(hStdErrRead_, buf.data(), n, &bytesRead, nullptr) || bytesRead == 0)
                break;
            buf[bytesRead] = '\0';
            raw.append(buf.data(), bytesRead);
        }
    }
    return raw;
}

// ─── 读取原始输出（无编码转换、无分割、无日志） ────────────────────────────

std::string CommandWindow::ReadAllRaw(DWORD timeoutMs) {
    std::string raw;
    if (!hStdOutRead_) return raw;

    std::array<char, 16384> buf;
    DWORD totalWait = 0;
    const DWORD step = 50;
    bool hasData = false;

    while (totalWait < timeoutMs) {
        // 读 stdout — PeekNamedPipe 查可用字节
        DWORD avail = 0;
        if (PeekNamedPipe(hStdOutRead_, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            DWORD bytesRead = 0;
            if (ReadFile(hStdOutRead_, buf.data(),
                         (std::min)(avail, (DWORD)buf.size() - 1), &bytesRead, nullptr))
            {
                buf[bytesRead] = '\0';
                raw.append(buf.data(), bytesRead);
                totalWait = 0;
                hasData = true;
                continue;
            }
        }
        // 读 stderr
        if (hStdErrRead_) {
            avail = 0;
            if (PeekNamedPipe(hStdErrRead_, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
                DWORD bytesRead = 0;
                if (ReadFile(hStdErrRead_, buf.data(),
                             (std::min)(avail, (DWORD)buf.size() - 1), &bytesRead, nullptr))
                {
                    buf[bytesRead] = '\0';
                    raw.append(buf.data(), bytesRead);
                    totalWait = 0;
                    hasData = true;
                    continue;
                }
            }
        }
        // 进程已退出则读完残留
        if (pi_.hProcess && WaitForSingleObject(pi_.hProcess, 0) == WAIT_OBJECT_0) {
            break;
        }
        // 收到过数据且空闲 > 500ms
        if (hasData && totalWait >= 500)
            break;
        if (totalWait >= timeoutMs) break;
        Sleep(step);
        totalWait += step;
    }
    return raw;
}

// ─── 状态查询 ───────────────────────────────────────────────────────────────

bool CommandWindow::IsRunning() const {
    if (!pi_.hProcess) return false;
    return WaitForSingleObject(pi_.hProcess, 0) == WAIT_TIMEOUT;
}

DWORD CommandWindow::GetProcessId() const { return pi_.dwProcessId; }
HANDLE CommandWindow::GetProcessHandle() const { return pi_.hProcess; }

// ─── 编码工具（静态） ──────────────────────────────────────────────────────

std::vector<std::string> CommandWindow::ProcessAndLog(const std::string& rawData) {
    std::vector<std::string> lines;
    ProcessAndLogInto(rawData, lines);
    return lines;
}

void CommandWindow::ProcessAndLogInto(const std::string& raw,
                                       std::vector<std::string>& lines) {
    std::string s = raw;
    // 顺序：BOM → UTF-16LE 转换 → ANSI 剥离 → OEM 兜底
    StripBom(s);
    bool cameFromUtf16 = IsLikelyUtf16Le(s);
    if (cameFromUtf16) ConvertFromUtf16Le(s);
    StripAnsi(s);

    if (HasHighBytes(s)) {
        if (cameFromUtf16) {
            // PS 的 UTF-16LE 已正确转换，跳过 OEM。若转换后仍非 UTF-8 才兜底
            if (!IsValidUtf8(s)) OemFallback(s);
        } else {
            // 非 UTF-16LE 来源（如 wsl.exe OEM 输出）→ 大胆尝试 OEM
            if (!IsValidUtf8(s)) {
                OemFallback(s);
            } else {
                std::string oem = s;
                OemFallback(oem);
                if (!oem.empty() && oem != s && IsValidUtf8(oem))
                    s = std::move(oem);
            }
        }
    }

    std::string line;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\n') {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) Logger::Instance().Info(line);
            lines.push_back(line);
            line.clear();
        } else if (s[i] != '\r') {
            line += s[i];
        }
    }
    if (!line.empty()) {
        Logger::Instance().Info(line);
        lines.push_back(std::move(line));
    }
}

// ─── ANSI 转义剥离 ─────────────────────────────────────────────────────────

void CommandWindow::StripAnsi(std::string& s) {
    if (s.empty()) return;
    std::string r;
    r.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '\x1b') {
            i++; if (i >= s.size()) break;
            if (s[i] == '[') {
                i++;
                while (i < s.size() && s[i] >= 0x20 && s[i] <= 0x3F) i++;
                while (i < s.size() && s[i] >= 0x20 && s[i] <= 0x2F) i++;
                if (i < s.size()) i++;
            } else if (s[i] == ']') {
                i++;
                while (i < s.size() && s[i] != 0x07 &&
                       !(s[i] == '\x1b' && i + 1 < s.size() && s[i + 1] == '\\'))
                    i++;
                if (i < s.size() && s[i] == 0x07) i++;
                else if (i < s.size() && s[i] == '\x1b') i += 2;
            } else {
                i++;
            }
        } else {
            r += s[i];
            i++;
        }
    }
    s = std::move(r);
}

// ─── 编码处理 ───────────────────────────────────────────────────────────────

void CommandWindow::StripBom(std::string& s) {
    if (s.size() >= 3 &&
        (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF)
        s = s.substr(3);
    else if (s.size() >= 2 &&
             (unsigned char)s[0] == 0xFF && (unsigned char)s[1] == 0xFE)
        s = s.substr(2);
}

bool CommandWindow::IsLikelyUtf16Le(const std::string& s) {
    if (s.size() < 4) return false;
    if ((unsigned char)s[0] == 0xFF && (unsigned char)s[1] == 0xFE) return true;
    size_t oddNull = 0, evenNull = 0;
    for (size_t i = 1; i < s.size(); i++) {
        if (i % 2 == 0) { if (s[i] == '\0') evenNull++; }
        else { if (s[i] == '\0') oddNull++; }
    }
    size_t oddCount = s.size() / 2;
    return oddNull > oddCount / 4 && evenNull < oddCount / 10;
}

void CommandWindow::ConvertFromUtf16Le(std::string& s) {
    size_t len = s.size();
    if (len % 2 == 1) len--;
    if (len < 2) return;
    size_t offset = 0;
    if (len >= 2 && (unsigned char)s[0] == 0xFF && (unsigned char)s[1] == 0xFE)
        { offset = 2; len -= 2; }
    if (len < 2) { s.clear(); return; }
    int wideLen = static_cast<int>(len / 2);
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0,
        reinterpret_cast<LPCWCH>(s.data() + offset), wideLen,
        nullptr, 0, nullptr, nullptr);
    if (utf8Len > 0) {
        std::string utf8(utf8Len, '\0');
        WideCharToMultiByte(CP_UTF8, 0,
            reinterpret_cast<LPCWCH>(s.data() + offset), wideLen,
            utf8.data(), utf8Len, nullptr, nullptr);
        s = std::move(utf8);
    }
}

bool CommandWindow::IsValidUtf8(const std::string& s) {
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) { i++; continue; }
        else if (c < 0xC2) return false;
        else if (c < 0xE0) {
            if (i + 1 >= s.size() || (static_cast<unsigned char>(s[i+1]) & 0xC0) != 0x80) return false;
            i += 2;
        }
        else if (c < 0xF0) {
            if (i + 2 >= s.size() || (static_cast<unsigned char>(s[i+1]) & 0xC0) != 0x80 || (static_cast<unsigned char>(s[i+2]) & 0xC0) != 0x80) return false;
            i += 3;
        }
        else if (c < 0xF8) {
            if (i + 3 >= s.size() || (static_cast<unsigned char>(s[i+1]) & 0xC0) != 0x80 || (static_cast<unsigned char>(s[i+2]) & 0xC0) != 0x80 || (static_cast<unsigned char>(s[i+3]) & 0xC0) != 0x80) return false;
            i += 4;
        }
        else return false;
    }
    return true;
}

bool CommandWindow::HasHighBytes(const std::string& s) {
    for (unsigned char c : s) if (c > 127) return true;
    return false;
}

void CommandWindow::OemFallback(std::string& s) {
    std::string converted = utils::OemToUtf8(s);
    if (!converted.empty()) s = std::move(converted);
}

int CommandWindow::CountCjkChars(const std::string& s) {
    if (!IsValidUtf8(s)) return 0;
    int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                    s.data(), (int)s.size(), nullptr, 0);
    if (wlen <= 0) return 0;
    std::wstring wstr(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), wstr.data(), wlen);
    int count = 0;
    for (wchar_t wc : wstr) {
        if (wc >= 0x3400 && wc <= 0x9FFF) count++;
    }
    return count;
}

void CommandWindow::TryBetterEncoding(std::string& s) {
    // 仅当不是有效 UTF-8 时尝试 OEM 转换。
    // 不做 CJK 字符统计：从 UTF-16LE 正确转换的中文已是有效 UTF-8，
    // OEM 回退会破坏它产生不同但同样有效的 UTF-8 乱码，无法区分。
    if (!IsValidUtf8(s))
        OemFallback(s);
}

// ─── 管道非阻塞读取 ────────────────────────────────────────────────────────

DWORD CommandWindow::ReadAvailable(HANDLE hPipe, char* buffer, DWORD bufSize) {
    DWORD avail = 0;
    if (!PeekNamedPipe(hPipe, nullptr, 0, nullptr, &avail, nullptr) || avail == 0)
        return 0;
    DWORD bytesRead = 0;
    if (ReadFile(hPipe, buffer, (std::min)(avail, bufSize), &bytesRead, nullptr))
        return bytesRead;
    return 0;
}
