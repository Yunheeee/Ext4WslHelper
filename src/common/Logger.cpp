#include "Logger.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vector>
#include <mutex>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>

class Logger::Impl {
public:
    Impl() : level_(LogLevel::Debug) {}

    void SetLevel(LogLevel level) { level_ = level; }

    void EnableFileLogging(const std::wstring& filePath) {
        std::lock_guard<std::mutex> lock(mutex_);
        fileStream_.open(filePath, std::ios::app);
    }

    void Log(LogLevel level, const std::string& message) {
        if (level < level_) return;

        std::string entry = FormatEntry(level, message);

        // OutputDebugString
        OutputDebugStringA(entry.c_str());

        // 写文件
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (fileStream_.is_open()) {
                fileStream_ << entry;
                fileStream_.flush();
            }
        }

        // 环形缓冲区
        {
            std::lock_guard<std::mutex> lock(mutex_);
            buffer_ += entry;
            if (buffer_.size() > 65536) {
                buffer_.erase(0, buffer_.size() - 49152);
            }
        }
    }

    std::string GetBuffer() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return buffer_;
    }

    void ClearBuffer() {
        std::lock_guard<std::mutex> lock(mutex_);
        buffer_.clear();
    }

private:
    std::string FormatEntry(LogLevel level, const std::string& message) {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        std::tm tm;
        localtime_s(&tm, &t);

        std::ostringstream ss;
        ss << "[" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "]"
           << "  " << message << "\r\n";
        return ss.str();
    }

    LogLevel level_;
    mutable std::mutex mutex_;
    std::ofstream fileStream_;
    std::string buffer_;
};

Logger::Logger() : impl_(new Impl()) {}
Logger::~Logger() { delete impl_; }

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

void Logger::SetLevel(LogLevel level) { impl_->SetLevel(level); }
void Logger::EnableFileLogging(const std::wstring& filePath) { impl_->EnableFileLogging(filePath); }

void Logger::Log(LogLevel level, const std::string& message) { impl_->Log(level, message); }
void Logger::Debug(const std::string& message) { impl_->Log(LogLevel::Debug, message); }
void Logger::Info(const std::string& message) { impl_->Log(LogLevel::Info, message); }
void Logger::Warning(const std::string& message) { impl_->Log(LogLevel::Warning, message); }
void Logger::Error(const std::string& message) { impl_->Log(LogLevel::Error, message); }

std::string Logger::GetBuffer() const { return impl_->GetBuffer(); }
void Logger::ClearBuffer() { impl_->ClearBuffer(); }
