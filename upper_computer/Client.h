/**
 * @file Client.h
 * @brief 上位机TCP客户端头文件
 * @details 定义上位机客户端类，包含TCP通信、光谱数据显示、系统监控等功能
 * @author 系统设计项目组
 * @date 2024
 */

#pragma once

// Qt核心模块
#include <QMainWindow>      // 主窗口基类
#include <QTcpSocket>       // TCP套接字
#include <QNetworkProxy>    // 网络代理（用于设置 NoProxy）
#include <QTimer>           // 定时器
#include <QElapsedTimer>    // 高精度计时器
#include <QThread>          // 线程类

// JSON数据处理
#include <QJsonObject>      // JSON对象
#include <QJsonArray>       // JSON数组

// UI控件
#include <QTableWidget>     // 表格控件
#include <QTextEdit>        // 文本编辑框
#include <QTextBrowser>     // 文本浏览器
#include <QGroupBox>        // 分组框
#include <QLabel>           // 标签
#include <QLineEdit>        // 单行文本输入框
#include <QSpinBox>         // 数字输入框
#include <QProgressBar>     // 进度条
#include <QPushButton>      // 按钮
#include <QStatusBar>       // 状态栏
#include <QCheckBox>        // 复选框
#include <QComboBox>        // 下拉框
#include <QHBoxLayout>      // 水平布局
#include <QPair>            // 键值对
#include <QFile>            // 文件读写
#include <QCoreApplication> // 应用路径
#include "Database.h"

// 系统相关
#include <QProcess>         // 进程管理
#include <QSet>             // 集合容器
#include <QVariant>         // 变体类型

// Qt图表模块
#include <QtCharts/QChartView>      // 图表视图
#include <QtCharts/QLineSeries>     // 折线图系列
#include <QtCharts/QScatterSeries>  // 散点图系列
#include <QtCharts/QValueAxis>      // 数值轴
#include "ZoomableChartView.h"      // 可缩放视图

// 光谱预测模块
#include "SpectrumPredictor.h"      // 光谱预测器

// 加密模块
#include "CryptoUtils.h"            // 加密工具类

// 前向声明
class PredictionWorker;

/**
 * @class UpperComputerClient
 * @brief 上位机TCP客户端主类
 * @details 继承自QMainWindow，实现与下位机的TCP通信、光谱数据显示、
 *          系统状态监控、数据保存等功能
 */
class UpperComputerClient : public QMainWindow
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父窗口指针
     */
    explicit UpperComputerClient(QWidget *parent = nullptr);
    
    /**
     * @brief 析构函数
     * @details 清理资源，停止定时器，断开网络连接
     */
    ~UpperComputerClient();
    
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

signals:
    // 光谱图表已更新（用于弹窗与原图同频同步）
    void spectrumChartUpdated();

