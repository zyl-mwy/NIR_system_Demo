#include "log.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <QDebug>

namespace upper_computer {
namespace basic {

std::function<void(const std::string&, LogLevel)> LogManager::log_callback_ = nullptr;
bool LogManager::has_callback_ = false;

void LogManager::setLogCallback(std::function<void(const std::string&, LogLevel)> callback)
{
    log_callback_ = callback;
    has_callback_ = (callback != nullptr);
}

void LogManager::debug(const std::string& message)
{
    log(message, LogLevel::Debug);
}

void LogManager::debug(const QString& message)
{
    log(message.toStdString(), LogLevel::Debug);
}

void LogManager::info(const std::string& message)
{
    log(message, LogLevel::Info);
}

void LogManager::info(const QString& message)
{
    log(message.toStdString(), LogLevel::Info);
}

void LogManager::warning(const std::string& message)
{
    log(message, LogLevel::Warning);
}

void LogManager::warning(const QString& message)
{
    log(message.toStdString(), LogLevel::Warning);
}

void LogManager::error(const std::string& message)
{
    log(message, LogLevel::Error);
}

void LogManager::error(const QString& message)
{
    log(message.toStdString(), LogLevel::Error);
}

void LogManager::log(const std::string& message, LogLevel level)
{
    if (has_callback_) {
        log_callback_(message, level);
    } else {
        // 默认输出到控制台
        const char* level_str = "INFO";
        switch (level) {
            case LogLevel::Debug:   level_str = "DEBUG"; break;
            case LogLevel::Info:    level_str = "INFO"; break;
            case LogLevel::Warning: level_str = "WARN"; break;
            case LogLevel::Error:   level_str = "ERROR"; break;
        }
        
        // 获取当前时间
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%H:%M:%S");
        ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
        
        std::cout << "[" << ss.str() << "] [" << level_str << "] " << message << std::endl;
    }
}

} // namespace basic
} // namespace upper_computer
