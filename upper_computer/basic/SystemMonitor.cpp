/**
 * @file SystemMonitor.cpp
 * @brief 系统监控模块实现文件
 * @details 实现主机状态监控、心跳检测、日志管理等功能
 * @author 系统设计项目组
 * @date 2024
 */

#include "SystemMonitor.h"
#include "log.h"
#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <QProcess>
#include <QTextStream>
#include <QStandardPaths>
#include <QStorageInfo>

SystemMonitor::SystemMonitor(QObject *parent)
    : QObject(parent)
    , hostStatusTimer_(nullptr)
    , heartbeatCheckTimer_(nullptr)
    , logRotateTimer_(nullptr)
    , heartbeatReceived_(false)
    , logFile_(nullptr)
    , logFileMaxSize_(10 * 1024 * 1024)  // 10MB
    , logFileCount_(0)
    , cpuUsage_(0.0)
    , memoryUsage_(0.0)
    , diskUsage_(0.0)
    , isInitialized_(false)
    , isDestroying_(false)
{
}

SystemMonitor::~SystemMonitor()
{
    stop();
}

void SystemMonitor::initialize()
{
    if (isInitialized_) {
        return;
    }
    
    // 初始化日志系统
    initLogging();
    
    // 创建主机状态监控定时器
    hostStatusTimer_ = new QTimer(this);
    connect(hostStatusTimer_, &QTimer::timeout, this, &SystemMonitor::updateHostStatus);
    hostStatusTimer_->start(1000);  // 每秒更新一次
    
    // 创建心跳检测定时器
    heartbeatCheckTimer_ = new QTimer(this);
    connect(heartbeatCheckTimer_, &QTimer::timeout, this, &SystemMonitor::updateHeartbeatStatus);
    heartbeatCheckTimer_->start(1000);  // 每秒检查一次
    
    // 创建日志轮转定时器
    logRotateTimer_ = new QTimer(this);
    connect(logRotateTimer_, &QTimer::timeout, this, &SystemMonitor::rotateLogFile);
    logRotateTimer_->start(60000);  // 每分钟检查一次
    
    // 启动心跳计时器
    heartbeatTimer_.start();
    
    isInitialized_ = true;
    upper_computer::basic::LogManager::info(std::string("系统监控模块初始化完成"));
}

void SystemMonitor::stop()
{
    if (!isInitialized_) {
        return;
    }
    
    isDestroying_ = true;
    
    // 停止所有定时器
    if (hostStatusTimer_) {
        hostStatusTimer_->stop();
        hostStatusTimer_->deleteLater();
        hostStatusTimer_ = nullptr;
    }
    
    if (heartbeatCheckTimer_) {
        heartbeatCheckTimer_->stop();
        heartbeatCheckTimer_->deleteLater();
        heartbeatCheckTimer_ = nullptr;
    }
    
    if (logRotateTimer_) {
        logRotateTimer_->stop();
        logRotateTimer_->deleteLater();
        logRotateTimer_ = nullptr;
    }
    
    // 关闭日志文件
    if (logFile_) {
        logFile_->close();
        delete logFile_;
        logFile_ = nullptr;
    }
    
    isInitialized_ = false;
    upper_computer::basic::LogManager::info(std::string("系统监控模块已停止"));
}

void SystemMonitor::setHeartbeatStatus(bool received, const QDateTime &timestamp)
{
    heartbeatReceived_ = received;
    if (received) {
        lastHeartbeatTime_ = timestamp;
        heartbeatTimer_.restart();
    }
}

double SystemMonitor::getCpuUsage() const
{
    return cpuUsage_;
}

double SystemMonitor::getMemoryUsage() const
{
    return memoryUsage_;
}

double SystemMonitor::getDiskUsage() const
{
    return diskUsage_;
}

void SystemMonitor::writeLog(const QString &message)
{
    if (!logFile_ || !logFile_->isOpen()) {
        return;
    }
    
    QTextStream stream(logFile_);
    stream << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz") 
           << " " << message << Qt::endl;
    stream.flush();
}

