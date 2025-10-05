/**
 * @file NetworkManager.h
 * @brief 网络通信模块头文件
 * @details 提供TCP连接管理、数据收发、重连机制等功能
 * @author 系统设计项目组
 * @date 2024
 */

#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QByteArray>
#include <QString>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

// 前向声明
class CryptoUtils;

/**
 * @class NetworkManager
 * @brief 网络通信管理类
 * @details 负责与下位机的TCP通信，包括连接管理、数据收发、重连机制等
 */
class NetworkManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父对象指针
     */
    explicit NetworkManager(QObject *parent = nullptr);
    
    /**
     * @brief 析构函数
     */
    ~NetworkManager();
    
    /**
     * @brief 初始化网络管理器
     * @details 创建TCP套接字，设置信号槽连接
     */
    void initialize();
    
    /**
     * @brief 停止网络管理器
     * @details 断开连接，停止定时器，清理资源
     */
    void stop();
    
    /**
     * @brief 连接到服务器
     * @param host 服务器地址
     * @param port 服务器端口
     * @param userInitiated 是否由用户发起
     * @details 开始连接过程，支持自动重试
     */
    void connectToHost(const QString &host, int port, bool userInitiated = true);
    
    /**
     * @brief 断开连接
     * @details 主动断开与服务器的连接
     */
    void disconnectFromHost();
    
    /**
     * @brief 发送命令
     * @param command 命令字符串
     * @return 是否发送成功
     * @details 发送命令到服务器，支持加密
     */
    bool sendCommand(const QString &command);
    
    /**
     * @brief 发送数据
     * @param data 数据字节数组
     * @return 是否发送成功
     * @details 发送原始数据到服务器
     */
    bool sendData(const QByteArray &data);
    
    /**
     * @brief 获取连接状态
     * @return 当前连接状态
     */
    QAbstractSocket::SocketState getConnectionState() const;
    
    /**
     * @brief 是否已连接
     * @return 是否已连接到服务器
     */
    bool isConnected() const;
    
    /**
     * @brief 设置加密工具
     * @param cryptoUtils 加密工具实例
     * @details 设置用于数据加密的工具
     */
    void setCryptoUtils(CryptoUtils *cryptoUtils);
    
    /**
     * @brief 设置加密状态
     * @param enabled 是否启用加密
     */
    void setEncryptionEnabled(bool enabled);
    
    /**
     * @brief 获取加密状态
     * @return 是否启用加密
     */
    bool isEncryptionEnabled() const;

signals:
    /**
     * @brief 连接成功信号
     * @details 当成功连接到服务器时发出
     */
    void connected();
    
    /**
     * @brief 连接断开信号
     * @details 当与服务器的连接断开时发出
     */
    void disconnected();
    
    /**
     * @brief 数据接收信号
     * @param data 接收到的数据
     * @details 当接收到数据时发出
     */
    void dataReceived(const QByteArray &data);
    
    /**
     * @brief JSON数据接收信号
     * @param jsonData JSON对象
     * @details 当接收到有效的JSON数据时发出
     */
    void jsonDataReceived(const QJsonObject &jsonData);
    
    /**
     * @brief 连接错误信号
     * @param error 错误类型
     * @param errorString 错误描述
     * @details 当连接发生错误时发出
     */
    void connectionError(QAbstractSocket::SocketError error, const QString &errorString);
    
    /**
     * @brief 重连状态信号
     * @param retryCount 当前重试次数
     * @param maxRetries 最大重试次数
     * @details 当重连状态发生变化时发出
     */
    void retryStatusChanged(int retryCount, int maxRetries);

private slots:
    /**
     * @brief 处理连接成功
     * @details 内部槽函数，处理TCP连接成功事件
     */
    void onConnected();
    
    /**
     * @brief 处理连接断开
     * @details 内部槽函数，处理TCP连接断开事件
     */
    void onDisconnected();
    
    /**
     * @brief 处理数据接收
     * @details 内部槽函数，处理TCP数据接收事件
     */
    void onDataReceived();
    
    /**
     * @brief 处理连接错误
     * @param error 错误类型
     * @details 内部槽函数，处理TCP连接错误事件
     */
    void onSocketError(QAbstractSocket::SocketError error);
    
    /**
     * @brief 处理重连定时器
     * @details 内部槽函数，处理重连定时器超时事件
     */
    void onRetryTimer();

private:
    /**
     * @brief 开始重连
     * @details 开始重连过程
     */
    void startRetry();
    
    /**
     * @brief 取消重连
     * @details 取消重连过程
     */
    void cancelRetry();
    
    /**
     * @brief 加密数据
     * @param data 要加密的数据
     * @return 加密后的数据
     */
    QByteArray encryptData(const QByteArray &data);
    
    /**
     * @brief 解密数据
     * @param data 要解密的数据
     * @return 解密后的数据
     */
    QByteArray decryptData(const QByteArray &data);
    
    /**
     * @brief 解析JSON数据
     * @param data 原始数据
     * @details 尝试从原始数据中解析JSON
     */
    void parseJsonData(const QByteArray &data);

private:
    // 网络相关
    QTcpSocket *tcpSocket_;           // TCP套接字
    QString currentHost_;             // 当前连接的主机
    int currentPort_;                 // 当前连接的端口
    
    // 重连机制
    QTimer *retryTimer_;              // 重连定时器
    int retryCount_;                  // 当前重试次数
    int maxRetries_;                  // 最大重试次数
    bool infiniteRetry_;              // 是否无限重试
    bool userInitiated_;              // 是否由用户发起
    int retryInterval_;               // 重连间隔（毫秒）
    
    // 加密相关
    CryptoUtils *cryptoUtils_;        // 加密工具实例
    bool encryptionEnabled_;          // 是否启用加密
    
    // 数据缓冲
    QByteArray dataBuffer_;           // 数据接收缓冲区
    
    // 状态标志
    bool isInitialized_;              // 是否已初始化
    bool isDestroying_;               // 是否正在析构
};
