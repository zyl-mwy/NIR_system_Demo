/**
 * @file Server.cpp
 * @brief 下位机TCP服务器实现文件
 * @details 实现下位机TCP服务器功能，包括客户端连接管理、数据发送、
 *          命令处理、光谱数据加载等功能
 * @author 系统设计项目组
 * @date 2024
 */

#include "Server.h"

/**
 * @brief 下位机服务器构造函数
 * @param parent 父窗口指针
 * @details 初始化下位机服务器界面，设置TCP服务器，加载光谱数据
 */
LowerComputerServer::LowerComputerServer(QWidget *parent) : QMainWindow(parent)
{
    // 0. 初始化成员变量
    logFile = nullptr;
    
    // 初始化加密系统
    initializeEncryption();
    
    // 1. 初始化用户界面
    setupUI();
    setupConnections();
    
    // 2. 设置TCP服务器
    setupServer();
    
    // 3. 加载光谱数据文件
    loadSpectrumData("diesel_spec.csv");

    // 4. 设置状态栏
    statusLabel = new QLabel("服务器未启动");
    statusBar()->addWidget(statusLabel);
    connectionCountLabel = new QLabel("连接数: 0");
    statusBar()->addPermanentWidget(connectionCountLabel);

    // 5. 设置窗口属性
    setWindowTitle("下位机服务器 - TCP通信系统");
    setMinimumSize(800, 600);
    resize(900, 700);

    // 6. 初始化日志文件
    initializeLogFile();
    
    // 7. 延迟1秒后自动启动服务器
    QTimer::singleShot(1000, this, &LowerComputerServer::onStartServerClicked);
    serverStartTime = QDateTime::currentDateTime();
}

/**
 * @brief 服务器启动/停止按钮点击事件处理
 * @details 根据当前服务器状态，执行启动或停止操作
 */