void SystemMonitor::updateHostStatus()
{
    if (isDestroying_) {
        return;
    }
    
    // 更新系统状态
    cpuUsage_ = readCpuUsage();
    memoryUsage_ = readMemoryUsage();
    diskUsage_ = readDiskUsage();
    
    // 发送状态更新信号
    emit statusUpdated(cpuUsage_, memoryUsage_, diskUsage_);
    
    // 调试信息
    static int debugCounter = 0;
    if (++debugCounter % 10 == 0) {  // 每10秒输出一次调试信息
        upper_computer::basic::LogManager::debug(QString("系统状态 - CPU: %1% 内存: %2% 磁盘: %3%")
            .arg(cpuUsage_, 0, 'f', 1)
            .arg(memoryUsage_, 0, 'f', 1)
            .arg(diskUsage_, 0, 'f', 1));
    }
}

void SystemMonitor::updateHeartbeatStatus()
{
    if (isDestroying_) {
        return;
    }
    
    // 发送心跳状态更新信号
    emit heartbeatStatusUpdated(heartbeatReceived_, lastHeartbeatTime_);
    
    // 调试信息
    static int debugCounter = 0;
    if (++debugCounter % 10 == 0) {  // 每10秒输出一次调试信息
        upper_computer::basic::LogManager::debug(QString("心跳状态 - 收到: %1 最后时间: %2")
            .arg(heartbeatReceived_ ? "是" : "否")
            .arg(lastHeartbeatTime_.toString("hh:mm:ss")));
    }
}

void SystemMonitor::rotateLogFile()
{
    if (!logFile_ || !logFile_->isOpen()) {
        return;
    }
    
    // 检查文件大小
    if (logFile_->size() > logFileMaxSize_) {
        logFile_->close();
        logFileCount_++;
        
        // 重命名当前日志文件
        QString oldName = logFile_->fileName();
        QString newName = oldName + QString(".%1").arg(logFileCount_);
        QFile::rename(oldName, newName);
        
        // 创建新的日志文件
        logFile_->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
        
        upper_computer::basic::LogManager::info(QString("日志文件已轮转: %1").arg(newName));
    }
}

void SystemMonitor::initLogging()
{
    // 创建日志目录
    QString appDir = QCoreApplication::applicationDirPath();
    dataDirPath_ = QDir(appDir).filePath("../logs");
    QDir().mkpath(dataDirPath_);
    
    // 创建日志文件
    QString logFileName = QDir(dataDirPath_).filePath(
        QString("communication_%1.log")
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"))
    );
    
    logFile_ = new QFile(logFileName, this);
    if (logFile_->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        upper_computer::basic::LogManager::info(QString("日志文件已创建: %1").arg(logFileName));
    } else {
        upper_computer::basic::LogManager::error(QString("无法创建日志文件: %1").arg(logFileName));
    }
}

double SystemMonitor::readCpuUsage()
{
    // 读取CPU使用率
    QFile file("/proc/stat");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return 0.0;
    }
    
    QTextStream in(&file);
    QString line = in.readLine();
    file.close();
    
    if (line.startsWith("cpu ")) {
        QStringList values = line.split(' ', Qt::SkipEmptyParts);
        if (values.size() >= 8) {
            quint64 user = values[1].toULongLong();
            quint64 nice = values[2].toULongLong();
            quint64 system = values[3].toULongLong();
            quint64 idle = values[4].toULongLong();
            quint64 iowait = values[5].toULongLong();
            quint64 irq = values[6].toULongLong();
            quint64 softirq = values[7].toULongLong();
            quint64 steal = values.size() > 8 ? values[8].toULongLong() : 0;
            
            // 静态变量存储上次的CPU时间统计
            static quint64 lastUser = 0, lastNice = 0, lastSys = 0, lastIdle = 0, 
                           lastIowait = 0, lastIrq = 0, lastSoftirq = 0, lastSteal = 0;
            
            // 计算差值
            quint64 dUser = user - lastUser;
            quint64 dNice = nice - lastNice;
            quint64 dSys = system - lastSys;
            quint64 dIdle = idle - lastIdle;
            quint64 dIowait = iowait - lastIowait;
            quint64 dIrq = irq - lastIrq;
            quint64 dSoftirq = softirq - lastSoftirq;
            quint64 dSteal = steal - lastSteal;
            
            // 更新上次值
            lastUser = user;
            lastNice = nice;
            lastSys = system;
            lastIdle = idle;
            lastIowait = iowait;
            lastIrq = irq;
            lastSoftirq = softirq;
            lastSteal = steal;
            
            // 计算CPU使用率
            quint64 total = dUser + dNice + dSys + dIdle + dIowait + dIrq + dSoftirq + dSteal;
            if (total > 0) {
                quint64 used = total - dIdle;
                return (used * 100.0) / total;
            }
        }
    }
    
    return 0.0;
}