private slots:
    // === 网络通信相关槽函数 ===
    /**
     * @brief 连接按钮点击事件
     * @details 根据当前状态执行连接或断开操作
     */
    void onConnectClicked();
    
    /**
     * @brief 发送命令按钮点击事件
     * @details 发送命令到下位机并记录日志
     */
    void onSendCommandClicked();
    
    /**
     * @brief TCP连接成功事件
     * @details 更新界面状态，显示连接成功信息
     */
    void onConnected();
    
    /**
     * @brief TCP连接断开事件
     * @details 更新界面状态，显示断开信息
     */
    void onDisconnected();
    
    /**
     * @brief 数据接收事件
     * @details 处理从下位机接收的数据
     */
    void onDataReceived();
    
    /**
     * @brief 套接字错误事件
     * @param error 错误类型
     * @details 处理网络连接错误
     */
    void onSocketError(QAbstractSocket::SocketError error);
    
    // === 数据显示更新相关槽函数 ===
    /**
     * @brief 更新数据显示
     * @details 定时更新传感器数据和界面显示
     */
    void updateDataDisplay();
    
    /**
     * @brief 更新光谱数据显示
     * @details 更新光谱数据表格和图表
     */
    void updateSpectrumDisplay();
    
    /**
     * @brief 更新光谱图表
     * @details 绘制光谱数据图表
     */
    void updateSpectrumChart();
    
    /**
     * @brief 更新质量指标
     * @param wavelengths 波长数据
     * @param spectrumValues 光谱值数据
     * @details 计算并显示光谱数据质量指标
     */
    void updateQualityMetrics(const QJsonArray &wavelengths, const QJsonArray &spectrumValues);
    
    /**
     * @brief 应用校准数据
     * @param spectrumValues 光谱值数据（引用）
     * @details 如果校准数据可用，则应用到光谱数据
     */
    void applyCalibrationIfReady(QJsonArray &spectrumValues);
    
    /**
     * @brief 应用预处理
     * @param spectrumValues 光谱值数据（引用）
     * @param wavelengths 波长数据
     * @details 对光谱数据进行预处理（平滑、基线校正等）
     */
    void applyPreprocessing(QJsonArray &spectrumValues, const QJsonArray &wavelengths);
    
    // === 系统监控相关槽函数 ===
    /**
     * @brief 更新主机状态
     * @details 监控CPU、内存、磁盘使用率等系统状态
     */
    void updateHostStatus();
    
    /**
     * @brief 更新心跳状态
     * @details 监控与下位机的心跳连接状态
     */
    void updateHeartbeatStatus();
    
    // === 日志和文件保存功能 ===
    /**
     * @brief 初始化日志系统
     * @details 设置日志文件路径和格式
     */
    void initLogging();
    
    /**
     * @brief 写入日志
     * @param message 日志消息
     * @details 将消息写入日志文件
     */
    void writeToLog(const QString &message);
    
    /**
     * @brief 轮转日志文件
     * @details 当日志文件过大时创建新文件
     */
    void rotateLogFile();
    
    /**
     * @brief 保存光谱数据
     * @param spectrumData 光谱数据
     * @param wavelengths 波长数据
     * @param filename 文件名（可选）
     * @details 将光谱数据保存到文件
     */
    void saveSpectrumData(const QJsonArray &spectrumData, const QJsonArray &wavelengths, const QString &filename = QString());
    
    /**
     * @brief 保存校准数据
     * @param data 校准数据
     * @param type 数据类型（暗电流或白参考）
     * @details 保存暗电流和白参考校准数据
     */
    void saveCalibrationData(const QJsonArray &data, const QString &type);
    
    // === 光谱预测相关槽函数 ===
    /**
     * @brief 开始光谱预测
     * @details 使用当前光谱数据进行属性预测
     */
    void onStartPredictionClicked();
    
    /**
     * @brief 停止光谱预测
     * @details 停止光谱预测过程
     */
    void onStopPredictionClicked();
    
    /**
     * @brief 更新预测结果显示
     * @details 更新预测结果表格和图表
     */
    void updatePredictionDisplay();
    
    /**
     * @brief 自动进行光谱预测
     * @details 当接收到新光谱数据时自动进行预测
     */
    void performAutoPrediction();
    
    /**
     * @brief 更新实时预测结果显示
     * @details 更新光谱图表右侧的实时预测结果
     */
    void updateRealtimePredictionDisplay(const QMap<QString, float>& results);
    
    /**
     * @brief 预测完成回调
     * @details 当后台线程完成预测时调用
     */
    void onPredictionCompleted(const QMap<QString, float>& results);
    
    /**
     * @brief 预测错误回调
     * @details 当后台线程预测出错时调用
     */
    void onPredictionError(const QString& error);
    
    /**
     * @brief 安全更新预测显示
     * @details 在主线程中安全地更新预测显示
     */
    void safeUpdatePredictionDisplay();
    
    /**
     * @brief 更新预测历史数据图表
     * @details 更新折线图显示预测历史数据
     */
    void updatePredictionHistoryChart();
    
    // === 历史图交互：单属性趋势图 ===
    void onHistorySeriesClicked(const QPointF &point);
    void showPropertyHistoryChart(const QString &propertyName);
    void refreshPropertyHistoryChart(const QString &propertyName);

    // 右键菜单导出 CSV（可选扩展）
    void exportPropertyHistoryToCsv(const QString &propertyName, const QString &filePath = QString());
    
    /**
     * @brief 添加预测数据到历史记录
     * @param results 预测结果数据
     * @details 将新的预测结果添加到历史数据中
     */
    void addPredictionToHistory(const QMap<QString, float>& results);
    
    /**
     * @brief 初始化预测工作线程
     * @details 创建预测工作线程和工作对象
     */
    void initPredictionThread();

