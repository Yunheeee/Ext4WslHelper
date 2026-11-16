#include "Utils.h"
#include "Logger.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <array>

namespace utils {

namespace {

bool IsValidUtf8Bytes(const std::string& s) {
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) { i++; continue; }
        if (c < 0xC2) return false;
        if (c < 0xE0) {
            if (i + 1 >= s.size() || (static_cast<unsigned char>(s[i + 1]) & 0xC0) != 0x80) return false;
            i += 2;
        } else if (c < 0xF0) {
            if (i + 2 >= s.size() || (static_cast<unsigned char>(s[i + 1]) & 0xC0) != 0x80 ||
                (static_cast<unsigned char>(s[i + 2]) & 0xC0) != 0x80) return false;
            i += 3;
        } else if (c < 0xF8) {
            if (i + 3 >= s.size() || (static_cast<unsigned char>(s[i + 1]) & 0xC0) != 0x80 ||
                (static_cast<unsigned char>(s[i + 2]) & 0xC0) != 0x80 ||
                (static_cast<unsigned char>(s[i + 3]) & 0xC0) != 0x80) return false;
            i += 4;
        } else {
            return false;
        }
    }
    return true;
}

bool LooksLikeUtf16Le(const std::string& s) {
    if (s.size() < 4) return false;
    if (static_cast<unsigned char>(s[0]) == 0xFF &&
        static_cast<unsigned char>(s[1]) == 0xFE) return true;
    size_t oddNull = 0, evenNull = 0;
    for (size_t i = 0; i + 1 < s.size(); i++) {
        if (i % 2 == 0) { if (s[i] == '\0') evenNull++; }
        else { if (s[i] == '\0') oddNull++; }
    }
    size_t pairs = s.size() / 2;
    return oddNull > pairs / 4 && evenNull < pairs / 10;
}

std::string Utf16LeToUtf8(const std::string& s) {
    size_t offset = 0;
    size_t len = s.size();
    if (len >= 2 && static_cast<unsigned char>(s[0]) == 0xFF &&
        static_cast<unsigned char>(s[1]) == 0xFE) {
        offset = 2;
        len -= 2;
    }
    len &= ~size_t(1); // 只处理完整 UTF-16 单元
    if (len < 2) return {};
    int wideLen = static_cast<int>(len / 2);
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0,
        reinterpret_cast<LPCWCH>(s.data() + offset), wideLen,
        nullptr, 0, nullptr, nullptr);
    if (utf8Len <= 0) return {};
    std::string utf8(utf8Len, '\0');
    WideCharToMultiByte(CP_UTF8, 0,
        reinterpret_cast<LPCWCH>(s.data() + offset), wideLen,
        utf8.data(), utf8Len, nullptr, nullptr);
    return utf8;
}

// 把原始管道数据转换成可读的单行日志预览（UTF-16LE → UTF-8，OEM 兜底）
std::string DecodeForLog(const std::string& raw) {
    std::string text = LooksLikeUtf16Le(raw) ? Utf16LeToUtf8(raw) : raw;
    if (!text.empty() && !IsValidUtf8Bytes(text)) {
        std::string oem = OemToUtf8(text);
        if (!oem.empty()) text = std::move(oem);
    }

    // 折叠换行/制表，连续空白只保留一个
    std::string clean;
    clean.reserve(text.size());
    bool prevSpace = false;
    for (unsigned char c : text) {
        if (c == '\r' || c == '\n' || c == '\t') {
            if (!prevSpace) { clean += ' '; prevSpace = true; }
        } else if (c >= 0x20 || c >= 0x80) {
            clean += static_cast<char>(c);
            prevSpace = (c == ' ');
        }
    }
    const size_t kMaxPreview = 400;
    if (clean.size() > kMaxPreview) {
        clean.resize(kMaxPreview);
        clean += "...";
    }
    return clean;
}

std::string DescribeForLog(const std::string& data, const char* label) {
    if (data.empty()) return {};
    return std::string(label) + "=" + std::to_string(data.size()) +
           "bytes \"" + DecodeForLog(data) + "\" ";
}

