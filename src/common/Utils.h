#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>
#include <sstream>
#include <functional>

namespace utils {

// 直接运行可执行文件（不通过 PowerShell），输出从 OEM 自动转 UTF-8
struct ProcessResult {
    DWORD exitCode;
    std::string stdOut;
    std::string stdErr;
};
ProcessResult RunProcess(const std::wstring& executable,
                          const std::wstring& args,
                          const std::string& stdinInput,
                          DWORD timeoutMs);

// 流式版本：每从管道读到完整行即回调 onLine(line)，同时累加到 ProcessResult
// 适合 winget/wsl --install 等长时间命令，debug 窗口和日志可实时看到进度
ProcessResult RunProcessStreaming(const std::wstring& executable,
                                   const std::wstring& args,
                                   const std::string& stdinInput,
                                   DWORD timeoutMs,
                                   std::function<void(const std::string& line)> onLine);

// 字符串分割
inline std::vector<std::string> Split(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::istringstream stream(s);
    std::string token;
    while (std::getline(stream, token, delimiter)) {
        if (!token.empty()) tokens.push_back(token);
    }
    return tokens;
}

// 去除首尾空白
inline std::string Trim(const std::string& s) {
    if (s.empty()) return s;
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r' || s[start] == '\n'))
        start++;
    size_t end = s.size();
    while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r' || s[end - 1] == '\n'))
        end--;
    return s.substr(start, end - start);
}

// 去除首尾空白（wstring 版本）
inline std::wstring TrimW(const std::wstring& s) {
    if (s.empty()) return s;
    size_t start = 0;
    while (start < s.size() && (s[start] == L' ' || s[start] == L'\t' || s[start] == L'\r' || s[start] == L'\n'))
        start++;
    size_t end = s.size();
    while (end > start && (s[end - 1] == L' ' || s[end - 1] == L'\t' || s[end - 1] == L'\r' || s[end - 1] == L'\n'))
        end--;
    return s.substr(start, end - start);
}

// 判断字符串是否以 prefix 开头
inline bool StartsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

// UTF-8 与 UTF-16 互转
std::string WStringToUtf8(const std::wstring& wstr);
std::wstring Utf8ToWString(const std::string& str);

// OEM 代码页 → UTF-8（用于 powershell.exe 输出）
std::string OemToUtf8(const std::string& oemStr);

// 比较点分版本号（如 "5.10.60.1"、"2.0.9.0"）。
// 返回 <0 表示 lhs < rhs，0 表示相等，>0 表示 lhs > rhs。
// 非数字后缀会被忽略（如 "5.10.60.1-microsoft-standard-WSL2"）。
int CompareVersion(const std::string& lhs, const std::string& rhs);

// 替换字符串
inline std::string Replace(const std::string& s, const std::string& from, const std::string& to) {
    std::string result = s;
    size_t pos = 0;
    while ((pos = result.find(from, pos)) != std::string::npos) {
        result.replace(pos, from.length(), to);
        pos += to.length();
    }
    return result;
}

} // namespace utils
