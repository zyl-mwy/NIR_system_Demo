/**
 * @file SystemMonitor.h
 * @brief 系统监控模块头文件
 * @details 提供主机状态监控、心跳检测、日志管理等功能
 * @author 系统设计项目组
 * @date 2024
 */

#pragma once

#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QString>

/**
 * @class SystemMonitor
 * @brief 系统监控类
 * @details 负责监控系统状态、心跳检测、日志管理等功能
 */
class SystemMonitor : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父对象指针
     */
    explicit SystemMonitor(QObject *parent = nullptr);
    
    /**
     * @brief 析构函数
     */
    ~SystemMonitor();
    
    /**
     * @brief 初始化系统监控
     * @details 启动定时器，初始化日志系统
     */
    void initialize();
    
    /**
     * @brief 停止系统监控
     * @details 停止所有定时器，清理资源
     */
    void stop();
    
    /**
     * @brief 设置心跳状态
     * @param received 是否收到心跳
     * @param timestamp 心跳时间戳
     */
    void setHeartbeatStatus(bool received, const QDateTime &timestamp);
    
    /**
     * @brief 获取CPU使用率
     * @return CPU使用率百分比
     */
    double getCpuUsage() const;
    
    /**
     * @brief 获取内存使用率
     * @return 内存使用率百分比
     */
    double getMemoryUsage() const;
    
    /**
     * @brief 获取磁盘使用率
     * @return 磁盘使用率百分比
     */
    double getDiskUsage() const;
    
    /**
     * @brief 写入日志
     * @param message 日志消息
     */
    void writeLog(const QString &message);

signals:
    /**
     * @brief 系统状态更新信号
     * @param cpuUsage CPU使用率
     * @param memoryUsage 内存使用率
     * @param diskUsage 磁盘使用率
     */
    void statusUpdated(double cpuUsage, double memoryUsage, double diskUsage);
    
    /**
     * @brief 心跳状态更新信号
     * @param received 是否收到心跳
     * @param lastTime 最后心跳时间
     */
    void heartbeatStatusUpdated(bool received, const QDateTime &lastTime);

private slots:
    /**
     * @brief 更新主机状态
     * @details 定时更新CPU、内存、磁盘使用率
     */
    void updateHostStatus();
    
    /**
     * @brief 更新心跳状态
     * @details 检查心跳状态并更新显示
     */
    void updateHeartbeatStatus();
    
    /**
     * @brief 轮转日志文件
     * @details 当日志文件过大时进行轮转
     */
    void rotateLogFile();

private:
    /**
     * @brief 初始化日志系统
     * @details 创建日志目录和文件
     */
    void initLogging();
    
    /**
     * @brief 读取CPU使用率
     * @return CPU使用率百分比
     */
    double readCpuUsage();
    
    /**
     * @brief 读取内存使用率
     * @return 内存使用率百分比
     */
    double readMemoryUsage();
    
    /**
     * @brief 读取磁盘使用率
     * @return 磁盘使用率百分比
     */
    double readDiskUsage();

private:
    // 定时器
    QTimer *hostStatusTimer_;        // 主机状态更新定时器
    QTimer *heartbeatCheckTimer_;    // 心跳检测定时器
    QTimer *logRotateTimer_;         // 日志轮转定时器
    
    // 心跳状态
    bool heartbeatReceived_;         // 是否收到心跳
    QDateTime lastHeartbeatTime_;    // 最后心跳时间
    QElapsedTimer heartbeatTimer_;   // 心跳计时器
    
    // 日志系统
    QFile *logFile_;                 // 日志文件对象
    QString dataDirPath_;            // 数据目录路径
    int logFileMaxSize_;             // 日志文件最大大小
    int logFileCount_;               // 日志文件计数器
    
    // 系统状态
    double cpuUsage_;                // CPU使用率
    double memoryUsage_;             // 内存使用率
    double diskUsage_;               // 磁盘使用率
    
    // 状态标志
    bool isInitialized_;             // 是否已初始化
    bool isDestroying_;              // 是否正在析构
};
