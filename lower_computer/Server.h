/**
 * @file Server.h
 * @brief 下位机TCP服务器头文件
 * @details 定义下位机服务器类，包含TCP服务器、客户端管理、数据发送、
 *          命令处理、光谱数据管理等功能
 * @author 系统设计项目组
 * @date 2024
 */

#pragma once

// Qt核心模块
#include <QMainWindow>      // 主窗口基类
#include <QTimer>           // 定时器
#include <QDebug>           // 调试输出
#include <QDateTime>        // 日期时间
#include <QHostAddress>     // 网络地址
#include <QMap>             // 映射容器
#include <QStatusBar>       // 状态栏

// 网络通信
#include <QTcpServer>       // TCP服务器
#include <QTcpSocket>       // TCP套接字

// JSON数据处理
#include <QJsonDocument>    // JSON文档
#include <QJsonObject>      // JSON对象
#include <QJsonArray>       // JSON数组

// 加密模块
#include "CryptoUtils.h"    // 加密工具类

// UI布局
#include <QVBoxLayout>      // 垂直布局
#include <QHBoxLayout>      // 水平布局
#include <QGridLayout>      // 网格布局

// UI控件
#include <QLabel>           // 标签
#include <QLineEdit>        // 单行文本输入框
#include <QPushButton>      // 按钮
#include <QTextEdit>        // 文本编辑框
#include <QGroupBox>        // 分组框
#include <QTableWidget>     // 表格控件
#include <QHeaderView>      // 表头视图
#include <QProgressBar>     // 进度条
#include <QSpinBox>         // 数字输入框
#include <QCheckBox>        // 复选框
#include <QMessageBox>      // 消息框

// 文件处理
#include <QFile>            // 文件操作
#include <QTextStream>      // 文本流
#include <QDir>             // 目录操作
#include <QFileInfo>        // 文件信息
#include <QStringList>      // 字符串列表

// 系统相关
#include <QCoreApplication> // 核心应用程序
#include <iostream>         // 标准输入输出
#include <algorithm>        // 算法库
#include <QRandomGenerator> // 随机数生成器

/**
 * @class LowerComputerServer
 * @brief 下位机TCP服务器主类
 * @details 继承自QMainWindow，实现TCP服务器功能，包括客户端连接管理、
 *          数据发送、命令处理、光谱数据管理等功能
 */
class LowerComputerServer : public QMainWindow
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父窗口指针
     */
    explicit LowerComputerServer(QWidget *parent = nullptr);
    
    /**
     * @brief 析构函数
     * @details 清理资源，关闭日志文件
     */
    ~LowerComputerServer();
    
    // === 加密相关公共函数 ===
    /**
     * @brief 启用/禁用加密
     * @param enabled 是否启用加密
     * @param password 加密密码（可选，为空时使用默认密码）
     * @return 设置是否成功
     * @details 设置通信加密状态和密码
     */
    bool setEncryption(bool enabled, const QString &password = QString());
    
    /**
     * @brief 获取加密状态
     * @return 是否启用加密
     */
    bool isEncryptionEnabled() const;
    
    /**
     * @brief 获取加密状态信息
     * @return 加密状态描述字符串
     */
    QString getEncryptionStatus() const;

private slots:
    // === 服务器控制相关槽函数 ===
    /**
     * @brief 服务器启动/停止按钮点击事件
     * @details 根据当前状态执行启动或停止服务器操作
     */
    void onStartServerClicked();
    
    // === 客户端连接管理相关槽函数 ===
    /**
     * @brief 新客户端连接事件
     * @details 处理新客户端连接，设置信号槽，发送欢迎消息
     */
    void onNewConnection();
    
    /**
     * @brief 客户端断开连接事件
     * @details 清理客户端相关数据，停止相关定时器
     */
    void onClientDisconnected();
    
    /**
     * @brief 数据接收事件
     * @details 接收并处理客户端发送的数据和命令
     */
    void onDataReceived();
    
    // === 数据发送相关槽函数 ===
    /**
     * @brief 手动发送数据按钮点击事件
     * @details 手动发送数据到所有连接的客户端
     */
    void onSendDataClicked();
    
    /**
     * @brief 自动发送开关切换事件
     * @param enabled 是否启用自动发送
     * @details 根据开关状态启动或停止自动数据发送
     */
    void onAutoSendToggled(bool enabled);
    
    /**
     * @brief 发送间隔改变事件
     * @details 当发送间隔改变时重新配置定时器
     */
    void onIntervalChanged();
    
    // === 界面操作相关槽函数 ===
    /**
     * @brief 清空日志按钮点击事件
     * @details 清空日志显示区域
     */
    void onClearLogClicked();
    
    /**
     * @brief 清空数据按钮点击事件
     * @details 清空数据表格显示
     */
    void onClearDataClicked();