public:
    // === 阈值告警相关 ===
    /**
     * @brief 初始化阈值告警UI
     * @param propertyLabels 物质/属性名称列表
     */
    void initializeThresholdAlarms(const QStringList &propertyLabels);

    /**
     * @brief 刷新阈值告警显示
     * @param results 最新预测结果（属性->数值）
     */
    void updateThresholdAlarms(const QMap<QString, float> &results);

    /**
     * @brief 从JSON配置加载阈值范围（覆盖默认值）
     * @param configPath 配置文件路径
     */
    void loadThresholdsFromConfig(const QString &configPath);

    // 质量阈值结构
    struct QualityLimits {
        double snrMin = 0.0;           // 信噪比最小值
        double baselineMax = 1e9;      // 基线指标最大允许
        double integrityMin = 0.0;     // 完整性最小值(0~1)
    } qualityLimits;
    bool lastQualityOk = true;         // 最近一次质量是否通过

    // 光谱质量异常计数与阈值
    int spectrumQualityAnomalyCount = 0;       // 累计光谱质量不达标次数
    int spectrumQualityAnomalyLimit = 100;     // 超过该值触发停止流（从config读取）
    bool qualityLimitWarned = false;           // 达到上限后是否已告警弹窗

    // === 数据库相关 ===
    void initializeDatabase();
    void createTablesIfNotExists();
    void insertSpectrumRecord(const QJsonArray &wavelengths, const QJsonArray &rawSpectrum);
    void insertPredictionRecord(const QMap<QString, float>& results);

private:
    void sendStopStream();
    void applyPurpleStyleToPropertyButtons();
    // 刷新已打开统计弹窗中的“光谱质量异常累计次数”显示
    void refreshQualityAnomalyCountInOpenDialogs();
    // 显示某物质的统计弹窗
    void showPropertyStatsDialog(const QString &normalizedKey, const QString &displayName);

    // === 连接与重试辅助函数 ===
    void startConnectWithRetry(const QString &host, int port, bool userInitiated);
    void scheduleNextRetry();
    void cancelConnectRetry();

    // === 界面初始化相关私有函数 ===
    /**
     * @brief 设置用户界面
     * @details 初始化主窗口界面布局和控件
     */
    void setupUI();
    
    /**
     * @brief 创建控制面板
     * @return 控制面板控件指针
     * @details 创建连接控制、命令发送等控制面板
     */
    QWidget* createControlPanel();
    
    /**
     * @brief 创建光谱数据面板
     * @return 光谱数据面板控件指针
     * @details 创建光谱数据显示、图表、质量控制面板
     */
    QWidget* createSpectrumDataPanel();
    
    /**
     * @brief 创建底部标签页
     * @return 标签页控件指针
     * @details 创建日志、命令历史等标签页
     */
    QWidget* createBottomTabs();
    
    /**
     * @brief 设置信号槽连接
     * @details 连接各种信号和槽函数
     */
    void setupConnections();
    
    // === 数据处理相关私有函数 ===
    /**
     * @brief 更新传感器数据
     * @param data 传感器数据JSON对象
     * @details 解析并显示传感器数据
     */
    void updateSensorData(const QJsonObject &data);
    
    /**
     * @brief 更新光谱数据点
     * @param data 光谱数据点JSON对象
     * @details 处理流式光谱数据点
     */
    void updateSpectrumDataPoint(const QJsonObject &data);
    
    // === 光谱预测相关私有函数 ===
    /**
     * @brief 初始化光谱预测器
     * @details 加载LibTorch模型和相关信息
     */
    void initSpectrumPredictor();
    
    /**
     * @brief 执行光谱预测
     * @param spectrumData 光谱数据
     * @return 预测结果映射
     * @details 使用LibTorch模型进行光谱属性预测
     */
    QMap<QString, float> performPrediction(const QJsonArray &spectrumData);
    
    /**
     * @brief 创建预测控制面板
     * @return 预测控制面板控件指针
     * @details 创建光谱预测相关的控制界面
     */
    QWidget* createPredictionPanel();
    
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