int ReadVersionComponent(const std::string& s, size_t& pos) {
    while (pos < s.size() && !(s[pos] >= '0' && s[pos] <= '9')) pos++;
    int value = 0;
    while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') {
        value = value * 10 + (s[pos] - '0');
        pos++;
    }
    while (pos < s.size() && s[pos] != '.') pos++;
    if (pos < s.size() && s[pos] == '.') pos++;
    return value;
}

} // namespace

std::string WStringToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(),
                                  nullptr, 0, nullptr, nullptr);
    std::string result(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(),
                        result.data(), len, nullptr, nullptr);
    return result;
}

std::wstring Utf8ToWString(const std::string& str) {
    if (str.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(),
                                  nullptr, 0);
    std::wstring result(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(),
                        result.data(), len);
    return result;
}

std::string OemToUtf8(const std::string& oemStr) {
    if (oemStr.empty()) return {};
    // OEM → UTF-16
    int len16 = MultiByteToWideChar(CP_OEMCP, 0, oemStr.c_str(), (int)oemStr.size(),
                                    nullptr, 0);
    std::wstring wstr(len16, 0);
    MultiByteToWideChar(CP_OEMCP, 0, oemStr.c_str(), (int)oemStr.size(),
                        wstr.data(), len16);
    // UTF-16 → UTF-8
    int len8 = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(),
                                   nullptr, 0, nullptr, nullptr);
    std::string result(len8, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(),
                        result.data(), len8, nullptr, nullptr);
    return result;
}

int CompareVersion(const std::string& lhs, const std::string& rhs) {
    size_t pa = 0, pb = 0;
    // 最多比较 8 段，足够覆盖内核/工具版本号
    for (int i = 0; i < 8; i++) {
        bool aDone = (pa >= lhs.size());
        bool bDone = (pb >= rhs.size());
        if (aDone && bDone) break;

        int a = aDone ? 0 : ReadVersionComponent(lhs, pa);
        int b = bDone ? 0 : ReadVersionComponent(rhs, pb);
        if (a < b) return -1;
        if (a > b) return 1;
    }
    return 0;
}

