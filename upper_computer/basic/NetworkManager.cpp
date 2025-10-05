/**
 * @file NetworkManager.cpp
 * @brief 网络通信模块实现文件
 * @details 实现TCP连接管理、数据收发、重连机制等功能
 * @author 系统设计项目组
 * @date 2024
 */

#include "NetworkManager.h"
#include "CryptoUtils.h"
#include "log.h"
#include <QNetworkProxy>
#include <QDebug>

NetworkManager::NetworkManager(QObject *parent)
    : QObject(parent)
    , tcpSocket_(nullptr)
    , currentHost_("")
    , currentPort_(0)
    , retryTimer_(nullptr)
    , retryCount_(0)
    , maxRetries_(5)
    , infiniteRetry_(false)
    , userInitiated_(false)
    , retryInterval_(2000)
    , cryptoUtils_(nullptr)
    , encryptionEnabled_(false)
    , isInitialized_(false)
    , isDestroying_(false)
{
}

NetworkManager::~NetworkManager()
{
    stop();
}

void NetworkManager::initialize()
{
    if (isInitialized_) {
        return;
    }
    
    // 创建TCP套接字
    tcpSocket_ = new QTcpSocket(this);
    
    // 设置套接字选项
    tcpSocket_->setProxy(QNetworkProxy::NoProxy);
    tcpSocket_->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
    
    // 连接信号槽
    connect(tcpSocket_, &QTcpSocket::connected, this, &NetworkManager::onConnected);
    connect(tcpSocket_, &QTcpSocket::disconnected, this, &NetworkManager::onDisconnected);
    connect(tcpSocket_, &QTcpSocket::readyRead, this, &NetworkManager::onDataReceived);
    connect(tcpSocket_, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::errorOccurred),
            this, &NetworkManager::onSocketError);
    
    // 创建重连定时器
    retryTimer_ = new QTimer(this);
    retryTimer_->setSingleShot(true);
    connect(retryTimer_, &QTimer::timeout, this, &NetworkManager::onRetryTimer);
    
    isInitialized_ = true;
    upper_computer::basic::LogManager::info(std::string("网络通信模块初始化完成"));
}

void NetworkManager::stop()
{
    if (!isInitialized_) {
        return;
    }
    
    isDestroying_ = true;
    
    // 取消重连
    cancelRetry();
    
    // 断开连接
    if (tcpSocket_) {
        tcpSocket_->disconnectFromHost();
        tcpSocket_->close();
    }
    
    isInitialized_ = false;
    upper_computer::basic::LogManager::info(std::string("网络通信模块已停止"));
}

void NetworkManager::connectToHost(const QString &host, int port, bool userInitiated)
{
    if (!isInitialized_) {
        upper_computer::basic::LogManager::error(QString("网络管理器未初始化"));
        return;
    }
    
    currentHost_ = host;
    currentPort_ = port;
    userInitiated_ = userInitiated;
    retryCount_ = 0;
    
    // 如果当前已连接，先断开
    if (tcpSocket_->state() != QAbstractSocket::UnconnectedState) {
        tcpSocket_->abort();
    }
    
    upper_computer::basic::LogManager::info(QString("开始连接到 %1:%2").arg(host).arg(port));
    tcpSocket_->connectToHost(host, port);
}

void NetworkManager::disconnectFromHost()
{
    if (tcpSocket_) {
        cancelRetry();
        tcpSocket_->disconnectFromHost();
    }
}

bool NetworkManager::sendCommand(const QString &command)
{
    if (!isConnected()) {
        upper_computer::basic::LogManager::warning(QString("未连接到服务器，无法发送命令: %1").arg(command));
        return false;
    }
    
    QByteArray commandData = command.toUtf8();
    
    // 如果启用加密，则加密数据
    if (encryptionEnabled_) {
        commandData = encryptData(commandData);
        if (commandData.isEmpty()) {
            upper_computer::basic::LogManager::error(QString("命令加密失败: %1").arg(command));
            return false;
        }
    }
    
    // 添加换行符作为命令结束标志
    commandData.append('\n');
    
    qint64 bytesWritten = tcpSocket_->write(commandData);
    if (bytesWritten == commandData.size()) {
        upper_computer::basic::LogManager::info(QString("发送命令成功: %1").arg(command));
        return true;
    } else {
        upper_computer::basic::LogManager::error(QString("发送命令失败: %1").arg(command));
        return false;
    }
}

bool NetworkManager::sendData(const QByteArray &data)
{
    if (!isConnected()) {
        upper_computer::basic::LogManager::warning(QString("未连接到服务器，无法发送数据"));
        return false;
    }
    
    QByteArray dataToSend = data;
    
    // 如果启用加密，则加密数据
    if (encryptionEnabled_) {
        dataToSend = encryptData(data);
        if (dataToSend.isEmpty()) {
            upper_computer::basic::LogManager::error(QString("数据加密失败"));
            return false;
        }
    }
    
    qint64 bytesWritten = tcpSocket_->write(dataToSend);
    if (bytesWritten == dataToSend.size()) {
        upper_computer::basic::LogManager::debug(QString("发送数据成功，大小: %1 字节").arg(dataToSend.size()));
        return true;
    } else {
        upper_computer::basic::LogManager::error(QString("发送数据失败"));
        return false;
    }
}