private:
    // === 界面初始化相关私有函数 ===
    /**
     * @brief 设置用户界面
     * @details 初始化主窗口界面布局和控件
     */
    void setupUI();
    
    /**
     * @brief 创建控制面板
     * @return 控制面板控件指针
     * @details 创建服务器控制、数据发送等控制面板
     */
    QWidget* createControlPanel();
    
    /**
     * @brief 创建数据面板
     * @return 数据面板控件指针
     * @details 创建数据显示、客户端列表等数据面板
     */
    QWidget* createDataPanel();
    
    /**
     * @brief 设置信号槽连接
     * @details 连接各种信号和槽函数
     */
    void setupConnections();
    
    /**
     * @brief 设置TCP服务器
     * @details 初始化TCP服务器和定时器
     */
    void setupServer();
    
    // === 数据发送相关私有函数 ===
    /**
     * @brief 发送数据到所有客户端
     * @details 向所有连接的客户端发送传感器数据
     */
    void sendDataToClients();
    
    /**
     * @brief 更新数据显示
     * @param data 传感器数据JSON对象
     * @details 在界面上显示传感器数据
     */
    void updateDataDisplay(const QJsonObject &data);
    
    /**
     * @brief 更新客户端列表
     * @details 更新客户端列表表格显示
     */
    void updateClientList();
    
    // === 命令处理相关私有函数 ===
    /**
     * @brief 处理客户端命令
     * @param client 客户端套接字
     * @param command 命令字符串
     * @details 解析并执行客户端发送的命令
     */
    void processClientCommand(QTcpSocket *client, const QString &command);
    
    // === 光谱数据相关私有函数 ===
    /**
     * @brief 加载光谱数据文件
     * @param fileName 文件名
     * @return 是否加载成功
     * @details 从CSV文件加载光谱数据
     */
    bool loadSpectrumData(const QString &fileName);
    
    /**
     * @brief 发送光谱数据到客户端
     * @param client 客户端套接字
     * @details 向指定客户端发送完整的光谱数据
     */
    void sendSpectrumDataToClient(QTcpSocket *client);
    
    /**
     * @brief 发送传感器数据到客户端
     * @param client 客户端套接字
     * @details 向指定客户端发送传感器数据
     */
    void sendSensorDataToClient(QTcpSocket *client);
    
    // === 命令处理相关私有函数 ===
    /**
     * @brief 处理设置采集参数命令
     * @param client 客户端套接字
     * @param obj 命令JSON对象
     * @details 处理客户端发送的采集参数设置命令
     */
    void handleSetAcqCommand(QTcpSocket *client, const QJsonObject &obj);
    
    /**
     * @brief 处理请求暗电流命令
     * @param client 客户端套接字
     * @details 处理客户端请求暗电流校准数据的命令
     */
    void handleReqDark(QTcpSocket *client);
    
    /**
     * @brief 处理请求白参考命令
     * @param client 客户端套接字
     * @details 处理客户端请求白参考校准数据的命令
     */
    void handleReqWhite(QTcpSocket *client);
    
    /**
     * @brief 发送设备状态到客户端
     * @param client 客户端套接字
     * @details 向指定客户端发送设备状态信息
     */
    void sendDeviceStatusTo(QTcpSocket *client);
    
    /**
     * @brief 发送心跳到客户端
     * @param client 客户端套接字
     * @details 向指定客户端发送心跳包
     */
    void sendHeartbeatTo(QTcpSocket *client);

    // === 传感器数据流式发送相关私有函数 ===
    /**
     * @brief 开始传感器数据流
     * @param client 客户端套接字
     * @details 为指定客户端开始传感器数据流式发送
     */
    void startSensorDataStream(QTcpSocket *client);
    
    /**
     * @brief 停止传感器数据流
     * @param client 客户端套接字
     * @details 为指定客户端停止传感器数据流式发送
     */
    void stopSensorDataStream(QTcpSocket *client);
    
    /**
     * @brief 发送传感器数据流
     * @details 向所有订阅的客户端发送传感器数据流
     */
    void sendSensorDataStream();

    // === 光谱数据流式发送相关私有函数 ===
    /**
     * @brief 开始光谱数据流
     * @param client 客户端套接字
     * @details 为指定客户端开始光谱数据流式发送
     */
    void startSpectrumDataStream(QTcpSocket *client);
    
    /**
     * @brief 停止光谱数据流
     * @param client 客户端套接字
     * @details 为指定客户端停止光谱数据流式发送
     */
    void stopSpectrumDataStream(QTcpSocket *client);
    
    /**
     * @brief 发送光谱数据流
     * @details 向所有订阅的客户端发送光谱数据流
     */
    void sendSpectrumDataStream();