ProcessResult RunProcess(const std::wstring& executable,
                          const std::wstring& args,
                          const std::string& stdinInput,
                          DWORD timeoutMs) {
    ProcessResult result = { (DWORD)-1, "", "" };
    DWORD startTick = GetTickCount();

    // 创建管道
    HANDLE hStdInRd = nullptr, hStdInWr = nullptr;
    HANDLE hStdOutRd = nullptr, hStdOutWr = nullptr;
    HANDLE hStdErrRd = nullptr, hStdErrWr = nullptr;

    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };

    if (!CreatePipe(&hStdOutRd, &hStdOutWr, &sa, 0) ||
        !CreatePipe(&hStdErrRd, &hStdErrWr, &sa, 0) ||
        !CreatePipe(&hStdInRd, &hStdInWr, &sa, 0)) {
        return result;
    }

    SetHandleInformation(hStdOutRd, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hStdErrRd, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hStdInWr, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hStdOutWr;
    si.hStdError = hStdErrWr;
    si.hStdInput = hStdInRd;

    if (stdinInput.empty()) {
        CloseHandle(hStdInWr);
        hStdInWr = nullptr;
    }

    // 拼接完整命令行
    std::wstring cmdLine = executable + L" " + args;

    PROCESS_INFORMATION pi = {};
    BOOL created = CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

    CloseHandle(hStdOutWr);
    CloseHandle(hStdErrWr);
    if (hStdInRd) CloseHandle(hStdInRd);

    if (!created) {
        DWORD err = GetLastError();
        CloseHandle(hStdOutRd);
        CloseHandle(hStdErrRd);
        if (hStdInWr) CloseHandle(hStdInWr);
        std::string errMsg = "CreateProcessW failed for '" + WStringToUtf8(cmdLine) +
                             "': error " + std::to_string(err);
        Logger::Instance().Error("[RunProc] " + errMsg);
        result.stdErr = errMsg;
        return result;
    }

    // 写入 stdin
    if (!stdinInput.empty() && hStdInWr) {
        DWORD written = 0;
        WriteFile(hStdInWr, stdinInput.data(), (DWORD)stdinInput.size(), &written, nullptr);
        CloseHandle(hStdInWr);
    }

    // 等待
    DWORD waitResult = WaitForSingleObject(pi.hProcess, timeoutMs);
    if (waitResult == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        result.exitCode = (DWORD)-1;
    } else {
        GetExitCodeProcess(pi.hProcess, &result.exitCode);
    }

    // 读取输出
    std::array<char, 4096> buf;
    DWORD bytesRead = 0;
    while (ReadFile(hStdOutRd, buf.data(), (DWORD)buf.size() - 1, &bytesRead, nullptr) &&
           bytesRead > 0) {
        result.stdOut.append(buf.data(), bytesRead);
    }
    while (ReadFile(hStdErrRd, buf.data(), (DWORD)buf.size() - 1, &bytesRead, nullptr) &&
           bytesRead > 0) {
        result.stdErr.append(buf.data(), bytesRead);
    }

    // 编码检测与统一转换：UTF-16LE → OEM → UTF-8
    // [RunProc 原始输出日志 — 按 UTF-16LE/OEM 正确解码后记录，避免不可读的 hex]
    {
        DWORD elapsed = GetTickCount() - startTick;
        std::string preview = DescribeForLog(result.stdOut, "stdout") +
                              DescribeForLog(result.stdErr, "stderr");
        Logger::Instance().Info("[RunProc] " + WStringToUtf8(cmdLine) +
                                " exit=" + std::to_string(result.exitCode) +
                                " elapsed=" + std::to_string(elapsed) + "ms " + preview);
    }
    auto IsValidUtf8 = [](const std::string& s) -> bool {
        size_t i = 0;
        while (i < s.size()) {
            unsigned char c = (unsigned char)s[i];
            if (c < 0x80) { i++; continue; }
            if (c < 0xC2) return false;
            if (c < 0xE0) {
                if (i + 1 >= s.size() || ((unsigned char)s[i+1] & 0xC0) != 0x80) return false;
                i += 2;
            } else if (c < 0xF0) {
                if (i + 2 >= s.size() || ((unsigned char)s[i+1] & 0xC0) != 0x80 || ((unsigned char)s[i+2] & 0xC0) != 0x80) return false;
                i += 3;
            } else if (c < 0xF8) {
                if (i + 3 >= s.size() || ((unsigned char)s[i+1] & 0xC0) != 0x80 || ((unsigned char)s[i+2] & 0xC0) != 0x80 || ((unsigned char)s[i+3] & 0xC0) != 0x80) return false;
                i += 4;
            } else return false;
        }
        return true;
    };
    auto HasHighBytes = [](const std::string& s) -> bool {
        for (unsigned char c : s) if (c > 127) return true;
        return false;
    };
    auto ConvertFromUtf16LeIfNeeded = [](std::string& s) {
        if (s.size() < 4) return;
        // 检查 BOM
        if ((unsigned char)s[0] == 0xFF && (unsigned char)s[1] == 0xFE)
            goto doConvert;
        // 扫描 UTF-16LE 特征：奇数位多为 null，偶数位极少 null
        {
            size_t oddNull = 0, evenNull = 0;
            for (size_t i = 0; i + 1 < s.size(); i++) {
                if (i % 2 == 0) { if (s[i] == '\0') evenNull++; }
                else { if (s[i] == '\0') oddNull++; }
            }
            size_t oddCount = s.size() / 2;
            if (oddNull > oddCount / 4 && evenNull < oddCount / 10)
                goto doConvert;
        }
        return;
    doConvert:
        {
            int wideLen = static_cast<int>(s.size() / 2);
            int utf8Len = WideCharToMultiByte(CP_UTF8, 0,
                reinterpret_cast<LPCWCH>(s.data()), wideLen,
                nullptr, 0, nullptr, nullptr);
            if (utf8Len > 0) {
                std::string utf8(utf8Len, '\0');
                WideCharToMultiByte(CP_UTF8, 0,
                    reinterpret_cast<LPCWCH>(s.data()), wideLen,
                    utf8.data(), utf8Len, nullptr, nullptr);
                s = std::move(utf8);
            }
        }
    };
    auto ConvIfNeeded = [&](std::string& s, const char* streamLabel) {
        if (s.empty()) return;
        size_t origSize = s.size();
        ConvertFromUtf16LeIfNeeded(s);
        if (s.size() != origSize) {
            Logger::Instance().Debug("[RunProc] " + std::string(streamLabel) + " UTF-16LE→UTF-8 " +
                                     std::to_string(origSize) + "→" + std::to_string(s.size()) + "bytes");
        }
        if (!HasHighBytes(s)) return;
        if (!IsValidUtf8(s)) {
            std::string utf8 = OemToUtf8(s);
            if (!utf8.empty()) s = std::move(utf8);
            Logger::Instance().Debug("[RunProc] " + std::string(streamLabel) +
                                     " OEM→UTF-8 applied (was invalid UTF-8)");
            return;
        }
        // Even if it looks like valid UTF-8, it could be OEM-codepage text
        // (e.g. GBK) that happens to pass byte-level UTF-8 validation.
        // Try OEM; if it produces CJK characters while the original has none,
        // prefer OEM — the original was likely GBK misinterpreted as UTF-8.
        std::string fromOem = OemToUtf8(s);
        if (!fromOem.empty() && fromOem != s && IsValidUtf8(fromOem)) {
            auto countCjk = [&](const std::string& str) -> int {
                if (!IsValidUtf8(str)) return 0;
                int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                    str.data(), (int)str.size(), nullptr, 0);
                if (wlen <= 0) return 0;
                std::wstring wstr(wlen, L'\0');
                MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), wstr.data(), wlen);
                int count = 0;
                for (wchar_t wc : wstr) {
                    if (wc >= 0x3400 && wc <= 0x9FFF) count++;
                }
                return count;
            };
            int cjkOrig = countCjk(s);
            int cjkOem  = countCjk(fromOem);
            if (cjkOem > 0 && cjkOrig == 0) {
                Logger::Instance().Debug("[RunProc] " + std::string(streamLabel) +
                                         " OEM→UTF-8 preferred (CJK heuristic: " +
                                         std::to_string(cjkOem) + " vs " +
                                         std::to_string(cjkOrig) + ")");
                s = std::move(fromOem);
            }
        }
    };
    ConvIfNeeded(result.stdOut, "stdout");
    ConvIfNeeded(result.stdErr, "stderr");

    CloseHandle(hStdOutRd);
    CloseHandle(hStdErrRd);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    return result;
}

