#pragma once
#include <string>
#include <sstream>

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

class Logger {
public:
    static Logger& Instance();

    void SetLevel(LogLevel level);
    void EnableFileLogging(const std::wstring& filePath);

    void Log(LogLevel level, const std::string& message);
    void Debug(const std::string& message);
    void Info(const std::string& message);
    void Warning(const std::string& message);
    void Error(const std::string& message);

    // 获取日志缓冲区内容（供 UI 日志区域显示）
    std::string GetBuffer() const;
    void ClearBuffer();

private:
    Logger();
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    class Impl;
    Impl* impl_;
};