void LowerComputerServer::onStartServerClicked()
{
    if (tcpServer->isListening()) {
        // 如果服务器正在监听，则停止服务器
        tcpServer->close();
        dataTimer->stop();          // 停止数据发送定时器
        heartbeatTimer->stop();     // 停止心跳定时器
        startButton->setText("启动服务器");
        statusLabel->setText("服务器已停止");
        logText->append(QString("[%1] 服务器已停止").arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
    } else {
        // 如果服务器未启动，则尝试启动服务器
        int port = portSpinBox->value();
        if (tcpServer->listen(QHostAddress::Any, port)) {
            // 启动成功
            startButton->setText("停止服务器");
            statusLabel->setText(QString("服务器运行中 - 端口: %1").arg(port));
            heartbeatTimer->start();  // 启动心跳定时器
            logText->append(QString("[%1] 服务器启动成功，监听端口: %2")
                           .arg(QDateTime::currentDateTime().toString("hh:mm:ss")).arg(port));
        } else {
            // 启动失败，显示错误信息
            QMessageBox::critical(this, "错误", "服务器启动失败:\n" + tcpServer->errorString());
        }
    }
}

/**
 * @brief 新客户端连接事件处理
 * @details 处理新的客户端连接，设置信号槽，发送欢迎消息
 */
void LowerComputerServer::onNewConnection()
{
    // 获取新连接的客户端套接字
    QTcpSocket *clientSocket = tcpServer->nextPendingConnection();
    if (!clientSocket) return;
    
    // 获取客户端信息（IP地址和端口）
    QString clientInfo = QString("%1:%2").arg(clientSocket->peerAddress().toString()).arg(clientSocket->peerPort());
    writeToLog(QString("新客户端连接: %1").arg(clientInfo));
    
    // 连接客户端套接字的信号槽
    connect(clientSocket, &QTcpSocket::disconnected, this, &LowerComputerServer::onClientDisconnected);
    connect(clientSocket, &QTcpSocket::readyRead, this, &LowerComputerServer::onDataReceived);
    
    // 将客户端添加到客户端列表
    clients.append(clientSocket);
    updateClientList();
    
    // 发送欢迎消息给新客户端
    QString welcomeMsg = "欢迎连接到下位机服务器！";
    QByteArray welcomeData = welcomeMsg.toUtf8();
    clientSocket->write(welcomeData);
    clientSocket->write("\n");
    
    // 记录连接日志
    logText->append(QString("[%1] 新客户端连接: %2").arg(QDateTime::currentDateTime().toString("hh:mm:ss")).arg(clientInfo));
}

/**
 * @brief 客户端断开连接事件处理
 * @details 清理客户端相关数据，停止相关定时器，更新客户端列表
 */
void LowerComputerServer::onClientDisconnected()
{
    // 获取断开连接的客户端套接字
    QTcpSocket *clientSocket = qobject_cast<QTcpSocket*>(sender());
    if (!clientSocket) return;
    
    // 获取客户端信息
    QString clientInfo = QString("%1:%2").arg(clientSocket->peerAddress().toString()).arg(clientSocket->peerPort());
    writeToLog(QString("客户端断开连接: %1").arg(clientInfo));
    
    // 清理所有相关的客户端映射和状态
    clients.removeAll(clientSocket);                    // 从客户端列表移除
    spectrumStreamActive.remove(clientSocket);          // 移除光谱流状态
    spectrumStreamClients.remove(clientSocket);         // 移除光谱流客户端
    sensorStreamActive.remove(clientSocket);            // 移除传感器流状态
    deviceStatusActive.remove(clientSocket);            // 移除设备状态流状态
    
    // 如果没有其他客户端，停止相关的定时器
    if (spectrumStreamActive.isEmpty()) spectrumStreamTimer->stop();
    if (sensorStreamActive.isEmpty()) sensorStreamTimer->stop();
    if (deviceStatusActive.isEmpty()) deviceStatusTimer->stop();
    
    // 更新客户端列表显示
    updateClientList();
    
    // 记录断开连接日志
    logText->append(QString("[%1] 客户端断开连接: %2").arg(QDateTime::currentDateTime().toString("hh:mm:ss")).arg(clientInfo));
    
    // 延迟删除客户端对象，确保所有信号处理完成
    clientSocket->deleteLater();
}

/**
 * @brief 数据接收事件处理
 * @details 接收客户端发送的数据，按行分割并处理命令
 */
void LowerComputerServer::onDataReceived()
{
    // 获取发送数据的客户端套接字
    QTcpSocket *clientSocket = qobject_cast<QTcpSocket*>(sender());
    if (!clientSocket) return;
    
    // 读取所有接收到的数据
    QByteArray data = clientSocket->readAll();
    QString clientInfo = QString("%1:%2").arg(clientSocket->peerAddress().toString()).arg(clientSocket->peerPort());
    
    // 按换行符分割数据，逐行处理
    QList<QByteArray> lines = data.split('\n');
    for (const QByteArray &line : lines) {
        if (line.trimmed().isEmpty()) continue;  // 跳过空行
        
        // 如果启用加密，则解密数据
        QByteArray decryptedData = line;
        if (encryptionEnabled) {
            decryptedData = decryptData(line);
            if (decryptedData.isEmpty()) {
                writeToLog("❌ 数据解密失败，跳过此条命令");
                continue;
            }
        }
        
        // 将字节数组转换为字符串
        QString decryptedLine = QString::fromUtf8(decryptedData);
        
        // 处理客户端命令
        processClientCommand(clientSocket, decryptedLine);
        
        // 记录接收到的命令到日志
        logText->append(QString("[%1] 来自 %2: %3").arg(QDateTime::currentDateTime().toString("hh:mm:ss")).arg(clientInfo).arg(decryptedLine.trimmed()));
    }
}

/**
 * @brief 手动发送数据按钮点击事件
 * @details 检查客户端连接状态，手动发送数据到所有客户端
 */
void LowerComputerServer::onSendDataClicked()
{
    if (clients.isEmpty()) {
        QMessageBox::information(this, "提示", "没有连接的客户端！");
        return;
    }
    sendDataToClients();
}

/**
 * @brief 自动发送开关切换事件
 * @param enabled 是否启用自动发送
 * @details 根据开关状态启动或停止自动数据发送定时器
 */
void LowerComputerServer::onAutoSendToggled(bool enabled)
{
    if (enabled && tcpServer->isListening()) {
        // 启用自动发送，启动定时器
        dataTimer->start(intervalSpinBox->value() * 1000);
        logText->append(QString("[%1] 自动发送已启用，间隔: %2秒").arg(QDateTime::currentDateTime().toString("hh:mm:ss")).arg(intervalSpinBox->value()));
    } else {
        // 禁用自动发送，停止定时器
        dataTimer->stop();
        logText->append(QString("[%1] 自动发送已禁用").arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
    }
}

/**
 * @brief 发送间隔改变事件
 * @details 当发送间隔改变时，如果自动发送已启用，则重新启动定时器
 */
void LowerComputerServer::onIntervalChanged()
{
    if (autoSendCheckBox->isChecked() && tcpServer->isListening()) {
        dataTimer->start(intervalSpinBox->value() * 1000);
    }
}

/**
 * @brief 清空日志按钮点击事件
 * @details 清空日志显示区域
 */
void LowerComputerServer::onClearLogClicked()
{
    logText->clear();
}

void LowerComputerServer::onClearDataClicked()
{
    dataTable->clearContents();
    for (int i = 0; i < dataTable->rowCount(); ++i) {
        dataTable->setItem(i, 1, new QTableWidgetItem("--"));
    }
}

void LowerComputerServer::setupUI()
{
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    QHBoxLayout *mainLayout = new QHBoxLayout(centralWidget);
    QWidget *controlPanel = createControlPanel();
    mainLayout->addWidget(controlPanel, 1);
    QWidget *dataPanel = createDataPanel();
    mainLayout->addWidget(dataPanel, 2);
}

QWidget* LowerComputerServer::createControlPanel()
{
    QWidget *panel = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(panel);
    QGroupBox *serverGroup = new QGroupBox("服务器控制");
    QGridLayout *serverLayout = new QGridLayout(serverGroup);
    serverLayout->addWidget(new QLabel("端口:"), 0, 0);
    portSpinBox = new QSpinBox();
    portSpinBox->setRange(1, 65535);
    portSpinBox->setValue(8888);
    serverLayout->addWidget(portSpinBox, 0, 1);
    startButton = new QPushButton("启动服务器");
    serverLayout->addWidget(startButton, 1, 0, 1, 2);
    layout->addWidget(serverGroup);

    QGroupBox *dataGroup = new QGroupBox("数据发送");
    QVBoxLayout *dataLayout = new QVBoxLayout(dataGroup);
    QHBoxLayout *intervalLayout = new QHBoxLayout();
    intervalLayout->addWidget(new QLabel("发送间隔(秒):"));
    intervalSpinBox = new QSpinBox();
    intervalSpinBox->setRange(1, 60);
    intervalSpinBox->setValue(2);
    intervalLayout->addWidget(intervalSpinBox);
    dataLayout->addLayout(intervalLayout);
    autoSendCheckBox = new QCheckBox("自动发送数据");
    autoSendCheckBox->setChecked(true);
    dataLayout->addWidget(autoSendCheckBox);
    QPushButton *sendButton = new QPushButton("立即发送数据");
    dataLayout->addWidget(sendButton);
    layout->addWidget(dataGroup);

    QGroupBox *clientGroup = new QGroupBox("客户端列表");
    QVBoxLayout *clientLayout = new QVBoxLayout(clientGroup);
    clientTable = new QTableWidget(0, 2);
    clientTable->setHorizontalHeaderLabels(QStringList() << "客户端地址" << "端口");
    clientTable->horizontalHeader()->setStretchLastSection(true);
    clientTable->setAlternatingRowColors(true);
    clientTable->setMaximumHeight(150);
    clientLayout->addWidget(clientTable);
    layout->addWidget(clientGroup);

    QGroupBox *logGroup = new QGroupBox("日志控制");
    QHBoxLayout *logControlLayout = new QHBoxLayout(logGroup);
    QPushButton *clearLogButton = new QPushButton("清空日志");
    QPushButton *clearDataButton = new QPushButton("清空数据");
    logControlLayout->addWidget(clearLogButton);
    logControlLayout->addWidget(clearDataButton);
    layout->addWidget(logGroup);

    connect(sendButton, &QPushButton::clicked, this, &LowerComputerServer::onSendDataClicked);
    connect(clearLogButton, &QPushButton::clicked, this, &LowerComputerServer::onClearLogClicked);
    connect(clearDataButton, &QPushButton::clicked, this, &LowerComputerServer::onClearDataClicked);
    return panel;
}

QWidget* LowerComputerServer::createDataPanel()
{
    QWidget *panel = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(panel);
    QGroupBox *dataGroup = new QGroupBox("传感器数据");
    QVBoxLayout *dataLayout = new QVBoxLayout(dataGroup);
    dataTable = new QTableWidget(5, 2);
    dataTable->setHorizontalHeaderLabels(QStringList() << "参数" << "数值");
    dataTable->verticalHeader()->setVisible(false);
    dataTable->horizontalHeader()->setStretchLastSection(true);
    dataTable->setAlternatingRowColors(true);
    QStringList labels = {"时间戳", "温度(°C)", "湿度(%)", "气压(hPa)", "状态"};
    for (int i = 0; i < labels.size(); ++i) {
        dataTable->setItem(i, 0, new QTableWidgetItem(labels[i]));
        dataTable->setItem(i, 1, new QTableWidgetItem("--"));
    }
    dataLayout->addWidget(dataTable);
    layout->addWidget(dataGroup);
    QGroupBox *logGroup = new QGroupBox("通信日志");
    QVBoxLayout *logLayout = new QVBoxLayout(logGroup);
    logText = new QTextEdit();
    logText->setReadOnly(true);
    logText->setMaximumHeight(200);
    logLayout->addWidget(logText);
    layout->addWidget(logGroup);
    return panel;
}

void LowerComputerServer::setupConnections()
{
    connect(startButton, &QPushButton::clicked, this, &LowerComputerServer::onStartServerClicked);
    connect(autoSendCheckBox, &QCheckBox::toggled, this, &LowerComputerServer::onAutoSendToggled);
    connect(intervalSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &LowerComputerServer::onIntervalChanged);
}

void LowerComputerServer::setupServer()
{
    tcpServer = new QTcpServer(this);
    connect(tcpServer, &QTcpServer::newConnection, this, &LowerComputerServer::onNewConnection);
    dataTimer = new QTimer(this);
    connect(dataTimer, &QTimer::timeout, this, &LowerComputerServer::sendDataToClients);
    spectrumStreamTimer = new QTimer(this);
    connect(spectrumStreamTimer, &QTimer::timeout, this, &LowerComputerServer::sendSpectrumDataStream);
    sensorStreamTimer = new QTimer(this);
    sensorStreamTimer->setInterval(5000);
    connect(sensorStreamTimer, &QTimer::timeout, this, &LowerComputerServer::sendSensorDataStream);

    // 设备状态周期发送（5s）
    deviceStatusTimer = new QTimer(this);
    deviceStatusTimer->setInterval(5000);
    connect(deviceStatusTimer, &QTimer::timeout, [this]() {
        for (auto it = deviceStatusActive.begin(); it != deviceStatusActive.end(); ) {
            QTcpSocket *client = it.key();
            if (!client || client->state() != QAbstractSocket::ConnectedState) { 
                it = deviceStatusActive.erase(it); 
                continue; 
            }
            // 检查客户端是否仍然有效
            if (client->parent() == nullptr) {
                it = deviceStatusActive.erase(it);
                continue;
            }
            sendDeviceStatusTo(client); 
            ++it;
        }
        if (deviceStatusActive.isEmpty()) deviceStatusTimer->stop();
    });

    // 心跳发送（3s）
    heartbeatTimer = new QTimer(this);
    heartbeatTimer->setInterval(3000);
    connect(heartbeatTimer, &QTimer::timeout, [this]() {
        for (auto it = clients.begin(); it != clients.end(); ) {
            QTcpSocket *client = *it;
            if (!client || client->state() != QAbstractSocket::ConnectedState) {
                it = clients.erase(it);
                lastHeartbeatTime.remove(client);
                continue;
            }
            // 检查客户端是否仍然有效
            if (client->parent() == nullptr) {
                it = clients.erase(it);
                lastHeartbeatTime.remove(client);
                continue;
            }
            sendHeartbeatTo(client);
            lastHeartbeatTime[client] = QDateTime::currentDateTime();
            ++it;
        }
        // 即使没有客户端也保持心跳定时器运行，以便新连接时能立即发送心跳
    });
}
void LowerComputerServer::sendDataToClients()
{
    if (clients.isEmpty()) {
        return;
    }

    QJsonObject dataToSend;
    dataToSend["timestamp"] = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    dataToSend["type"] = "spectrum_data";

    if (!wavelengthData.isEmpty() && !spectrumData.isEmpty()) {
        dataToSend["wavelengths"] = wavelengthData;
        dataToSend["spectrum_values"] = spectrumData;
        dataToSend["file_name"] = currentSpectrumFile;
        dataToSend["data_points"] = spectrumData.size();
    } else {
        dataToSend["type"] = "sensor_data";
        dataToSend["temperature"] = 20.0 + (QRandomGenerator::global()->bounded(100)) / 10.0;
        dataToSend["humidity"] = 40.0 + (QRandomGenerator::global()->bounded(400)) / 10.0;
        dataToSend["pressure"] = 1013.0 + (QRandomGenerator::global()->bounded(100)) / 10.0;
        dataToSend["status"] = "normal";
    }

    QJsonDocument doc(dataToSend);
    QByteArray data = doc.toJson(QJsonDocument::Compact);
    
    // 如果启用加密，则加密数据
    if (encryptionEnabled) {
        data = encryptData(data);
        if (data.isEmpty()) {
            writeToLog("❌ 数据加密失败，跳过发送");
            return;
        }
    }

    for (QTcpSocket *client : clients) {
        if (client->state() == QAbstractSocket::ConnectedState) {
            client->write(data);
            client->write("\n");
        }
    }

    updateDataDisplay(dataToSend);

    QString dataType = dataToSend["type"].toString();
    logText->append(QString("[%1] 发送%2给 %3 个客户端")
                   .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                   .arg(dataType == "spectrum_data" ? "光谱数据" : "传感器数据")
                   .arg(clients.size()));
}
void LowerComputerServer::updateDataDisplay(const QJsonObject &data)
{
    int row = 0;
    dataTable->setItem(row++, 1, new QTableWidgetItem(data["timestamp"].toString()));

    QString dataType = data["type"].toString();
    if (dataType == "spectrum_data") {
        dataTable->setItem(row++, 1, new QTableWidgetItem("光谱数据"));
        dataTable->setItem(row++, 1, new QTableWidgetItem(data["file_name"].toString()));
        dataTable->setItem(row++, 1, new QTableWidgetItem(QString::number(data["data_points"].toInt()) + " 个数据点"));
        dataTable->setItem(row++, 1, new QTableWidgetItem("已加载"));
    } else {
        dataTable->setItem(row++, 1, new QTableWidgetItem(QString::number(data["temperature"].toDouble(), 'f', 1) + " °C"));
        dataTable->setItem(row++, 1, new QTableWidgetItem(QString::number(data["humidity"].toDouble(), 'f', 1) + " %"));
        dataTable->setItem(row++, 1, new QTableWidgetItem(QString::number(data["pressure"].toDouble(), 'f', 1) + " hPa"));
        dataTable->setItem(row++, 1, new QTableWidgetItem(data["status"].toString()));
    }
}
void LowerComputerServer::updateClientList()
{
    clientTable->setRowCount(clients.size());
    for (int i = 0; i < clients.size(); ++i) {
        QTcpSocket *client = clients[i];
        clientTable->setItem(i, 0, new QTableWidgetItem(client->peerAddress().toString()));
        clientTable->setItem(i, 1, new QTableWidgetItem(QString::number(client->peerPort())));
    }

    connectionCountLabel->setText(QString("连接数: %1").arg(clients.size()));
}
void LowerComputerServer::processClientCommand(QTcpSocket *client, const QString &command)
{
    QString trimmed = command.trimmed();
    QString response;

    if (trimmed == "GET_STATUS") {
        response = "下位机状态: 运行正常";
    } else if (trimmed == "GET_VERSION") {
        response = "下位机版本: v1.0.0";
    } else if (trimmed == "RESTART") {
        response = "下位机重启命令已接收";
    } else if (trimmed == "STOP_DATA") {
        dataTimer->stop();
        response = "数据发送已停止";
    } else if (trimmed == "START_DATA") {
        if (tcpServer->isListening()) {
            dataTimer->start(intervalSpinBox->value() * 1000);
            response = "数据发送已开始";
        } else {
            response = "服务器未启动";
        }
    } else if (trimmed == "GET_SPECTRUM") {
        writeToLog("Processing GET_SPECTRUM command");
        sendSpectrumDataToClient(client);
        response = "光谱数据已发送";
    } else if (trimmed == "GET_SPECTRUM_STREAM") {
        writeToLog("Processing GET_SPECTRUM_STREAM command");
        startSpectrumDataStream(client);
        response = "开始流式发送光谱数据";
    } else if (trimmed == "STOP_SPECTRUM_STREAM") {
        writeToLog("Processing STOP_SPECTRUM_STREAM command");
        stopSpectrumDataStream(client);
        response = "停止流式发送光谱数据";
    } else if (trimmed == "GET_SENSOR_DATA") {
        writeToLog("Processing GET_SENSOR_DATA command (start sensor stream)");
        startSensorDataStream(client);
        response = "开始周期性发送传感器数据(5s)";
    } else if (trimmed == "STOP_SENSOR_STREAM") {
        writeToLog("Processing STOP_SENSOR_STREAM command");
        stopSensorDataStream(client);
        response = "停止传感器数据流";
    } else if (trimmed.startsWith("{")) {
        // 尝试解析JSON指令（例如 SET_ACQ）
        QJsonParseError err; QJsonDocument doc = QJsonDocument::fromJson(trimmed.toUtf8(), &err);
        if (err.error == QJsonParseError::NoError && doc.isObject()) {
            QJsonObject obj = doc.object();
            const QString type = obj.value("type").toString();
            if (type == "SET_ACQ") {
                handleSetAcqCommand(client, obj);
                return;
            } else if (type == "REQ_DARK") {
                handleReqDark(client);
                return;
            } else if (type == "REQ_WHITE") {
                handleReqWhite(client);
                return;
            } else if (type == "GET_DEVICE_STATUS") {
                sendDeviceStatusTo(client);
                return;
            } else if (type == "START_DEVICE_STATUS_STREAM") {
                deviceStatusActive[client] = true; if (!deviceStatusTimer->isActive()) deviceStatusTimer->start();
                client->write("已开始设备状态流\n");
                return;
            } else if (type == "STOP_DEVICE_STATUS_STREAM") {
                deviceStatusActive.remove(client); if (deviceStatusActive.isEmpty()) deviceStatusTimer->stop();
                client->write("已停止设备状态流\n");
                return;
            }
        }
        response = "未知命令: " + trimmed;
    } else {
        response = "未知命令: " + trimmed;
    }

    QByteArray responseData = response.toUtf8();
    client->write(responseData);
    client->write("\n");
}
void LowerComputerServer::handleReqDark(QTcpSocket *client)
{
    // 5秒后基于当前光谱数据生成一个假定暗电流：取当前行(或第一行)的某个低幅度比例，例如 5% 的值
    QTimer::singleShot(5000, this, [this, client]() {
        QJsonObject resp; resp["type"] = "DARK_DATA"; resp["timestamp"] = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
        if (!wavelengthData.isEmpty()) resp["wavelengths"] = wavelengthData;
        QJsonArray dark;
        if (!spectrumRows.isEmpty()) {
            const QJsonArray &row = spectrumRows[0];
            for (int i = 0; i < row.size(); ++i) dark.append(row[i].toDouble() * 0.05); // 5%
        } else if (!spectrumData.isEmpty()) {
            for (int i = 0; i < spectrumData.size(); ++i) dark.append(spectrumData[i].toDouble() * 0.05);
        }
        resp["spectrum_values"] = dark;
        QJsonDocument doc(resp); QByteArray data = doc.toJson(QJsonDocument::Compact);
        if (client && client->state() == QAbstractSocket::ConnectedState) { client->write(data); client->write("\n"); }
        logText->append(QString("[%1] 返回假定暗电流").arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
    });
}

void LowerComputerServer::handleReqWhite(QTcpSocket *client)
{
    // 5秒后基于当前光谱数据生成一个假定白参考：将当前行提升到接近最大值(归一化后约0.9-1.0区间)
    QTimer::singleShot(5000, this, [this, client]() {
        QJsonObject resp; resp["type"] = "WHITE_DATA"; resp["timestamp"] = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
        if (!wavelengthData.isEmpty()) resp["wavelengths"] = wavelengthData;
        QJsonArray white;
        if (!spectrumRows.isEmpty()) {
            const QJsonArray &row = spectrumRows[0];
            double maxV = 0.0; for (int i = 0; i < row.size(); ++i) maxV = std::max(maxV, row[i].toDouble());
            double target = (maxV > 0.0) ? maxV * 0.95 : 1.0;
            for (int i = 0; i < row.size(); ++i) white.append(target);
        } else if (!spectrumData.isEmpty()) {
            double maxV = 0.0; for (int i = 0; i < spectrumData.size(); ++i) maxV = std::max(maxV, spectrumData[i].toDouble());
            double target = (maxV > 0.0) ? maxV * 0.95 : 1.0;
            for (int i = 0; i < spectrumData.size(); ++i) white.append(target);
        }
        resp["spectrum_values"] = white;
        QJsonDocument doc(resp); QByteArray data = doc.toJson(QJsonDocument::Compact);
        if (client && client->state() == QAbstractSocket::ConnectedState) { client->write(data); client->write("\n"); }
        logText->append(QString("[%1] 返回假定白参考").arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
    });
}

void LowerComputerServer::handleSetAcqCommand(QTcpSocket *client, const QJsonObject &obj)
{
    // 解析积分时间与平均次数
    int newIntegration = obj.value("integration_ms").toInt(integrationTimeMs);
    int newAverage = obj.value("average").toInt(averageCount);
    integrationTimeMs = qBound(1, newIntegration, 60000);
    averageCount = qBound(1, newAverage, 1000);

    // 在界面日志显示
    logText->append(QString("[%1] 接收采集设置: 积分=%2ms, 平均=%3")
                   .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                   .arg(integrationTimeMs)
                   .arg(averageCount));

    // 回执JSON
    QJsonObject ack;
    ack["type"] = "SET_ACQ_ACK";
    ack["timestamp"] = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    ack["integration_ms"] = integrationTimeMs;
    ack["average"] = averageCount;
    QJsonDocument doc(ack); QByteArray data = doc.toJson(QJsonDocument::Compact);
    client->write(data); client->write("\n");
}
bool LowerComputerServer::loadSpectrumData(const QString &fileName)
{
    QString exeDir = QCoreApplication::applicationDirPath();
    QString candidate1 = QDir(exeDir + "/../data").filePath(fileName);
    QString candidate2 = QDir("data").filePath(fileName);
    QString candidate3 = QFileInfo(fileName).isAbsolute() ? fileName : QString();

    QString usedPath;
    QFile file;
    QStringList candidates;
    candidates << candidate1 << candidate2;
    if (!candidate3.isEmpty()) candidates << candidate3;

    for (const QString &path : candidates) {
        QFileInfo fi(path);
        writeToLog(QString("尝试打开光谱文件: %1").arg(fi.absoluteFilePath()));
        file.setFileName(fi.absoluteFilePath());
        if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            usedPath = fi.absoluteFilePath();
            break;
        }
    }

    if (usedPath.isEmpty()) {
        writeToLog(QString("无法打开文件: 尝试路径为 %1").arg(candidates.join(", ")));
        return false;
    }
    writeToLog(QString("使用光谱文件: %1").arg(usedPath));

    QTextStream in(&file);
    QStringList lines;
    while (!in.atEnd()) {
        lines.append(in.readLine());
    }
    file.close();

    if (lines.size() < 11) {
        writeToLog("文件行数不足，需要至少11行");
        return false;
    }

    wavelengthData = QJsonArray();
    spectrumData = QJsonArray();
    spectrumRows.clear();

    if (fileName.endsWith(".csv")) {
        QStringList wavelengthLine = lines[9].split(',');
        if (wavelengthLine.size() < 3) {
            writeToLog("第10行格式错误，无法解析波长信息");
            return false;
        }
        for (int i = 2; i < wavelengthLine.size(); ++i) {
            bool ok;
            double wavelength = wavelengthLine[i].toDouble(&ok);
            if (ok) {
                wavelengthData.append(wavelength);
            }
        }
        for (int i = 10; i < lines.size(); ++i) {
            QStringList dataLine = lines[i].split(',');
            if (dataLine.size() >= 2) {
                QJsonArray rowArray;
                for (int j = 1; j < dataLine.size() && j - 1 < wavelengthData.size(); ++j) {
                    bool ok;
                    double spectrumValue = dataLine[j].toDouble(&ok);
                    if (ok) {
                        rowArray.append(spectrumValue);
                    }
                }
                if (!rowArray.isEmpty()) {
                    spectrumRows.push_back(rowArray);
                }
            }
        }
        if (!spectrumRows.isEmpty()) {
            spectrumData = spectrumRows[0];
            currentSpectrumRowIndex = 0;
        }
    } else {
        QStringList wavelengthLine = lines[9].split('\t');
        if (wavelengthLine.size() < 3) {
            writeToLog("第10行格式错误，无法解析波段信息");
            return false;
        }
        for (int i = 10; i < lines.size(); ++i) {
            QStringList dataLine = lines[i].split('\t');
            if (dataLine.size() >= 2) {
                bool ok1, ok2;
                double wavelength = dataLine[0].toDouble(&ok1);
                double spectrumValue = dataLine[1].toDouble(&ok2);
                if (ok1 && ok2) {
                    wavelengthData.append(wavelength);
                    spectrumData.append(spectrumValue);
                }
            }
        }
    }

    writeToLog(QString("成功加载光谱数据，波长点数: %1, 光谱数据点数: %2").arg(wavelengthData.size()).arg(spectrumData.size()));

    currentSpectrumFile = fileName;

    return !wavelengthData.isEmpty() && (!spectrumData.isEmpty() || !spectrumRows.isEmpty());
}
void LowerComputerServer::sendSpectrumDataToClient(QTcpSocket *client)
{
    writeToLog("sendSpectrumDataToClient called");
    if (!client || client->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    writeToLog(QString("Wavelength data size: %1").arg(wavelengthData.size()));
    writeToLog(QString("Spectrum rows count: %1").arg(spectrumRows.size()));
    writeToLog(QString("Current spectrum file: %1").arg(currentSpectrumFile));

    QJsonObject dataToSend;
    dataToSend["timestamp"] = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    dataToSend["type"] = "spectrum_data";

    if (!wavelengthData.isEmpty() && !spectrumRows.isEmpty()) {
        const QJsonArray &row = spectrumRows[currentSpectrumRowIndex];
        dataToSend["wavelengths"] = wavelengthData;
        dataToSend["spectrum_values"] = row;
        dataToSend["file_name"] = currentSpectrumFile;
        dataToSend["data_points"] = row.size();
        dataToSend["row_index"] = currentSpectrumRowIndex;
        dataToSend["total_rows"] = static_cast<int>(spectrumRows.size());

        QJsonDocument doc(dataToSend);
        QByteArray data = doc.toJson(QJsonDocument::Compact);

        writeToLog(QString("Sending spectrum data: %1").arg(QString(data).left(100) + "..."));

        writeToLog("=== 发送光谱数据 ===");
        writeToLog(QString("文件: %1").arg(currentSpectrumFile));
        writeToLog(QString("行: %1/%2").arg(currentSpectrumRowIndex+1).arg(spectrumRows.size()));
        writeToLog(QString("数据点数: %1").arg(row.size()));
        writeToLog(QString("波长范围: %1 - %2 nm").arg(wavelengthData[0].toDouble()).arg(wavelengthData[wavelengthData.size()-1].toDouble()));
        double minValue = row[0].toDouble();
        double maxValue = row[0].toDouble();
        for (int i = 1; i < row.size(); ++i) {
            double value = row[i].toDouble();
            if (value < minValue) minValue = value;
            if (value > maxValue) maxValue = value;
        }
        writeToLog(QString("光谱值范围: %1 - %2").arg(minValue).arg(maxValue));
        writeToLog("前10个数据点:");
        for (int i = 0; i < qMin(10, wavelengthData.size()); ++i) {
            writeToLog(QString("  %1. 波长: %2 nm, 光谱值: %3").arg(i+1).arg(wavelengthData[i].toDouble()).arg(row[i].toDouble()));
        }
        if (wavelengthData.size() > 10) {
            writeToLog("后10个数据点:");
            for (int i = qMax(0, wavelengthData.size() - 10); i < wavelengthData.size(); ++i) {
                writeToLog(QString("  %1. 波长: %2 nm, 光谱值: %3").arg(i+1).arg(wavelengthData[i].toDouble()).arg(row[i].toDouble()));
            }
        }
        writeToLog("===================");

        client->write(data);
        client->write("\n");
        updateDataDisplay(dataToSend);
        logText->append(QString("[%1] 发送光谱数据给客户端: %2:%3")
                       .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                       .arg(client->peerAddress().toString())
                       .arg(client->peerPort()));

        currentSpectrumRowIndex = (currentSpectrumRowIndex + 1) % spectrumRows.size();
    } else {
        QJsonObject errorData;
        errorData["timestamp"] = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
        errorData["type"] = "error";
        errorData["message"] = "没有可用的光谱数据";
        QJsonDocument doc(errorData);
        QByteArray data = doc.toJson(QJsonDocument::Compact);
        client->write(data);
        client->write("\n");
    }
}
void LowerComputerServer::sendSensorDataToClient(QTcpSocket *client)
{
    if (!client || client->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    QJsonObject dataToSend;
    dataToSend["timestamp"] = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    dataToSend["type"] = "sensor_data";
    dataToSend["temperature"] = 20.0 + (QRandomGenerator::global()->bounded(100)) / 10.0;
    dataToSend["humidity"] = 40.0 + (QRandomGenerator::global()->bounded(400)) / 10.0;
    dataToSend["pressure"] = 1013.0 + (QRandomGenerator::global()->bounded(100)) / 10.0;
    dataToSend["status"] = "normal";

    QJsonDocument doc(dataToSend);
    QByteArray data = doc.toJson(QJsonDocument::Compact);
    client->write(data);
    client->write("\n");
    updateDataDisplay(dataToSend);
    logText->append(QString("[%1] 发送传感器数据给客户端: %2:%3")
                   .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                   .arg(client->peerAddress().toString())
                   .arg(client->peerPort()));
}

void LowerComputerServer::sendDeviceStatusTo(QTcpSocket *client)
{
    if (!client || client->state() != QAbstractSocket::ConnectedState) return;
    // 模拟设备状态
    QJsonObject st;
    st["type"] = "device_status";
    st["timestamp"] = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    st["device_temp"] = 30.0 + (QRandomGenerator::global()->bounded(200)) / 10.0; // 30-50
    st["lamp_temp"] = 35.0 + (QRandomGenerator::global()->bounded(200)) / 10.0;   // 35-55
    st["detector"] = (QRandomGenerator::global()->bounded(100) < 95) ? "ok" : "fault";
    st["optics"] = (QRandomGenerator::global()->bounded(100) < 97) ? "ok" : "fault";
    st["uptime_sec"] = serverStartTime.secsTo(QDateTime::currentDateTime());
    QJsonDocument doc(st); QByteArray data = doc.toJson(QJsonDocument::Compact);
    client->write(data); client->write("\n");
}


void LowerComputerServer::sendHeartbeatTo(QTcpSocket *client)
{
    if (!client || client->state() != QAbstractSocket::ConnectedState) return;
    
    QJsonObject heartbeat;
    heartbeat["type"] = "heartbeat";
    heartbeat["timestamp"] = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    heartbeat["server_uptime"] = serverStartTime.secsTo(QDateTime::currentDateTime());
    heartbeat["client_count"] = clients.size();
    
    QJsonDocument doc(heartbeat);
    QByteArray data = doc.toJson(QJsonDocument::Compact);
    client->write(data);
    client->write("\n");
    
    // 调试信息
    static int heartbeatCounter = 0;
    if (++heartbeatCounter % 10 == 0) {  // 每30秒输出一次
        writeToLog(QString("Sent heartbeat to client: %1:%2, Total clients: %3").arg(client->peerAddress().toString()).arg(client->peerPort()).arg(clients.size()));
        writeToLog(QString("Heartbeat data size: %1").arg(data.size()));
    }
}
void LowerComputerServer::startSensorDataStream(QTcpSocket *client)
{
    if (!client || client->state() != QAbstractSocket::ConnectedState) return;
    sensorStreamActive[client] = true;
    if (!sensorStreamTimer->isActive()) sensorStreamTimer->start();
}

void LowerComputerServer::stopSensorDataStream(QTcpSocket *client)
{
    if (!client) return;
    sensorStreamActive.remove(client);
    if (sensorStreamActive.isEmpty()) sensorStreamTimer->stop();
}

void LowerComputerServer::sendSensorDataStream()
{
    if (sensorStreamActive.isEmpty()) {
        sensorStreamTimer->stop();
        return;
    }
    for (auto it = sensorStreamActive.begin(); it != sensorStreamActive.end(); ) {
        QTcpSocket *client = it.key();
        if (!client || client->state() != QAbstractSocket::ConnectedState) {
            it = sensorStreamActive.erase(it);
            continue;
        }
        // 检查客户端是否仍然有效
        if (client->parent() == nullptr) {
            it = sensorStreamActive.erase(it);
            continue;
        }
        sendSensorDataToClient(client);
        ++it;
    }
    if (sensorStreamActive.isEmpty()) sensorStreamTimer->stop();
}
void LowerComputerServer::startSpectrumDataStream(QTcpSocket *client)
{
    if (!client || client->state() != QAbstractSocket::ConnectedState) {
        return;
    }
    if (wavelengthData.isEmpty() || spectrumRows.isEmpty()) {
        QJsonObject errorData;
        errorData["timestamp"] = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
        errorData["type"] = "error";
        errorData["message"] = "没有可用的光谱数据";
        QJsonDocument doc(errorData);
        QByteArray data = doc.toJson(QJsonDocument::Compact);
        client->write(data);
        client->write("\n");
        return;
    }
    spectrumStreamClients[client] = currentSpectrumRowIndex;
    spectrumStreamActive[client] = true;
    if (!spectrumStreamTimer->isActive()) {
        spectrumStreamTimer->start(50);
    }
    logText->append(QString("[%1] 开始流式发送光谱数据给客户端: %2:%3")
                   .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                   .arg(client->peerAddress().toString())
                   .arg(client->peerPort()));
}

void LowerComputerServer::stopSpectrumDataStream(QTcpSocket *client)
{
    if (!client) {
        return;
    }
    spectrumStreamActive.remove(client);
    spectrumStreamClients.remove(client);
    if (spectrumStreamActive.isEmpty()) {
        spectrumStreamTimer->stop();
    }
    logText->append(QString("[%1] 停止流式发送光谱数据给客户端: %2:%3")
                   .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                   .arg(client->peerAddress().toString())
                   .arg(client->peerPort()));
}

void LowerComputerServer::sendSpectrumDataStream()
{
    if (spectrumStreamActive.isEmpty() || wavelengthData.isEmpty() || spectrumRows.isEmpty()) {
        spectrumStreamTimer->stop();
        return;
    }
    const int rows = spectrumRows.size();
    const int cols = wavelengthData.size();
    for (auto it = spectrumStreamActive.begin(); it != spectrumStreamActive.end(); ) {
        QTcpSocket *client = it.key();
        if (!client || client->state() != QAbstractSocket::ConnectedState) {
            it = spectrumStreamActive.erase(it);
            spectrumStreamClients.remove(client);
            continue;
        }
        // 检查客户端是否仍然有效
        if (client->parent() == nullptr) {
            it = spectrumStreamActive.erase(it);
            spectrumStreamClients.remove(client);
            continue;
        }
        int currentRow = spectrumStreamClients[client];
        const QJsonArray &rowArray = spectrumRows[currentRow];
        QJsonObject dataToSend;
        dataToSend["timestamp"] = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
        dataToSend["type"] = "spectrum_data";
        dataToSend["file_name"] = currentSpectrumFile;
        dataToSend["row_index"] = currentRow;
        dataToSend["total_rows"] = rows;
        dataToSend["data_points"] = cols;
        dataToSend["wavelengths"] = wavelengthData;
        dataToSend["spectrum_values"] = rowArray;
        QJsonDocument doc(dataToSend);
        QByteArray data = doc.toJson(QJsonDocument::Compact);
        if (currentRow % 5 == 0) {
            writeToLog("--- 流式发送光谱数据行 ---");
            writeToLog(QString("当前行: %1/%2").arg(currentRow + 1).arg(rows));
            writeToLog(QString("数据点数: %1").arg(cols));
            writeToLog(QString("文件: %1").arg(currentSpectrumFile));
            writeToLog(QString("时间: %1").arg(dataToSend["timestamp"].toString()));
            writeToLog("------------------------");
        }
        client->write(data);
        client->write("\n");
        currentRow = (currentRow + 1) % rows;
        spectrumStreamClients[client] = currentRow;
        ++it;
    }
    if (spectrumStreamActive.isEmpty()) {
        spectrumStreamTimer->stop();
    }
}

/**
 * @brief 下位机服务器析构函数
 * @details 清理资源，关闭日志文件
 */
LowerComputerServer::~LowerComputerServer()
{
    // 清理日志文件
    if (logFile) {
        if (logFile->isOpen()) {
            writeToLog("下位机服务器正在关闭...");
            logFile->close();
        }
        delete logFile;
        logFile = nullptr;
    }
}

/**
 * @brief 初始化日志文件
 * @details 创建日志文件并设置文件路径
 */
void LowerComputerServer::initializeLogFile()
{
    // 创建logs目录
    QDir logsDir("logs");
    if (!logsDir.exists()) {
        logsDir.mkpath(".");
    }
    
    // 创建日志文件
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
    QString logFileName = QString("logs/lower_computer_%1.log").arg(timestamp);
    
    logFile = new QFile(logFileName);
    if (logFile->open(QIODevice::WriteOnly | QIODevice::Append)) {
        // 直接写入日志文件，不调用writeToLog避免递归
        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
        QString logEntry = QString("[%1] 下位机服务器日志文件已创建: %2\n").arg(timestamp).arg(logFileName);
        logFile->write(logEntry.toUtf8());
        logFile->flush();
    } else {
        // 无法创建日志文件时，使用标准错误输出
        std::cerr << "无法创建日志文件:" << logFileName.toStdString() << std::endl;
    }
}

/**
 * @brief 写入日志到文件
 * @param message 日志消息
 * @details 将日志消息写入日志文件，不在终端显示
 */
void LowerComputerServer::writeToLog(const QString &message)
{
    // 只输出到日志文件，不在终端显示
    // qDebug() << message;  // 注释掉终端输出
    
    if (!logFile || !logFile->isOpen()) return;
    
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    QString logEntry = QString("[%1] %2\n").arg(timestamp).arg(message);
    
    logFile->write(logEntry.toUtf8());
    logFile->flush();
    
    // 检查文件大小，如果超过限制则轮转
    if (logFile->size() > logFileMaxSize) {
        rotateLogFile();
    }
}

/**
 * @brief 轮转日志文件
 * @details 当日志文件超过最大大小时，创建新的日志文件
 */
void LowerComputerServer::rotateLogFile()
{
    if (!logFile) return;
    
    // 关闭当前日志文件
    logFile->close();
    
    // 创建新的日志文件
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
    QString logFileName = QString("logs/lower_computer_%1.log").arg(timestamp);
    
    logFile->setFileName(logFileName);
    if (logFile->open(QIODevice::WriteOnly | QIODevice::Append)) {
        writeToLog(QString("日志文件已轮转: %1").arg(logFileName));
    }
}

// === 加密相关函数实现 ===

void LowerComputerServer::initializeEncryption()
{
    // 创建加密工具实例
    cryptoUtils = new CryptoUtils(this);
    encryptionEnabled = false;
    encryptionPassword = "spectrum_system_2024"; // 默认密码
    
    // 设置默认密钥
    QByteArray key = CryptoUtils::generateKeyFromPassword(encryptionPassword);
    if (cryptoUtils->setKey(key)) {
        writeToLog("🔐 加密系统初始化成功");
    } else {
        writeToLog("❌ 加密系统初始化失败");
    }
}

bool LowerComputerServer::setEncryption(bool enabled, const QString &password)
{
    if (enabled) {
        QString pwd = password.isEmpty() ? encryptionPassword : password;
        QByteArray key = CryptoUtils::generateKeyFromPassword(pwd);
        
        if (cryptoUtils->setKey(key)) {
            encryptionEnabled = true;
            encryptionPassword = pwd;
            writeToLog(QString("🔐 加密已启用，密码: %1").arg(pwd));
            return true;
        } else {
            writeToLog("❌ 启用加密失败，密钥设置错误");
            return false;
        }
    } else {
        encryptionEnabled = false;
        writeToLog("🔓 加密已禁用");
        return true;
    }
}

bool LowerComputerServer::isEncryptionEnabled() const
{
    return encryptionEnabled;
}

QString LowerComputerServer::getEncryptionStatus() const
{
    if (!encryptionEnabled) {
        return "加密未启用";
    }
    return QString("加密已启用 - %1").arg(cryptoUtils->getStatus());
}

QByteArray LowerComputerServer::encryptData(const QByteArray &data)
{
    if (!encryptionEnabled || !cryptoUtils) {
        return data; // 未启用加密，直接返回原数据
    }
    
    QByteArray encrypted = cryptoUtils->encrypt(data);
    if (encrypted.isEmpty()) {
        writeToLog("❌ 数据加密失败");
        return QByteArray();
    }
    
    return encrypted;
}

QByteArray LowerComputerServer::decryptData(const QByteArray &data)
{
    if (!encryptionEnabled || !cryptoUtils) {
        return data; // 未启用加密，直接返回原数据
    }
    
    QByteArray decrypted = cryptoUtils->decrypt(data);
    if (decrypted.isEmpty()) {
        writeToLog("❌ 数据解密失败");
        return QByteArray();
    }
    
    return decrypted;
}