private:
    // === 网络通信相关成员变量 ===
    QTcpSocket *tcpSocket;      // TCP套接字
    QTimer *updateTimer;        // 数据更新定时器
    
    // === 加密相关成员变量 ===
    CryptoUtils *cryptoUtils;   // 加密工具实例
    bool encryptionEnabled;     // 是否启用加密
    QString encryptionPassword; // 加密密码

    // === 基础UI组件 ===
    QLineEdit *hostEdit;        // 服务器地址输入框
    QSpinBox *portSpinBox;      // 端口号输入框
    QPushButton *connectButton; // 连接/断开按钮
    QLineEdit *commandEdit;     // 命令输入框
    QTextEdit *logText;         // 日志显示文本框
    QTextEdit *commandHistory;  // 命令历史文本框
    QTableWidget *dataTable;    // 传感器数据表格
    QLabel *statusLabel;        // 连接状态标签
    QProgressBar *connectionProgress; // 连接进度条
    // 阈值告警UI
    QHBoxLayout *thresholdAlarmLayout;               // 阈值告警容器布局（在“已连接”右侧）
    QMap<QString, QLabel*> thresholdAlarmLabels;     // 规范化属性名 -> 标签
    
    // === 数据采集设置控件 ===
    QSpinBox *integrationSpin;  // 积分时间设置
    QSpinBox *averageSpin;      // 平均次数设置
    QPushButton *sendAcqBtn;    // 发送采集命令按钮

    // === 光谱数据显示组件 ===
    QTableWidget *spectrumTable;    // 光谱数据表格
    QTextBrowser *spectrumInfo;     // 光谱信息显示
    QtCharts::QChartView *spectrumChart; // 光谱图表视图
    QGroupBox *spectrumGroup;       // 光谱数据分组框
    
    // === 实时预测结果显示组件 ===
    QLabel *realtimePredictionStatusLabel;  // 实时预测状态标签
    QtCharts::QChartView *realtimePredictionChart;  // 实时预测结果柱状图
    QLabel *realtimePredictionTimeLabel;    // 实时预测时间标签
    
    // === 预测历史数据图表组件 ===
    QtCharts::QChartView *predictionHistoryChart;  // 预测历史数据折线图
    QMap<QString, QVector<QPointF>> predictionHistoryData;  // 预测历史数据存储
    QMap<QString, QtCharts::QLineSeries*> predictionHistorySeries;  // 预测历史数据折线系列
    QTimer *historyUpdateTimer;  // 历史数据更新定时器
    int maxHistoryPoints;  // 最大历史数据点数

    // === 单属性趋势弹窗相关（实时刷新） ===
    QMap<QString, QDialog*> propertyHistoryDialogs;               // 属性 -> 弹窗
    QMap<QString, QtCharts::QChartView*> propertyHistoryViews;    // 属性 -> ChartView
    QMap<QString, QtCharts::QLineSeries*> propertyHistoryLines;   // 属性 -> 线系列
    QMap<QString, QtCharts::QLineSeries*> propertyHistoryMinLines; // 属性 -> 阈值下限线
    QMap<QString, QtCharts::QLineSeries*> propertyHistoryMaxLines; // 属性 -> 阈值上限线
    QMap<QString, QGraphicsSimpleTextItem*> propertyHistoryMinTexts; // 属性 -> 下限数值文本
    QMap<QString, QGraphicsSimpleTextItem*> propertyHistoryMaxTexts; // 属性 -> 上限数值文本
    
    // === 质量监控组件 ===
    QGroupBox *qualityGroup;        // 质量监控分组框
    QLabel *snrLabel;               // 信噪比标签
    QLabel *baselineLabel;          // 基线稳定性标签
    QLabel *integrityLabel;         // 数据完整性标签
    QLabel *qualityScoreLabel;      // 质量评分标签
    
    // === 校准控制组件 ===
    QPushButton *captureDarkBtn;    // 采集暗电流按钮
    QPushButton *captureWhiteBtn;   // 采集白参考按钮
    QLabel *calibStatusLabel;       // 校准状态标签
    
    // === 预处理控制组件 ===
    QComboBox *preprocCombo;        // 预处理方法选择下拉框
    QSet<QString> selectedPreprocs; // 已选择的预处理方法（兼容保留）
    QList<QPair<QString, QVariantMap>> preprocPipeline; // 预处理流水线（方法+参数）
    QSpinBox *smoothWindow;         // 平滑窗口大小
    QSpinBox *baselineEdge;         // 基线校正边缘百分比
    QSpinBox *derivativeOrder;      // 导数阶数（1或2）
    QLabel *lblSmoothWindow;        // 平滑窗口标签
    QLabel *lblBaselineEdge;        // 基线边缘标签
    QLabel *lblDerivativeOrder;     // 导数阶数标签
    QTextEdit *preprocSummary;      // 预处理摘要显示

    // === 状态监控组件 ===
    QGroupBox *statusGroup;         // 状态监控分组框
    QTableWidget *hostStatusTable;  // 主机状态表格
    QTableWidget *deviceStatusTable; // 设备状态表格
    QTimer *hostStatusTimer;        // 主机状态更新定时器
    QElapsedTimer spectrumRateTimer; // 光谱数据接收速率计时器
    int spectrumCountInWindow = 0;   // 当前窗口内的光谱数据点计数

    // === 心跳监控相关 ===
    QDateTime lastHeartbeatTime;     // 最后心跳时间
    bool heartbeatReceived = false;  // 是否收到心跳
    int heartbeatTimeoutCount = 0;   // 心跳超时计数
    QTimer *heartbeatCheckTimer;    // 心跳检查定时器
    QDateTime heartbeatGraceUntil;   // 连接后宽限期截止时间

    // === 数据存储相关 ===
    QJsonObject lastSensorData;     // 最新传感器数据
    QJsonArray lastWavelengthData;  // 最新波长数据
    QJsonArray lastSpectrumData;    // 最新光谱数据
    
    // === 光谱数据流相关 ===
    QVector<QPointF> spectrumDataPoints;  // 累积的光谱数据点
    QString currentSpectrumFileName;      // 当前光谱文件名
    int currentSpectrumTotalPoints;       // 当前光谱总点数
    
    // === 校准数据相关 ===
    QJsonArray darkCurrent;         // 暗电流校准数据
    QJsonArray whiteReference;      // 白参考校准数据
    bool hasDark = false;           // 是否有暗电流数据
    bool hasWhite = false;          // 是否有白参考数据
    
    // === 系统状态相关 ===
    QByteArray dataBuffer;          // 数据接收缓冲区
    bool isDestroying = false;      // 析构标志，防止析构时继续操作
    
    // === 日志和文件保存相关 ===
    QString logFilePath;            // 日志文件路径
    QString dataDirPath;            // 数据保存目录路径
    QFile* logFile;                 // 日志文件对象指针
    int logFileMaxSize = 10 * 1024 * 1024;  // 日志文件最大大小(10MB)
    int logFileCount = 0;           // 日志文件计数器
    
    // === 光谱预测相关成员变量 ===
    SpectrumPredictor* spectrumPredictor;  // 光谱预测器指针
    QThread* predictionThread;             // 预测工作线程
    PredictionWorker* predictionWorker;    // 预测工作对象
    QPushButton* startPredictionBtn;       // 开始预测按钮
    QPushButton* stopPredictionBtn;        // 停止预测按钮
    QTableWidget* predictionTable;         // 预测结果表格
    QLabel* predictionStatusLabel;         // 预测状态标签
    QTimer* predictionTimer;               // 预测定时器
    QMap<QString, float> lastPredictionResults; // 最新预测结果
    bool predictionActive;                 // 预测是否激活
    bool autoPredictionEnabled;            // 自动预测是否启用
    bool predictionCompletedConnected;     // 预测完成信号是否已连接

    // 阈值范围配置（规范化属性名 -> [min,max]），大小写不敏感
    QMap<QString, QPair<float, float>> thresholdRanges;

    // 顶部物质按钮和统计
    QMap<QString, QPushButton*> propertyButtons;      // 规范化key -> 顶部按钮
    QMap<QString, QDialog*> propertyStatsDialogs;     // 规范化key -> 统计弹窗
    QMap<QString, int> anomalyCounts;                 // 规范化key -> 异常次数
    QMap<QString, int> detectCounts;                  // 规范化key -> 检测总次数
    QMap<QString, bool> currentAbnormal;              // 规范化key -> 当前是否异常
    bool isConnectedState = false;                    // 是否已连接
    bool isStreaming = false;                         // 是否检测中（开始流）
    bool isPaused = false;                            // 是否处于暂停（停止流）

    void refreshPropertyButtonsByState();             // 根据状态统一刷新按钮颜色
    void refreshPropertyButton(const QString &key);   // 刷新某一物质按钮颜色

    // 数据库连接
    DatabaseManager dbm;

protected:
    // 窗口尺寸变更事件（用于防抖处置）
    void resizeEvent(QResizeEvent* event) override;
    // 事件过滤（捕获图表双击）
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QTimer *resizeDebounceTimer = nullptr; // 窗口调整防抖定时器
    // 打开图表镜像窗口
    void openChartInWindow(QtCharts::QChartView *sourceView, const QString &titleSuffix);
    
    // === 连接重试状态 ===
    QTimer *connectRetryTimer = nullptr;   // 重试定时器
    int connectRetryCount = 0;             // 已重试次数
    int connectRetryMax = 5;               // 最大重试次数（自动连接/心跳使用）
    int connectRetryBaseDelayMs = 800;     // 初始退避时间
    bool connectUserInitiated = false;     // 是否用户触发（用于弹窗策略）
    bool connectInfiniteRetry = false;     // 用户点击连接时采用无限重试
};