// ─── 流式版本（边读边转发） ──────────────────────────────────────────────

ProcessResult RunProcessStreaming(const std::wstring& executable,
                                   const std::wstring& args,
                                   const std::string& stdinInput,
                                   DWORD timeoutMs,
                                   std::function<void(const std::string& line)> onLine) {
    ProcessResult result = { (DWORD)-1, "", "" };
    DWORD startTick = GetTickCount();

    HANDLE hStdInRd = nullptr, hStdInWr = nullptr;
    HANDLE hStdOutRd = nullptr, hStdOutWr = nullptr;
    HANDLE hStdErrRd = nullptr, hStdErrWr = nullptr;

    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };

    if (!CreatePipe(&hStdOutRd, &hStdOutWr, &sa, 0) ||
        !CreatePipe(&hStdErrRd, &hStdErrWr, &sa, 0) ||
        !CreatePipe(&hStdInRd, &hStdInWr, &sa, 0)) {
        return result;
    }

    SetHandleInformation(hStdOutRd, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hStdErrRd, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hStdInWr, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hStdOutWr;
    si.hStdError = hStdErrWr;
    si.hStdInput = hStdInRd;

    if (stdinInput.empty()) {
        CloseHandle(hStdInWr);
        hStdInWr = nullptr;
    }

    std::wstring cmdLine = executable + L" " + args;

    PROCESS_INFORMATION pi = {};
    BOOL created = CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

    CloseHandle(hStdOutWr);
    CloseHandle(hStdErrWr);
    if (hStdInRd) CloseHandle(hStdInRd);

    if (!created) {
        CloseHandle(hStdOutRd);
        CloseHandle(hStdErrRd);
        if (hStdInWr) CloseHandle(hStdInWr);
        std::string errMsg = "CreateProcessW failed for '" + WStringToUtf8(cmdLine) +
                             "': error " + std::to_string(GetLastError());
        Logger::Instance().Error("[RunProcStream] " + errMsg);
        result.stdErr = errMsg;
        return result;
    }

    // 写入 stdin
    if (!stdinInput.empty() && hStdInWr) {
        DWORD written = 0;
        WriteFile(hStdInWr, stdinInput.data(), (DWORD)stdinInput.size(), &written, nullptr);
        CloseHandle(hStdInWr);
    }

    // 流式读取主循环（仅用于 winget install 等 OEM 输出的长命令）
    std::string rawOut, rawErr;
    std::string tailOut, tailErr;
    std::array<char, 4096> buf;
    startTick = GetTickCount();  // 重置计时基准用于流式循环

    // 从累积原始数据提取完整 \n 行，折叠 \r，过滤纯 spinner 行，实时回调
    // 注意：winget 输出已是 UTF-8（进度条含 █▒ 块字符），OEM→UTF-8 仅限于非 UTF-8 行
    auto EmitCompleteLines = [&](std::string& tail, const std::string& chunk) {
        // 快速 UTF-8 有效性检查
        auto IsUtf8 = [](const std::string& s) -> bool {
            size_t i = 0;
            while (i < s.size()) {
                unsigned char c = (unsigned char)s[i];
                if (c < 0x80) { i++; continue; }
                if (c < 0xC2) return false;
                if (c < 0xE0) { if (i + 1 >= s.size() || ((unsigned char)s[i+1] & 0xC0) != 0x80) return false; i += 2; }
                else if (c < 0xF0) { if (i + 2 >= s.size() || ((unsigned char)s[i+1] & 0xC0) != 0x80 || ((unsigned char)s[i+2] & 0xC0) != 0x80) return false; i += 3; }
                else if (c < 0xF8) { if (i + 3 >= s.size() || ((unsigned char)s[i+1] & 0xC0) != 0x80 || ((unsigned char)s[i+2] & 0xC0) != 0x80 || ((unsigned char)s[i+3] & 0xC0) != 0x80) return false; i += 4; }
                else return false;
            }
            return true;
        };

        tail += chunk;
        size_t pos = 0;
        while (pos < tail.size()) {
            size_t nl = tail.find('\n', pos);
            if (nl == std::string::npos) { tail = tail.substr(pos); return; }
            std::string segment = tail.substr(pos, nl - pos);
            pos = nl + 1;

            // 折叠 \r：
            //   若 \r 在行尾（CRLF 行结束）→ 仅去掉尾部 \r
            //   若 \r 在行中（winget spinner）→ 只保留最后一个 \r 之后的内容
            size_t lastCr = segment.rfind('\r');
            std::string final;
            if (lastCr == std::string::npos) {
                final = segment;
            } else if (lastCr == segment.size() - 1) {
                // \r is the very last character — CRLF line ending
                final = segment.substr(0, segment.size() - 1);
            } else {
                // \r is mid-line — spinner overwrite, keep only after last \r
                final = segment.substr(lastCr + 1);
            }

            if (!final.empty()) {
                // winget 输出已是 UTF-8（含 █▒ 块字符），OEM→UTF-8 会破坏它
                if (!IsUtf8(final)) {
                    std::string utf8 = utils::OemToUtf8(final);
                    if (!utf8.empty()) final = std::move(utf8);
                }
            }

            // 跳过纯 spinner 动画行（- \ | / 的单字符动画，winget 以这些行做 spinner）
            if (final.find_first_not_of(" \\/|-") == std::string::npos) continue;

            if (!final.empty() && onLine) onLine(final);
        }
        tail.clear();
    };

    while (true) {
        // 读 stdout
        DWORD availOut = 0;
        if (PeekNamedPipe(hStdOutRd, nullptr, 0, nullptr, &availOut, nullptr) && availOut > 0) {
            DWORD read = 0;
            DWORD toRead = (std::min)(availOut, (DWORD)buf.size() - 1);
            if (ReadFile(hStdOutRd, buf.data(), toRead, &read, nullptr) && read > 0) {
                buf[read] = '\0';
                std::string chunk(buf.data(), read);
                rawOut += chunk;
                EmitCompleteLines(tailOut, chunk);
            }
        }
        // 读 stderr
        DWORD availErr = 0;
        if (PeekNamedPipe(hStdErrRd, nullptr, 0, nullptr, &availErr, nullptr) && availErr > 0) {
            DWORD read = 0;
            DWORD toRead = (std::min)(availErr, (DWORD)buf.size() - 1);
            if (ReadFile(hStdErrRd, buf.data(), toRead, &read, nullptr) && read > 0) {
                buf[read] = '\0';
                std::string chunk(buf.data(), read);
                rawErr += chunk;
                EmitCompleteLines(tailErr, chunk);
            }
        }

        if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0)
            break;

        if (GetTickCount() - startTick > timeoutMs) {
            TerminateProcess(pi.hProcess, 1);
            result.exitCode = (DWORD)-1;
            break;
        }

        Sleep(50);
    }

    // 排空管道残留
    while (true) {
        DWORD read = 0;
        if (!ReadFile(hStdOutRd, buf.data(), (DWORD)buf.size() - 1, &read, nullptr) || read == 0)
            break;
        std::string chunk(buf.data(), read);
        rawOut += chunk;
        EmitCompleteLines(tailOut, chunk);
    }
    while (true) {
        DWORD read = 0;
        if (!ReadFile(hStdErrRd, buf.data(), (DWORD)buf.size() - 1, &read, nullptr) || read == 0)
            break;
        std::string chunk(buf.data(), read);
        rawErr += chunk;
        EmitCompleteLines(tailErr, chunk);
    }
    // 转发末尾不完整行（与 EmitCompleteLines 相同的逻辑）
    {
        auto IsUtf8 = [](const std::string& s) -> bool {
            size_t i = 0;
            while (i < s.size()) {
                unsigned char c = (unsigned char)s[i];
                if (c < 0x80) { i++; continue; }
                if (c < 0xC2) return false;
                if (c < 0xE0) { if (i + 1 >= s.size() || ((unsigned char)s[i+1] & 0xC0) != 0x80) return false; i += 2; }
                else if (c < 0xF0) { if (i + 2 >= s.size() || ((unsigned char)s[i+1] & 0xC0) != 0x80 || ((unsigned char)s[i+2] & 0xC0) != 0x80) return false; i += 3; }
                else if (c < 0xF8) { if (i + 3 >= s.size() || ((unsigned char)s[i+1] & 0xC0) != 0x80 || ((unsigned char)s[i+2] & 0xC0) != 0x80 || ((unsigned char)s[i+3] & 0xC0) != 0x80) return false; i += 4; }
                else return false;
            }
            return true;
        };
        auto ForwardTail = [&](std::string& s) {
            if (s.empty()) return;
            if (!IsUtf8(s)) { std::string u = utils::OemToUtf8(s); if (!u.empty()) s = std::move(u); }
            if (s.find_first_not_of(" \\/|-") != std::string::npos && onLine) onLine(s);
        };
        ForwardTail(tailOut);
        ForwardTail(tailErr);
    }

    if (result.exitCode == (DWORD)-1 && WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0)
        GetExitCodeProcess(pi.hProcess, &result.exitCode);

    // 应用与 RunProcess 相同的编码管道，确保 stdOut/stdErr 为正确 UTF-8
    {
        auto IsValidUtf8 = [](const std::string& s) -> bool {
            size_t i = 0;
            while (i < s.size()) {
                unsigned char c = (unsigned char)s[i];
                if (c < 0x80) { i++; continue; }
                if (c < 0xC2) return false;
                if (c < 0xE0) { if (i + 1 >= s.size() || ((unsigned char)s[i+1] & 0xC0) != 0x80) return false; i += 2; }
                else if (c < 0xF0) { if (i + 2 >= s.size() || ((unsigned char)s[i+1] & 0xC0) != 0x80 || ((unsigned char)s[i+2] & 0xC0) != 0x80) return false; i += 3; }
                else if (c < 0xF8) { if (i + 3 >= s.size() || ((unsigned char)s[i+1] & 0xC0) != 0x80 || ((unsigned char)s[i+2] & 0xC0) != 0x80 || ((unsigned char)s[i+3] & 0xC0) != 0x80) return false; i += 4; }
                else return false;
            }
            return true;
        };
        auto HasHighBytes = [](const std::string& s) -> bool {
            for (unsigned char c : s) if (c > 127) return true;
            return false;
        };
        auto ConvIfNeeded = [&](std::string& s) {
            if (s.empty()) return;
            // UTF-16LE 检测 + 转换
            {
                bool isUtf16 = false;
                if (s.size() >= 2 && (unsigned char)s[0] == 0xFF && (unsigned char)s[1] == 0xFE) isUtf16 = true;
                else if (s.size() >= 4) {
                    size_t on = 0, en = 0;
                    for (size_t i = 0; i + 1 < s.size(); i++) {
                        if (i % 2 == 0) { if (s[i] == '\0') en++; }
                        else { if (s[i] == '\0') on++; }
                    }
                    isUtf16 = on > (s.size() / 2) / 4 && en < (s.size() / 2) / 10;
                }
                if (isUtf16) {
                    size_t len = s.size(); if (len % 2 == 1) len--;
                    int wideLen = (int)(len / 2);
                    int utf8Len = WideCharToMultiByte(CP_UTF8, 0,
                        reinterpret_cast<LPCWCH>(s.data()), wideLen, nullptr, 0, nullptr, nullptr);
                    if (utf8Len > 0) {
                        std::string u(utf8Len, '\0');
                        WideCharToMultiByte(CP_UTF8, 0,
                            reinterpret_cast<LPCWCH>(s.data()), wideLen, u.data(), utf8Len, nullptr, nullptr);
                        s = std::move(u);
                    }
                    return;
                }
            }
            if (!HasHighBytes(s)) return;
            if (!IsValidUtf8(s)) {
                std::string u = OemToUtf8(s);
                if (!u.empty()) s = std::move(u);
                return;
            }
            std::string fromOem = OemToUtf8(s);
            if (!fromOem.empty() && fromOem != s && IsValidUtf8(fromOem)) {
                auto countCjk = [&](const std::string& str) -> int {
                    if (!IsValidUtf8(str)) return 0;
                    int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str.data(), (int)str.size(), nullptr, 0);
                    if (wlen <= 0) return 0;
                    std::wstring wstr(wlen, L'\0');
                    MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), wstr.data(), wlen);
                    int cnt = 0;
                    for (wchar_t wc : wstr) if (wc >= 0x3400 && wc <= 0x9FFF) cnt++;
                    return cnt;
                };
                if (countCjk(fromOem) > 0 && countCjk(s) == 0) s = std::move(fromOem);
            }
        };
        ConvIfNeeded(rawOut);
        ConvIfNeeded(rawErr);
    }

    result.stdOut = std::move(rawOut);
    result.stdErr = std::move(rawErr);

    // [RunProcStream 输出日志 — 已按 UTF-16LE/OEM 正确解码]
    {
        DWORD elapsed = GetTickCount() - startTick;
        std::string preview = DescribeForLog(result.stdOut, "stdout") +
                              DescribeForLog(result.stdErr, "stderr");
        Logger::Instance().Info("[RunProcStream] " + WStringToUtf8(cmdLine) +
                                " exit=" + std::to_string(result.exitCode) +
                                " elapsed=" + std::to_string(elapsed) + "ms " + preview);
    }

    CloseHandle(hStdOutRd);
    CloseHandle(hStdErrRd);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    return result;
}

} // namespace utils