double SystemMonitor::readMemoryUsage()
{
    // 使用QProcess执行awk命令来读取内存使用率（更可靠）
    QProcess process;
    process.start("awk", QStringList() 
        << "/MemTotal:/ {total=$2} /MemAvailable:/ {avail=$2} END {if(total>0) printf \"%.1f\", (total-avail)*100/total}" 
        << "/proc/meminfo");
    process.waitForFinished(1000);
    
    if (process.exitCode() == 0) {
        QString output = process.readAllStandardOutput().trimmed();
        bool ok;
        double usage = output.toDouble(&ok);
        if (ok && usage >= 0 && usage <= 100) {
            // 添加调试信息
            static int debugCounter = 0;
            if (++debugCounter % 30 == 0) {  // 每30秒输出一次调试信息
                upper_computer::basic::LogManager::debug(QString("内存使用率: %1%").arg(usage, 0, 'f', 1));
            }
            return usage;
        }
    }
    
    // 备用方法：直接读取/proc/meminfo文件
    QFile file("/proc/meminfo");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        upper_computer::basic::LogManager::error(QString("无法打开/proc/meminfo文件"));
        return 0.0;
    }
    
    QTextStream in(&file);
    in.setCodec("UTF-8");
    quint64 totalMem = 0, availableMem = 0;
    
    while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.startsWith("MemTotal:")) {
            QStringList parts = line.split(' ', Qt::SkipEmptyParts);
            if (parts.size() >= 2) {
                totalMem = parts[1].toULongLong() * 1024; // kB转字节
            }
        } else if (line.startsWith("MemAvailable:")) {
            QStringList parts = line.split(' ', Qt::SkipEmptyParts);
            if (parts.size() >= 2) {
                availableMem = parts[1].toULongLong() * 1024; // kB转字节
            }
        }
    }
    file.close();
    
    if (totalMem > 0) {
        quint64 usedMem = totalMem - availableMem;
        double usage = (usedMem * 100.0) / totalMem;
        
        // 添加调试信息
        static int debugCounter = 0;
        if (++debugCounter % 30 == 0) {  // 每30秒输出一次调试信息
            upper_computer::basic::LogManager::debug(QString("备用方法 - 总内存: %1 MB, 可用: %2 MB, 使用: %3 MB, 使用率: %4%")
                .arg(totalMem / (1024 * 1024))
                .arg(availableMem / (1024 * 1024))
                .arg(usedMem / (1024 * 1024))
                .arg(usage, 0, 'f', 1));
        }
        
        return usage;
    }
    
    upper_computer::basic::LogManager::error(QString("无法读取内存信息"));
    return 0.0;
}

double SystemMonitor::readDiskUsage()
{
    // 读取磁盘使用率
    QStorageInfo storage = QStorageInfo::root();
    if (storage.isValid() && storage.isReady()) {
        qint64 total = storage.bytesTotal();
        qint64 available = storage.bytesAvailable();
        if (total > 0) {
            return ((total - available) * 100.0) / total;
        }
    }
    
    return 0.0;
}