QAbstractSocket::SocketState NetworkManager::getConnectionState() const
{
    return tcpSocket_ ? tcpSocket_->state() : QAbstractSocket::UnconnectedState;
}

bool NetworkManager::isConnected() const
{
    return tcpSocket_ && tcpSocket_->state() == QAbstractSocket::ConnectedState;
}

void NetworkManager::setCryptoUtils(CryptoUtils *cryptoUtils)
{
    cryptoUtils_ = cryptoUtils;
}

void NetworkManager::setEncryptionEnabled(bool enabled)
{
    encryptionEnabled_ = enabled;
    upper_computer::basic::LogManager::info(QString("加密状态: %1").arg(enabled ? "启用" : "禁用"));
}

bool NetworkManager::isEncryptionEnabled() const
{
    return encryptionEnabled_;
}

void NetworkManager::onConnected()
{
    upper_computer::basic::LogManager::info(QString("已连接到服务器 %1:%2").arg(currentHost_).arg(currentPort_));
    cancelRetry();
    emit connected();
}

void NetworkManager::onDisconnected()
{
    upper_computer::basic::LogManager::info(QString("与服务器 %1:%2 的连接已断开").arg(currentHost_).arg(currentPort_));
    emit disconnected();
}

void NetworkManager::onDataReceived()
{
    QByteArray data = tcpSocket_->readAll();
    upper_computer::basic::LogManager::debug(QString("接收到数据，大小: %1 字节").arg(data.size()));
    
    // 将新数据添加到缓冲区
    dataBuffer_.append(data);
    
    // 尝试解析JSON数据
    parseJsonData(dataBuffer_);
    
    // 发送原始数据信号
    emit dataReceived(data);
}

void NetworkManager::onSocketError(QAbstractSocket::SocketError error)
{
    QString errorString = tcpSocket_->errorString();
    upper_computer::basic::LogManager::error(QString("网络错误: %1").arg(errorString));
    
    emit connectionError(error, errorString);
    
    // 如果不是用户主动断开，则尝试重连
    if (error != QAbstractSocket::RemoteHostClosedError && !isDestroying_) {
        startRetry();
    }
}

void NetworkManager::onRetryTimer()
{
    if (tcpSocket_->state() == QAbstractSocket::ConnectedState) {
        cancelRetry();
        return;
    }
    
    if (!infiniteRetry_ && retryCount_ >= maxRetries_) {
        upper_computer::basic::LogManager::error(QString("重连失败，已达到最大重试次数: %1").arg(maxRetries_));
        cancelRetry();
        return;
    }
    
    retryCount_++;
    upper_computer::basic::LogManager::info(QString("尝试重连 (%1/%2): %3:%4")
        .arg(retryCount_)
        .arg(infiniteRetry_ ? "∞" : QString::number(maxRetries_))
        .arg(currentHost_)
        .arg(currentPort_));
    
    emit retryStatusChanged(retryCount_, maxRetries_);
    
    // 尝试重新连接
    tcpSocket_->connectToHost(currentHost_, currentPort_);
    
    // 设置下次重连定时器
    retryTimer_->start(retryInterval_);
}

void NetworkManager::startRetry()
{
    if (!retryTimer_ || retryTimer_->isActive()) {
        return;
    }
    
    infiniteRetry_ = true; // 默认无限重试
    retryTimer_->start(retryInterval_);
}

void NetworkManager::cancelRetry()
{
    if (retryTimer_) {
        retryTimer_->stop();
    }
    retryCount_ = 0;
    infiniteRetry_ = false;
}

QByteArray NetworkManager::encryptData(const QByteArray &data)
{
    if (!cryptoUtils_ || !encryptionEnabled_) {
        return data;
    }
    
    return cryptoUtils_->encrypt(data);
}

QByteArray NetworkManager::decryptData(const QByteArray &data)
{
    if (!cryptoUtils_ || !encryptionEnabled_) {
        return data;
    }
    
    return cryptoUtils_->decrypt(data);
}

void NetworkManager::parseJsonData(const QByteArray &data)
{
    // 尝试按行分割数据
    QByteArray dataToProcess = data;
    
    // 如果启用加密，先解密
    if (encryptionEnabled_) {
        dataToProcess = decryptData(data);
        if (dataToProcess.isEmpty()) {
            return;
        }
    }
    
    QString dataString = QString::fromUtf8(dataToProcess);
    QStringList lines = dataString.split('\n', Qt::SkipEmptyParts);
    
    for (const QString &line : lines) {
        if (line.trimmed().isEmpty()) {
            continue;
        }
        
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &error);
        
        if (error.error == QJsonParseError::NoError && doc.isObject()) {
            QJsonObject jsonObj = doc.object();
            upper_computer::basic::LogManager::debug(QString("解析到JSON数据: %1").arg(line.left(100)));
            emit jsonDataReceived(jsonObj);
        } else {
            upper_computer::basic::LogManager::debug(QString("非JSON数据: %1").arg(line.left(50)));
        }
    }
    
    // 清空缓冲区
    dataBuffer_.clear();
}
