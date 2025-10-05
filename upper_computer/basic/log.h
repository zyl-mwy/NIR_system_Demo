#pragma once
#include <string>
#include <functional>
#include <QString>

namespace upper_computer {
namespace basic {

/**
 * @brief 日志级别枚举
 */
enum class LogLevel {
    Debug = 0,
    Info = 1,
    Warning = 2,
    Error = 3
};

/**
 * @brief 日志管理器类
 */
class LogManager {
public:
    /**
     * @brief 设置日志回调函数
     * @param callback 日志回调函数
     */
    static void setLogCallback(std::function<void(const std::string&, LogLevel)> callback);
    
    /**
     * @brief 输出调试日志
     * @param message 日志消息
     */
    static void debug(const std::string& message);
    
    /**
     * @brief 输出调试日志（QString版本）
     * @param message 日志消息
     */
    static void debug(const QString& message);
    
    /**
     * @brief 输出信息日志
     * @param message 日志消息
     */
    static void info(const std::string& message);
    
    /**
     * @brief 输出信息日志（QString版本）
     * @param message 日志消息
     */
    static void info(const QString& message);
    
    /**
     * @brief 输出警告日志
     * @param message 日志消息
     */
    static void warning(const std::string& message);
    
    /**
     * @brief 输出警告日志（QString版本）
     * @param message 日志消息
     */
    static void warning(const QString& message);
    
    /**
     * @brief 输出错误日志
     * @param message 日志消息
     */
    static void error(const std::string& message);
    
    /**
     * @brief 输出错误日志（QString版本）
     * @param message 日志消息
     */
    static void error(const QString& message);
    
    /**
     * @brief 输出日志（带级别）
     * @param message 日志消息
     * @param level 日志级别
     */
    static void log(const std::string& message, LogLevel level = LogLevel::Info);

private:
    static std::function<void(const std::string&, LogLevel)> log_callback_;
    static bool has_callback_;
};

} // namespace basic
} // namespace upper_computer