private:
    // === 网络通信相关成员变量 ===
    QTcpServer *tcpServer;        // TCP服务器
    QList<QTcpSocket*> clients;   // 客户端套接字列表
    
    // === 加密相关成员变量 ===
    CryptoUtils *cryptoUtils;     // 加密工具实例
    bool encryptionEnabled;       // 是否启用加密
    QString encryptionPassword;   // 加密密码
    
    // === 定时器相关成员变量 ===
    QTimer *dataTimer;            // 数据发送定时器
    QTimer *spectrumStreamTimer;  // 光谱数据流式发送定时器
    QTimer *sensorStreamTimer;    // 传感器数据流式发送定时器
    QTimer *deviceStatusTimer;    // 设备状态周期发送定时器
    QTimer *heartbeatTimer;       // 心跳发送定时器
    
    // === 光谱数据相关成员变量 ===
    QStringList spectrumFiles;    // 光谱文件列表
    QString currentSpectrumFile;  // 当前光谱文件
    QJsonArray wavelengthData;    // 波长数据
    QJsonArray spectrumData;      // 当前行的光谱数据
    QVector<QJsonArray> spectrumRows; // 所有光谱行数据
    int currentSpectrumRowIndex = 0;  // 当前光谱行索引
    
    // === 客户端状态管理相关成员变量 ===
    QMap<QTcpSocket*, int> spectrumStreamClients;  // 客户端对应的当前光谱行索引
    QMap<QTcpSocket*, bool> spectrumStreamActive;  // 客户端是否正在接收光谱数据流
    QMap<QTcpSocket*, bool> sensorStreamActive;    // 客户端是否接收传感器数据流
    QMap<QTcpSocket*, bool> deviceStatusActive;    // 客户端是否接收设备状态
    QMap<QTcpSocket*, QDateTime> lastHeartbeatTime; // 客户端最后心跳时间

    // === UI组件相关成员变量 ===
    QPushButton *startButton;     // 启动/停止服务器按钮
    QSpinBox *portSpinBox;        // 端口号输入框
    QSpinBox *intervalSpinBox;    // 发送间隔输入框
    QCheckBox *autoSendCheckBox;  // 自动发送复选框
    QTableWidget *clientTable;    // 客户端列表表格
    QTableWidget *dataTable;      // 数据表格
    QTextEdit *logText;           // 日志文本框
    QLabel *statusLabel;          // 状态标签
    QLabel *connectionCountLabel; // 连接数标签

    // === 设备状态相关成员变量 ===
    QDateTime serverStartTime;    // 服务器启动时间

    // === 采集设置相关成员变量 ===
    int integrationTimeMs = 100;  // 积分时间（毫秒）
    int averageCount = 10;        // 平均次数
    
    // === 日志文件相关成员变量 ===
    QFile *logFile;               // 日志文件对象
    static const qint64 logFileMaxSize = 10 * 1024 * 1024; // 日志文件最大大小（10MB）
    
    // === 日志相关私有函数 ===
    /**
     * @brief 初始化日志文件
     * @details 创建日志文件并设置文件路径
     */
    void initializeLogFile();
    
    /**
     * @brief 写入日志到文件
     * @param message 日志消息
     * @details 将日志消息写入日志文件，不在终端显示
     */
    void writeToLog(const QString &message);
    
    /**
     * @brief 轮转日志文件
     * @details 当日志文件超过最大大小时，创建新的日志文件
     */
    void rotateLogFile();
    
    // === 加密相关私有函数 ===
    /**
     * @brief 初始化加密系统
     * @details 创建加密工具实例并设置默认密钥
     */
    void initializeEncryption();
    
    /**
     * @brief 加密数据
     * @param data 要加密的数据
     * @return 加密后的数据，失败时返回空数据
     * @details 如果加密未启用则直接返回原数据
     */
    QByteArray encryptData(const QByteArray &data);
    
    /**
     * @brief 解密数据
     * @param data 要解密的数据
     * @return 解密后的数据，失败时返回空数据
     * @details 如果加密未启用则直接返回原数据
     */
    QByteArray decryptData(const QByteArray &data);
    
};


