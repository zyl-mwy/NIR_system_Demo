#include <QtCharts/QChart>
#include <QEvent>
#include <QObject>
/**
 * @file Client.cpp
 * @brief 上位机TCP客户端实现文件
 * @details 实现与下位机的TCP通信，包括传感器数据接收、光谱数据显示、命令发送等功能
 * @author 系统设计项目组
 * @date 2024
 */

#include "Client.h"
#include <limits>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include "PredictionWorker.h"

// Qt核心模块
#include <QVBoxLayout>      // 垂直布局
#include <QHBoxLayout>      // 水平布局
#include <QGridLayout>      // 网格布局
#include <QGroupBox>        // 分组框
#include <QHeaderView>      // 表头视图
#include <QMessageBox>      // 消息框
#include <QDateTime>        // 日期时间
#include <QSplitter>        // 分割器
#include <QApplication>     // 应用程序类
#include <QTabWidget>       // 标签页控件
#include <QPushButton>      // 按钮
#include <QDebug>           // 调试输出

// JSON数据处理
#include <QJsonDocument>    // JSON文档
#include <QJsonParseError>  // JSON解析错误

// 图形绘制
#include <QPainter>         // 绘图器
#include <QPixmap>          // 像素图

// QtCharts图表库
#include <QtCharts/QChartView>
#include <QtCharts/QChart>
#include <QtCharts/QLegendMarker>
#include <QFileDialog>
#include <QtCharts/QLineSeries>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QValueAxis>
#include <QtCharts/QBarSet>
#include <QtCharts/QHorizontalBarSeries>
#include <QtCharts/QBarCategoryAxis>

// 系统相关
#include <QCoreApplication> // 核心应用程序
#include <QDir>             // 目录操作
#include <QTextStream>      // 文本流

void UpperComputerClient::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    if (resizeDebounceTimer) resizeDebounceTimer->start();
}

bool UpperComputerClient::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonDblClick) {
        if (auto *view = qobject_cast<QtCharts::QChartView*>(watched)) {
            if (view == spectrumChart) {
                openChartInWindow(view, " - 光谱曲线图");
                return true;
            }
            if (view == predictionHistoryChart) {
                openChartInWindow(view, " - 历史趋势图(总)");
                return true;
            }
            if (view == realtimePredictionChart) {
                openChartInWindow(view, " - 预测结果柱状图");
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void UpperComputerClient::openChartInWindow(QtCharts::QChartView *sourceView, const QString &titleSuffix)
{
    if (!sourceView || !sourceView->chart()) return;
    QDialog *dlg = new QDialog(nullptr);
    dlg->setWindowTitle(windowTitle() + titleSuffix);
    dlg->resize(1000, 600);
    dlg->setWindowFlags(Qt::Window | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint | Qt::CustomizeWindowHint);
    QVBoxLayout *layout = new QVBoxLayout(dlg);
    QtCharts::QChartView *view = new QtCharts::QChartView(dlg);
    view->setRenderHint(QPainter::Antialiasing);

    // 克隆图表（完整复制：新建chart并复制series数据与轴设置）
    QtCharts::QChart *src = sourceView->chart();
    QtCharts::QChart *chart = new QtCharts::QChart();
    chart->setTitle(src->title());
    // 强制浅色主题，确保柱状图标签文本为黑色
    chart->setTheme(QtCharts::QChart::ChartThemeLight);
    chart->setAnimationOptions(QtCharts::QChart::NoAnimation);
    chart->legend()->setVisible(src->legend()->isVisible());
    chart->setBackgroundBrush(src->backgroundBrush());

    // 复制所有series（仅支持常用类型）
    for (QtCharts::QAbstractSeries *s : src->series()) {
        if (auto *ls = qobject_cast<QtCharts::QLineSeries*>(s)) {
            auto *ls2 = new QtCharts::QLineSeries();
            ls2->setName(ls->name());
            ls2->setPen(ls->pen());
            ls2->setColor(ls->color());
            ls2->setBrush(ls->brush());
            ls2->replace(ls->points());
            chart->addSeries(ls2);
        } else if (auto *hs = qobject_cast<QtCharts::QHorizontalBarSeries*>(s)) {
            auto *hs2 = new QtCharts::QHorizontalBarSeries();
            // 显示柱状图数值标签
            hs2->setLabelsVisible(true);
            hs2->setLabelsPosition(QtCharts::QAbstractBarSeries::LabelsOutsideEnd);
            hs2->setLabelsFormat("@value");
            // 使用 series 的图形默认笔刷不可直接设置标签颜色，改为给每个 BarSet 设置刷子对比背景，确保标签可见
            for (auto *set : hs->barSets()) {
                auto *set2 = new QtCharts::QBarSet(set->label());
                for (int i = 0; i < set->count(); ++i) set2->append((*set)[i]);
                set2->setColor(set->color());
                QColor fill = set->color();
                if (fill.lightness() < 160) fill = fill.lighter(140);
                set2->setBrush(QBrush(fill));
                hs2->append(set2);
            }
            chart->addSeries(hs2);
        }
    }

    // 复制坐标轴（完整设置）
    QList<QtCharts::QAbstractAxis*> axes = src->axes();
    QtCharts::QValueAxis *valueAxisX = nullptr, *valueAxisY = nullptr;
    QtCharts::QDateTimeAxis *timeAxis = nullptr;
    
    for (QtCharts::QAbstractAxis *ax : axes) {
        if (auto *va = qobject_cast<QtCharts::QValueAxis*>(ax)) {
            if (ax->alignment() == Qt::AlignBottom) {
                valueAxisX = va;
            } else if (ax->alignment() == Qt::AlignLeft) {
                valueAxisY = va;
            }
        }
        if (auto *ta = qobject_cast<QtCharts::QDateTimeAxis*>(ax)) timeAxis = ta;
    }
    
    // 复制X轴（时间轴或数值轴）
    if (timeAxis) {
        auto *ax = new QtCharts::QDateTimeAxis();
        ax->setFormat(timeAxis->format());
        ax->setTitleText(timeAxis->titleText());
        ax->setLabelsVisible(timeAxis->labelsVisible());
        ax->setTitleVisible(timeAxis->isTitleVisible());
        ax->setLabelsFont(timeAxis->labelsFont());
        ax->setTitleFont(timeAxis->titleFont());
        ax->setMin(timeAxis->min());
        ax->setMax(timeAxis->max());
        chart->addAxis(ax, Qt::AlignBottom);
        for (QtCharts::QAbstractSeries *s : chart->series()) s->attachAxis(ax);
    } else if (valueAxisX) {
        auto *ax = new QtCharts::QValueAxis();
        ax->setTitleText(valueAxisX->titleText());
        ax->setLabelFormat(valueAxisX->labelFormat());
        ax->setLabelsVisible(valueAxisX->labelsVisible());
        ax->setTitleVisible(valueAxisX->isTitleVisible());
        ax->setLabelsFont(valueAxisX->labelsFont());
        ax->setTitleFont(valueAxisX->titleFont());
        ax->setMin(valueAxisX->min());
        ax->setMax(valueAxisX->max());
        ax->setTickCount(valueAxisX->tickCount());
        chart->addAxis(ax, Qt::AlignBottom);
        for (QtCharts::QAbstractSeries *s : chart->series()) s->attachAxis(ax);
    }
    
    // 复制Y轴
    if (valueAxisY) {
        auto *ay = new QtCharts::QValueAxis();
        ay->setTitleText(valueAxisY->titleText());
        ay->setLabelFormat(valueAxisY->labelFormat());
        ay->setLabelsVisible(valueAxisY->labelsVisible());
        ay->setTitleVisible(valueAxisY->isTitleVisible());
        ay->setLabelsFont(valueAxisY->labelsFont());
        ay->setTitleFont(valueAxisY->titleFont());
        ay->setMin(valueAxisY->min());
        ay->setMax(valueAxisY->max());
        ay->setTickCount(valueAxisY->tickCount());
        chart->addAxis(ay, Qt::AlignLeft);
        for (QtCharts::QAbstractSeries *s : chart->series()) s->attachAxis(ay);
    }

    view->setChart(chart);
    layout->addWidget(view);
    dlg->setModal(false);
    dlg->show();
    
    // 为弹窗图表建立信号驱动的实时同步，与原图刷新频率一致
    if (sourceView == spectrumChart || sourceView == realtimePredictionChart || sourceView == predictionHistoryChart) {
        auto doSync = [=]() {
            if (dlg->isVisible() && sourceView && sourceView->chart()) {
                // 重新克隆图表数据
                QtCharts::QChart *srcChart = sourceView->chart();
                QtCharts::QChart *popupChart = view->chart();
                
                // 获取现有系列映射（避免图例闪烁）
                QMap<QString, QtCharts::QAbstractSeries*> existingSeries;
                // 额外按类型收集无名系列列表，用于按序号同步
                QVector<QtCharts::QLineSeries*> popupLineSeriesList;
                QVector<QtCharts::QHorizontalBarSeries*> popupBarSeriesList;
                QVector<QtCharts::QScatterSeries*> popupScatterSeriesList;
                for (auto *s : popupChart->series()) {
                    if (!s->name().isEmpty()) {
                        existingSeries[s->name()] = s;
                    }
                    if (auto *ls = qobject_cast<QtCharts::QLineSeries*>(s)) popupLineSeriesList.append(ls);
                    else if (auto *bs = qobject_cast<QtCharts::QHorizontalBarSeries*>(s)) popupBarSeriesList.append(bs);
                    else if (auto *ss = qobject_cast<QtCharts::QScatterSeries*>(s)) popupScatterSeriesList.append(ss);
                }
                
                // 更新或创建系列
                int lineTypeIndex = 0;
                int barTypeIndex = 0;
                int scatterTypeIndex = 0;
                for (QtCharts::QAbstractSeries *s : srcChart->series()) {
                    QString seriesName = s->name();
                    
                    if (auto *ls = qobject_cast<QtCharts::QLineSeries*>(s)) {
                        QtCharts::QLineSeries *popupLineSeries = nullptr;
                        if (!seriesName.isEmpty() && existingSeries.contains(seriesName)) {
                            popupLineSeries = qobject_cast<QtCharts::QLineSeries*>(existingSeries[seriesName]);
                        } else {
                            // 无名系列：按类型序号匹配
                            if (lineTypeIndex < popupLineSeriesList.size()) {
                                popupLineSeries = popupLineSeriesList[lineTypeIndex];
                            }
                        }
                        if (!popupLineSeries) {
                            // 创建新系列
                            popupLineSeries = new QtCharts::QLineSeries();
                            if (!seriesName.isEmpty()) popupLineSeries->setName(seriesName);
                            popupLineSeries->setPen(ls->pen());
                            popupLineSeries->setColor(ls->color());
                            popupLineSeries->setBrush(ls->brush());
                            popupChart->addSeries(popupLineSeries);
                            // 附加到坐标轴
                            for (auto *axis : popupChart->axes()) {
                                popupLineSeries->attachAxis(axis);
                            }
                            // 维护列表
                            if (lineTypeIndex >= popupLineSeriesList.size()) popupLineSeriesList.append(popupLineSeries);
                            else popupLineSeriesList[lineTypeIndex] = popupLineSeries;
                        }
                        // 更新现有系列数据
                        popupLineSeries->replace(ls->points());
                        lineTypeIndex++;
                    } else if (auto *hs = qobject_cast<QtCharts::QHorizontalBarSeries*>(s)) {
                        QtCharts::QHorizontalBarSeries *popupBarSeries = nullptr;
                        if (!seriesName.isEmpty() && existingSeries.contains(seriesName)) {
                            popupBarSeries = qobject_cast<QtCharts::QHorizontalBarSeries*>(existingSeries[seriesName]);
                        } else {
                            if (barTypeIndex < popupBarSeriesList.size()) {
                                popupBarSeries = popupBarSeriesList[barTypeIndex];
                            }
                        }
                        if (!popupBarSeries) {
                            // 创建新系列
                            popupBarSeries = new QtCharts::QHorizontalBarSeries();
                            // 显示柱状图数值标签
                            popupBarSeries->setLabelsVisible(true);
                            popupBarSeries->setLabelsPosition(QtCharts::QAbstractBarSeries::LabelsOutsideEnd);
                            popupBarSeries->setLabelsFormat("@value");
                            if (!seriesName.isEmpty()) popupBarSeries->setName(seriesName);
                            popupChart->addSeries(popupBarSeries);
                            // 附加到坐标轴
                            for (auto *axis : popupChart->axes()) {
                                popupBarSeries->attachAxis(axis);
                            }
                            if (barTypeIndex >= popupBarSeriesList.size()) popupBarSeriesList.append(popupBarSeries);
                            else popupBarSeriesList[barTypeIndex] = popupBarSeries;
                        }
                        // 确保重复使用的系列也打开标签
                        popupBarSeries->setLabelsVisible(true);
                        popupBarSeries->setLabelsPosition(QtCharts::QAbstractBarSeries::LabelsOutsideEnd);
                        popupBarSeries->setLabelsFormat("@value");
                        // 同步主题，保证标签文本颜色
                        if (popupChart->theme() != QtCharts::QChart::ChartThemeLight) {
                            popupChart->setTheme(QtCharts::QChart::ChartThemeLight);
                        }
                        // 更新柱状图数据
                        popupBarSeries->clear();
                        for (auto *set : hs->barSets()) {
                            auto *set2 = new QtCharts::QBarSet(set->label());
                            for (int i = 0; i < set->count(); ++i) set2->append((*set)[i]);
                            set2->setColor(set->color());
                            // 为了保证标签对比度，优先使用浅色填充，文本默认黑色即可清晰显示
                            QColor fill = set->color();
                            if (fill.lightness() < 160) {
                                fill = fill.lighter(140);
                            }
                            set2->setBrush(QBrush(fill));
                            popupBarSeries->append(set2);
                        }

                        // 自绘黑色数值标签置于柱体中间：先清理旧标签
                        for (auto *item : popupChart->scene()->items()) {
                            if (auto *txt = qgraphicsitem_cast<QGraphicsSimpleTextItem*>(item)) {
                                if (txt->data(0).toString() == "bar-value-label") {
                                    delete txt;
                                }
                            }
                        }
                        // 禁用内置标签，避免重叠
                        popupBarSeries->setLabelsVisible(false);
                        // 计算每个类别的总值（用于居中定位）
                        int categories = 0;
                        if (!popupBarSeries->barSets().isEmpty()) {
                            categories = popupBarSeries->barSets().first()->count();
                        }
                        for (int iCat = 0; iCat < categories; ++iCat) {
                            double total = 0.0;
                            for (auto *set2 : popupBarSeries->barSets()) {
                                if (iCat < set2->count()) total += (*set2)[iCat];
                            }
                            // 若无数据，跳过
                            if (total == 0.0) continue;
                            // 类目中心 y
                            qreal y = iCat + 0.5;
                            // 居中点（0 到 total 的中点）
                            QPointF p0 = popupChart->mapToPosition(QPointF(0.0,  y), popupBarSeries);
                            QPointF pT = popupChart->mapToPosition(QPointF(total, y), popupBarSeries);
                            qreal cx = (p0.x() + pT.x()) / 2.0;
                            qreal cy = p0.y();
                            // 文本内容：显示 total
                            auto *txt = new QGraphicsSimpleTextItem(QString::number(total, 'f', 2));
                            txt->setBrush(QBrush(Qt::black));
                            txt->setData(0, "bar-value-label");
                            txt->setZValue(10);
                            QRectF br = txt->boundingRect();
                            txt->setPos(cx - br.width() / 2.0, cy - br.height() / 2.0);
                            popupChart->scene()->addItem(txt);
                        }
                        barTypeIndex++;
                    } else if (auto *ss = qobject_cast<QtCharts::QScatterSeries*>(s)) {
                        QtCharts::QScatterSeries *popupScatterSeries = nullptr;
                        if (!seriesName.isEmpty() && existingSeries.contains(seriesName)) {
                            popupScatterSeries = qobject_cast<QtCharts::QScatterSeries*>(existingSeries[seriesName]);
                        } else {
                            if (scatterTypeIndex < popupScatterSeriesList.size()) {
                                popupScatterSeries = popupScatterSeriesList[scatterTypeIndex];
                            }
                        }
                        if (!popupScatterSeries) {
                            // 创建新系列
                            popupScatterSeries = new QtCharts::QScatterSeries();
                            if (!seriesName.isEmpty()) popupScatterSeries->setName(seriesName);
                            popupScatterSeries->setPen(ss->pen());
                            popupScatterSeries->setColor(ss->color());
                            popupScatterSeries->setBrush(ss->brush());
                            popupScatterSeries->setMarkerSize(ss->markerSize());
                            popupChart->addSeries(popupScatterSeries);
                            // 附加到坐标轴
                            for (auto *axis : popupChart->axes()) {
                                popupScatterSeries->attachAxis(axis);
                            }
                            if (scatterTypeIndex >= popupScatterSeriesList.size()) popupScatterSeriesList.append(popupScatterSeries);
                            else popupScatterSeriesList[scatterTypeIndex] = popupScatterSeries;
                        }
                        // 更新现有系列数据
                        popupScatterSeries->replace(ss->points());
                        scatterTypeIndex++;
                    }
                }
                
                // 清理不再需要的系列
                for (auto it = existingSeries.begin(); it != existingSeries.end(); ++it) {
                    bool found = false;
                    for (QtCharts::QAbstractSeries *s : srcChart->series()) {
                        if (s->name() == it.key()) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        popupChart->removeSeries(it.value());
                        delete it.value();
                    }
                }
                
                // 如果是历史趋势图，还需要更新坐标轴范围
                if (sourceView == predictionHistoryChart) {
                    // 更新X轴（时间轴）范围
                    QList<QtCharts::QAbstractAxis*> axes = popupChart->axes();
                    QtCharts::QDateTimeAxis *popupTimeAxis = nullptr;
                    QtCharts::QValueAxis *popupValueAxis = nullptr;
                    
                    for (auto *axis : axes) {
                        if (auto *ta = qobject_cast<QtCharts::QDateTimeAxis*>(axis)) {
                            popupTimeAxis = ta;
                        } else if (auto *va = qobject_cast<QtCharts::QValueAxis*>(axis)) {
                            popupValueAxis = va;
                        }
                    }
                    
                    // 从源图表获取坐标轴范围
                    QList<QtCharts::QAbstractAxis*> srcAxes = srcChart->axes();
                    QtCharts::QDateTimeAxis *srcTimeAxis = nullptr;
                    QtCharts::QValueAxis *srcValueAxis = nullptr;
                    
                    for (auto *axis : srcAxes) {
                        if (auto *ta = qobject_cast<QtCharts::QDateTimeAxis*>(axis)) {
                            srcTimeAxis = ta;
                        } else if (auto *va = qobject_cast<QtCharts::QValueAxis*>(axis)) {
                            srcValueAxis = va;
                        }
                    }
                    
                    // 同步坐标轴范围
                    if (popupTimeAxis && srcTimeAxis) {
                        popupTimeAxis->setMin(srcTimeAxis->min());
                        popupTimeAxis->setMax(srcTimeAxis->max());
                    }
                    if (popupValueAxis && srcValueAxis) {
                        popupValueAxis->setMin(srcValueAxis->min());
                        popupValueAxis->setMax(srcValueAxis->max());
                    }
                }
            }
        };
        // 建立与源图表的系列及坐标轴变动信号的连接
        auto wireSeriesSignals = [=](QtCharts::QAbstractSeries *s){
            if (auto *xy = qobject_cast<QtCharts::QXYSeries*>(s)) {
                QObject::connect(xy, &QtCharts::QXYSeries::pointsReplaced, dlg, doSync, Qt::QueuedConnection);
                QObject::connect(xy, &QtCharts::QXYSeries::pointAdded, dlg, doSync, Qt::QueuedConnection);
                QObject::connect(xy, &QtCharts::QXYSeries::pointRemoved, dlg, doSync, Qt::QueuedConnection);
            } else if (auto *bars = qobject_cast<QtCharts::QHorizontalBarSeries*>(s)) {
                for (auto *set : bars->barSets()) {
                    QObject::connect(set, &QtCharts::QBarSet::valuesAdded, dlg, doSync, Qt::QueuedConnection);
                    QObject::connect(set, &QtCharts::QBarSet::valuesRemoved, dlg, doSync, Qt::QueuedConnection);
                    QObject::connect(set, &QtCharts::QBarSet::valueChanged, dlg, doSync, Qt::QueuedConnection);
                }
            }
        };
        if (auto *srcChart = sourceView->chart()) {
            for (auto *s : srcChart->series()) wireSeriesSignals(s);
            for (auto *ax : srcChart->axes()) {
                if (auto *vax = qobject_cast<QtCharts::QValueAxis*>(ax)) {
                    QObject::connect(vax, &QtCharts::QValueAxis::rangeChanged, dlg, doSync, Qt::QueuedConnection);
                } else if (auto *tax = qobject_cast<QtCharts::QDateTimeAxis*>(ax)) {
                    QObject::connect(tax, &QtCharts::QDateTimeAxis::rangeChanged, dlg, doSync, Qt::QueuedConnection);
                }
            }
        }
        // 弹窗创建后立即同步一次
        QMetaObject::invokeMethod(dlg, [=](){ doSync(); }, Qt::QueuedConnection);
        
        // A方案：增加兜底定时器（50ms），确保持续刷新
        if (sourceView == spectrumChart) {
            QTimer *fallback = new QTimer(dlg);
            fallback->setTimerType(Qt::PreciseTimer);
            fallback->setInterval(50);
            QObject::connect(fallback, &QTimer::timeout, dlg, doSync, Qt::QueuedConnection);
            fallback->start();
            QObject::connect(dlg, &QDialog::finished, fallback, &QTimer::stop);
            QObject::connect(dlg, &QDialog::destroyed, fallback, &QObject::deleteLater);
        }
        // 弹窗关闭时无需清理，连接以dlg为接收者会自动断开
    }
}

// 标准库
#include <cmath>            // 数学函数
#include <limits>           // 数值限制
#include <algorithm>        // 算法库
#include <sys/statvfs.h>    // 文件系统统计

/**
 * @brief 上位机客户端构造函数
 * @param parent 父窗口指针
 * @details 初始化上位机客户端界面，建立TCP连接，设置定时器等
 */
UpperComputerClient::UpperComputerClient(QWidget *parent) : QMainWindow(parent)
{
    // 初始化成员变量
    autoPredictionEnabled = true;  // 默认启用自动预测
    predictionCompletedConnected = false; // 预测完成信号未连接
    
    // 1. 初始化用户界面
    setupUI();
    setupConnections();
    
    // 2. 初始化加密系统（在日志系统初始化之后）
    initializeEncryption();

    // 3. 创建TCP套接字并连接信号槽
    tcpSocket = new QTcpSocket(this);
    connect(tcpSocket, &QTcpSocket::connected, this, &UpperComputerClient::onConnected);
    connect(tcpSocket, &QTcpSocket::disconnected, this, &UpperComputerClient::onDisconnected);
    connect(tcpSocket, &QTcpSocket::readyRead, this, &UpperComputerClient::onDataReceived);
    connect(tcpSocket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::errorOccurred),
            this, &UpperComputerClient::onSocketError);

    // 3. 创建数据更新定时器（每秒更新一次显示）
    updateTimer = new QTimer(this);
    connect(updateTimer, &QTimer::timeout, this, &UpperComputerClient::updateDataDisplay);
    updateTimer->start(1000);

    // 4. 创建主机状态监控定时器（每秒更新一次系统状态）
    hostStatusTimer = new QTimer(this);
    connect(hostStatusTimer, &QTimer::timeout, this, &UpperComputerClient::updateHostStatus);
    hostStatusTimer->start(1000);
    spectrumRateTimer.start(); // 光谱数据接收速率计时器

    // 5. 创建心跳检测定时器（检测与下位机的连接状态）
    heartbeatCheckTimer = new QTimer(this);
    connect(heartbeatCheckTimer, &QTimer::timeout, this, &UpperComputerClient::updateHeartbeatStatus);
    heartbeatCheckTimer->start(1000);

    // 6. 初始化日志功能
    initLogging();
    
    // 7. 初始化光谱预测器
    initSpectrumPredictor();
    
    // 8. 延迟1秒后自动连接到下位机（给界面初始化时间）
    QTimer::singleShot(1000, this, [this]() {
        if (tcpSocket->state() != QAbstractSocket::ConnectedState) {
            QString host = hostEdit->text();
            int port = portSpinBox->value();
            connectInfiniteRetry = true; // 自动连接：改为无限重试（应用户要求自动尝试）
            startConnectWithRetry(host, port, false);
        }
    });
}

/**
 * @brief 上位机客户端析构函数
 * @details 清理资源，停止定时器，断开网络连接，关闭日志文件
 */
UpperComputerClient::~UpperComputerClient()
{
    // 1. 设置析构标志，防止在析构过程中继续执行某些操作
    isDestroying = true;
    
    // 2. 停止所有定时器，防止在析构过程中继续触发定时器事件
    if (updateTimer) {
        updateTimer->stop();
    }
    if (hostStatusTimer) {
        hostStatusTimer->stop();
    }
    if (heartbeatCheckTimer) {
        heartbeatCheckTimer->stop();
    }
    
    // 3. 断开TCP连接并关闭套接字
    if (tcpSocket) {
        tcpSocket->disconnectFromHost();
        tcpSocket->close();
    }
    
    // 4. 关闭日志文件并释放资源
    if (logFile) {
        logFile->close();
        delete logFile;
        logFile = nullptr;
    }
    
    // 5. 清理预测线程
    if (predictionThread) {
        writeToLog("正在停止预测线程...");
        predictionThread->quit();
        if (!predictionThread->wait(3000)) { // 等待3秒
            writeToLog("预测线程未能在3秒内停止，强制终止");
            predictionThread->terminate();
            predictionThread->wait(1000); // 再等待1秒
        }
        delete predictionThread;
        predictionThread = nullptr;
    }
    
    // 6. 清理预测工作对象
    if (predictionWorker) {
        delete predictionWorker;
        predictionWorker = nullptr;
    }
    
    // 7. 清理光谱预测器
    if (spectrumPredictor) {
        delete spectrumPredictor;
        spectrumPredictor = nullptr;
    }
}

/**
 * @brief 更新主机状态信息
 * @details 监控系统CPU使用率、内存使用率、磁盘使用率等系统状态信息
 *          并更新到界面上显示
 */
void UpperComputerClient::updateHostStatus()
{
    if (isDestroying) return;  // 如果正在析构，直接返回
    
    // 静态变量存储上次的CPU时间统计，用于计算CPU使用率
    static quint64 lastUser=0,lastNice=0,lastSys=0,lastIdle=0,lastIowait=0,lastIrq=0,lastSoftirq=0,lastSteal=0;
    
    // Lambda函数：读取/proc/stat文件并计算CPU使用率
    auto readCpu = [&](){ 
        QFile f("/proc/stat"); 
        if (!f.open(QIODevice::ReadOnly|QIODevice::Text)) return -1.0; 
        QByteArray line=f.readLine(); 
        QList<QByteArray> p=line.split(' '); 
        QVector<quint64> v; 
        for (auto &t:p) { 
            bool ok=false; 
            quint64 x=t.trimmed().toULongLong(&ok); 
            if (ok) v.push_back(x);
        } 
        if (v.size()<7) return -1.0; 
        
        // 解析CPU时间统计
        quint64 user=v[0],nice=v[1],sys=v[2],idle=v[3],iow=v[4],irq=v[5],soft=v[6],steal=(v.size()>7?v[7]:0); 
        
        // 计算CPU使用率
        quint64 prevIdle=lastIdle+lastIowait; 
        quint64 idleNow=idle+iow; 
        quint64 prevNon=lastUser+lastNice+lastSys+lastIrq+lastSoftirq+lastSteal; 
        quint64 nonNow=user+nice+sys+irq+soft+steal; 
        quint64 prevTotal=prevIdle+prevNon; 
        quint64 totalNow=idleNow+nonNow; 
        double cpu= -1.0; 
        if (totalNow>prevTotal) 
            cpu = double((nonNow-prevNon))*100.0/double((totalNow-prevTotal)); 
        
        // 更新上次的统计值
        lastUser=user;lastNice=nice;lastSys=sys;lastIdle=idle;lastIowait=iow;lastIrq=irq;lastSoftirq=soft;lastSteal=steal; 
        return cpu; 
    };
    
    // 获取当前CPU使用率
    double cpuUse = readCpu();

    // Lambda函数：读取内存使用率
    auto readMem = [&](){ 
        // 方法1：使用awk命令直接读取内存信息（更准确）
        QProcess process;
        process.start("awk", QStringList() << "/MemTotal:/ {total=$2} /MemAvailable:/ {avail=$2} END {if(total>0) printf \"%.1f\", (total-avail)*100/total}" << "/proc/meminfo");
        process.waitForFinished(1000);
        if (process.exitCode() == 0) {
            QString output = process.readAllStandardOutput().trimmed();
            bool ok;
            double usage = output.toDouble(&ok);
            if (ok && usage >= 0 && usage <= 100) {
                return usage;
            }
        }
        
        // 方法2：备用方法，直接读取/proc/meminfo文件
        QFile f("/proc/meminfo"); 
        if(!f.open(QIODevice::ReadOnly|QIODevice::Text)) return -1.0; 
        qint64 memTotal=0,memAvail=0; 
        while(!f.atEnd()){ 
            QByteArray l = f.readLine(); 
            QString line = QString::fromUtf8(l).trimmed();
            if(line.startsWith("MemTotal:")) {
                QString value = line.split(':')[1].trimmed().split(' ')[0];
                memTotal = value.toLongLong();
            } else if(line.startsWith("MemAvailable:")) {
                QString value = line.split(':')[1].trimmed().split(' ')[0];
                memAvail = value.toLongLong();
            }
        } 
        if(memTotal<=0) return -1.0; 
        return double(memTotal-memAvail)*100.0/double(memTotal); 
    };
    double memUse = readMem();

    // Lambda函数：读取磁盘使用率
    auto readDisk = [&](){ 
        struct statvfs s; 
        if(statvfs("/", &s)!=0) return -1.0; 
        double total=double(s.f_blocks)*s.f_frsize; 
        double avail=double(s.f_bavail)*s.f_frsize; 
        if(total<=0) return -1.0; 
        return (total-avail)*100.0/total; 
    };
    double diskUse = readDisk();
    
    // 调试信息
    static int debugCounter = 0;
    if (++debugCounter % 10 == 0) {  // 每10秒输出一次调试信息
        qDebug() << "Memory debug - CPU:" << cpuUse << "Memory:" << memUse << "Disk:" << diskUse;
    }

    // Lambda函数：读取系统运行时间
    auto readUptime = [&](){ 
        QFile f("/proc/uptime"); 
        if(!f.open(QIODevice::ReadOnly|QIODevice::Text)) return QString("--"); 
        QByteArray l=f.readAll(); 
        double up=l.split(' ')[0].toDouble(); 
        long long totalSeconds = (long long)up;
        
        // 将总秒数转换为天、时、分、秒格式
        long long days = totalSeconds / 86400;        // 天
        long long hours = (totalSeconds % 86400) / 3600;  // 时
        long long minutes = (totalSeconds % 3600) / 60;   // 分
        long long seconds = totalSeconds % 60;            // 秒
        
        return QString("%1天%2时%3分%4秒").arg(days).arg(hours, 2, 10, QChar('0')).arg(minutes, 2, 10, QChar('0')).arg(seconds, 2, 10, QChar('0'));
    };
    QString upStr = readUptime();

    // 计算光谱数据接收速率（每秒接收的数据点数量）
    double rate = 0.0; 
    qint64 ms = spectrumRateTimer.elapsed(); 
    if (ms>=1000) { 
        rate = double(spectrumCountInWindow) * 1000.0 / double(ms); 
        spectrumRateTimer.restart(); 
        spectrumCountInWindow=0; 
    }

    // 检查TCP连接状态
    QString connectionStatus = "未连接";
    if (tcpSocket) {
        if (tcpSocket->state() == QAbstractSocket::ConnectedState) {
            connectionStatus = "已连接";
        } else if (tcpSocket->state() == QAbstractSocket::ConnectingState) {
            connectionStatus = "连接中";
        }
    }
    
    // 检查心跳状态（用于监控与下位机的通信健康状态）
    QString heartbeatStatus = "无心跳";
    if (heartbeatReceived && lastHeartbeatTime.isValid()) {
        qint64 secondsSinceHeartbeat = lastHeartbeatTime.secsTo(QDateTime::currentDateTime());
        if (secondsSinceHeartbeat <= 15) {
            heartbeatStatus = QString("正常 (%1s前)").arg(secondsSinceHeartbeat);
        } else if (secondsSinceHeartbeat <= 45) {
            heartbeatStatus = QString("延迟 (%1s前)").arg(secondsSinceHeartbeat);
        } else {
            heartbeatStatus = QString("超时 (%1s前)").arg(secondsSinceHeartbeat);
        }
    }

    // 更新上位机状态表格（显示系统监控信息）
    if (hostStatusTable) {
        hostStatusTable->setItem(0,1,new QTableWidgetItem(cpuUse>=0?QString::number(cpuUse,'f',1)+" %":"--"));  // CPU使用率
        hostStatusTable->setItem(1,1,new QTableWidgetItem(memUse>=0?QString::number(memUse,'f',1)+" %":"--"));  // 内存使用率
        hostStatusTable->setItem(2,1,new QTableWidgetItem(diskUse>=0?QString::number(diskUse,'f',1)+" %":"--")); // 磁盘使用率
        hostStatusTable->setItem(3,1,new QTableWidgetItem(upStr));  // 系统运行时间
        hostStatusTable->setItem(4,1,new QTableWidgetItem(QString::number(rate,'f',2)+" 条/秒"));  // 数据接收速率
        hostStatusTable->setItem(5,1,new QTableWidgetItem(connectionStatus));  // TCP连接状态
        hostStatusTable->setItem(6,1,new QTableWidgetItem(heartbeatStatus));   // 心跳状态
    }
}

/**
 * @brief 连接按钮点击事件处理
 * @details 根据当前连接状态，执行连接或断开操作
 */
void UpperComputerClient::onConnectClicked()
{
    QString host = hostEdit->text();  // 获取服务器地址
    int port = portSpinBox->value();  // 获取服务器端口
    
    if (tcpSocket->state() == QAbstractSocket::ConnectedState) {
        // 如果已连接，则断开连接
        cancelConnectRetry();
        tcpSocket->disconnectFromHost();
    } else {
        // 如果未连接，则显示连接进度条并尝试连接
        connectionProgress->setVisible(true);
        connectionProgress->setRange(0, 0);  // 设置为无限进度条
        connectInfiniteRetry = true; // 用户发起：无限重试
        startConnectWithRetry(host, port, true);
    }
}

/**
 * @brief 发送命令按钮点击事件处理
 * @details 检查连接状态，验证命令内容，发送命令到下位机并记录日志
 */
void UpperComputerClient::onSendCommandClicked()
{
    // 检查TCP连接状态
    if (tcpSocket->state() != QAbstractSocket::ConnectedState) {
        QMessageBox::warning(this, "警告", "请先连接到下位机！");
        return;
    }
    
    // 获取并验证命令内容
    QString command = commandEdit->text().trimmed();
    if (command.isEmpty()) {
        QMessageBox::warning(this, "警告", "请输入命令！");
        return;
    }
    
    // 发送命令到下位机
    QByteArray commandData = command.toUtf8();
    
    // 如果启用加密，则加密数据
    if (encryptionEnabled) {
        commandData = encryptData(commandData);
        if (commandData.isEmpty()) {
            QMessageBox::warning(this, "错误", "命令加密失败！");
            return;
        }
    }
    
    tcpSocket->write(commandData);
    tcpSocket->write("\n");  // 添加换行符作为命令结束标志
    
    // 记录命令到历史记录和日志
    commandHistory->append(QString("[%1] 发送: %2").arg(QDateTime::currentDateTime().toString("hh:mm:ss")).arg(command));
    writeToLog(QString("发送命令: %1").arg(command));
    
    // 清空命令输入框
    commandEdit->clear();
}

/**
 * @brief TCP连接成功事件处理
 * @details 更新界面状态，显示连接成功信息，隐藏进度条
 */
void UpperComputerClient::onConnected()
{
    isConnectedState = true;
    isStreaming = false;
    isPaused = false;
    cancelConnectRetry();
    // 连接成功后启动心跳宽限期（例如 8 秒）
    heartbeatTimeoutCount = 0;
    heartbeatReceived = false;
    lastHeartbeatTime = QDateTime();
    heartbeatGraceUntil = QDateTime::currentDateTime().addSecs(8);
    // 更新状态标签显示为已连接
    if (statusLabel) {
        statusLabel->setText("已连接");
        statusLabel->setStyleSheet("QLabel { background-color: #c8e6c9; color: #2e7d32; padding: 5px; border-radius: 3px; font-weight: bold; }");
    }
    refreshPropertyButtonsByState();
    // 连接成功后，如果已加载属性标签，则初始化阈值告警UI
    if (spectrumPredictor) {
        auto labels = spectrumPredictor->getPropertyLabels();
        QStringList qlabels;
        qlabels.reserve(static_cast<int>(labels.size()));
        for (const auto &s : labels) qlabels << QString::fromStdString(s);
        initializeThresholdAlarms(qlabels);
    }
    
    // 更新连接按钮文本和样式
    if (connectButton) {
        connectButton->setText("断开连接");
        connectButton->setStyleSheet("QPushButton { background-color: #F44336; color: white; padding: 8px; border: none; border-radius: 4px; font-weight: bold; } QPushButton:hover { background-color: #D32F2F; } QPushButton:pressed { background-color: #B71C1C; }");
    }
    
    // 隐藏连接进度条
    if (connectionProgress) {
        connectionProgress->setVisible(false);
    }
    logText->append(QString("[%1] 已连接到下位机").arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
    writeToLog("已连接到下位机");
    
    // 请求开始设备状态流
    QJsonObject statusCmd;
    statusCmd["type"] = "START_DEVICE_STATUS_STREAM";
    QJsonDocument doc(statusCmd);
    QByteArray data = doc.toJson(QJsonDocument::Compact);
    tcpSocket->write(data);
    tcpSocket->write("\n");
    logText->append(QString("[%1] 请求开始设备状态监控").arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
    writeToLog("请求开始设备状态监控");
    
    // 请求开始传感器数据流
    tcpSocket->write("GET_SENSOR_DATA");
    tcpSocket->write("\n");
    logText->append(QString("[%1] 请求开始传感器数据流").arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
    writeToLog("请求开始传感器数据流");
}

void UpperComputerClient::onDisconnected()
{
    isConnectedState = false;
    isStreaming = false;
    isPaused = false;
    cancelConnectRetry();
    if (statusLabel) {
        statusLabel->setText("未连接");
        statusLabel->setStyleSheet("QLabel { background-color: #ffcccc; color: #c62828; padding: 5px; border-radius: 3px; font-weight: bold; }");
    }
    // 断开连接后清空阈值告警显示
    for (auto *lbl : thresholdAlarmLabels) {
        if (lbl) lbl->deleteLater();
    }
    thresholdAlarmLabels.clear();
    if (connectButton) {
        connectButton->setText("连接");
        connectButton->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; padding: 8px; border: none; border-radius: 4px; font-weight: bold; } QPushButton:hover { background-color: #45a049; } QPushButton:pressed { background-color: #3d8b40; }");
    }
    if (connectionProgress) {
        connectionProgress->setVisible(false);
    }
    logText->append(QString("[%1] 与下位机断开连接").arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
    writeToLog("与下位机断开连接");
    refreshPropertyButtonsByState();
    dataBuffer.clear();  // 清空数据缓冲区
    
    // 清空下位机状态显示
    if (deviceStatusTable) {
        // 清空所有下位机数据 (行0-9)
        for (int i = 0; i < 10; ++i) {
            deviceStatusTable->setItem(i, 1, new QTableWidgetItem("--"));
        }
    }
}

void UpperComputerClient::onDataReceived()
{
    QByteArray data = tcpSocket->readAll();
    qDebug() << "Received raw data size:" << data.size();
    
    // 将新数据添加到缓冲区
    dataBuffer.append(data);
    
    // 按换行符分割完整的数据
    while (dataBuffer.contains('\n')) {
        int newlinePos = dataBuffer.indexOf('\n');
        QByteArray line = dataBuffer.left(newlinePos);
        dataBuffer.remove(0, newlinePos + 1);
        
        if (line.trimmed().isEmpty()) continue;
        qDebug() << "Processing line size:" << line.size();
        qDebug() << "Line hex:" << line.toHex();
        
        // 如果启用加密，则解密数据
        QByteArray dataToParse = line;
        if (encryptionEnabled) {
            dataToParse = decryptData(line);
            if (dataToParse.isEmpty()) {
                writeToLog("❌ 数据解密失败，跳过此条数据");
                continue;
            }
        }
        
        // 直接解析JSON数据
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(dataToParse, &error);
        
        if (error.error == QJsonParseError::NoError && doc.isObject()) {
            QJsonObject obj = doc.object();
            const QString t = obj.value("type").toString();
            writeToLog(QString("解析JSON成功，类型: %1").arg(t));
            if (t == "DARK_DATA") {
                darkCurrent = obj.value("spectrum_values").toArray();
                hasDark = true; calibStatusLabel->setText(hasWhite ? "已校准(已有暗/白)" : "已捕获暗电流(来自下位机)");
                logText->append(QString("[%1] 收到暗电流").arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
                writeToLog("收到暗电流数据");
                // 保存暗电流数据
                saveCalibrationData(darkCurrent, "dark");
                // 不return，仍可继续走通用更新逻辑
            } else if (t == "WHITE_DATA") {
                whiteReference = obj.value("spectrum_values").toArray();
                hasWhite = true; calibStatusLabel->setText(hasDark ? "已校准(已有暗/白)" : "已捕获白参考(来自下位机)");
                logText->append(QString("[%1] 收到白参考").arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
                writeToLog("收到白参考数据");
                // 保存白参考数据
                saveCalibrationData(whiteReference, "white");
            } else if (t == "device_status") {
                // 下位机运行时间转换为天时分秒格式
                int uptimeSec = obj.value("uptime_sec").toInt();
                long long days = uptimeSec / 86400;
                long long hours = (uptimeSec % 86400) / 3600;
                long long minutes = (uptimeSec % 3600) / 60;
                long long seconds = uptimeSec % 60;
                QString uptimeStr = QString("%1天%2时%3分%4秒")
                    .arg(days)
                    .arg(hours, 2, 10, QChar('0'))
                    .arg(minutes, 2, 10, QChar('0'))
                    .arg(seconds, 2, 10, QChar('0'));
                
                // 更新下位机状态表格 - 设备数据 (行5-9)
                if (deviceStatusTable) {
                    deviceStatusTable->setItem(5,1,new QTableWidgetItem(QString::number(obj.value("device_temp").toDouble(),'f',1)+" °C"));
                    deviceStatusTable->setItem(6,1,new QTableWidgetItem(QString::number(obj.value("lamp_temp").toDouble(),'f',1)+" °C"));
                    deviceStatusTable->setItem(7,1,new QTableWidgetItem(obj.value("detector").toString()));
                    deviceStatusTable->setItem(8,1,new QTableWidgetItem(obj.value("optics").toString()));
                    deviceStatusTable->setItem(9,1,new QTableWidgetItem(uptimeStr));
                }
            } else if (t == "heartbeat") {
                // 心跳数据
                lastHeartbeatTime = QDateTime::currentDateTime();
                heartbeatReceived = true;
                logText->append(QString("[%1] 收到心跳").arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
            }
            updateSensorData(obj);
        } else {
            // 处理非JSON数据（如连接欢迎消息）
            QString decryptedText = QString::fromUtf8(dataToParse);
            // qDebug() << "Non-JSON data received:" << decryptedText;  // 注释掉终端输出
            writeToLog(QString("JSON解析失败: %1").arg(error.errorString()));
            logText->append(QString("[%1] 接收: %2").arg(QDateTime::currentDateTime().toString("hh:mm:ss")).arg(decryptedText));
            writeToLog(QString("接收数据: %1").arg(decryptedText));
        }
    }
}

void UpperComputerClient::onSocketError(QAbstractSocket::SocketError)
{
    QString errorString = tcpSocket->errorString();
    statusLabel->setText("连接错误: " + errorString);
    // 连接过程中不立刻弹窗。若达到最大重试次数则提示
    if (!connectRetryTimer || connectRetryCount >= connectRetryMax) {
        if (connectionProgress) connectionProgress->setVisible(false);
        if (connectUserInitiated) {
            QMessageBox::critical(this, "连接错误", "无法连接到下位机:\n" + errorString);
        }
    } else {
        // 继续尝试下一次重连
        scheduleNextRetry();
    }
}

void UpperComputerClient::updateDataDisplay()
{
    if (!lastSensorData.isEmpty()) {
        QString dataType = lastSensorData["type"].toString();
        if (dataType == "sensor_data") {
            // 更新下位机状态表格 - 传感器数据 (行0-4)
            if (deviceStatusTable) {
                deviceStatusTable->setItem(0, 1, new QTableWidgetItem(lastSensorData["timestamp"].toString()));
                deviceStatusTable->setItem(1, 1, new QTableWidgetItem(QString::number(lastSensorData["temperature"].toDouble(), 'f', 1) + " °C"));
                deviceStatusTable->setItem(2, 1, new QTableWidgetItem(QString::number(lastSensorData["humidity"].toDouble(), 'f', 1) + " %"));
                deviceStatusTable->setItem(3, 1, new QTableWidgetItem(QString::number(lastSensorData["pressure"].toDouble(), 'f', 1) + " hPa"));
                deviceStatusTable->setItem(4, 1, new QTableWidgetItem(lastSensorData["status"].toString()));
            }
        } else if (dataType == "spectrum_data") {
            updateSpectrumDisplay();
            // 更新采集速率计数
            ++spectrumCountInWindow;
        }
    }
}

void UpperComputerClient::updateSpectrumDisplay()
{
    writeToLog("updateSpectrumDisplay 被调用");
    
    if (!lastSensorData.isEmpty() && lastSensorData["type"].toString() == "spectrum_data") {
        writeToLog("检测到光谱数据，开始更新显示");
        if (spectrumChart) updateSpectrumChart();
        // 入库：原始光谱与波长（若有）
        QJsonArray wavelengths = lastSensorData["wavelengths"].toArray();
        QJsonArray spectrumValues = lastSensorData["spectrum_values"].toArray();
        if (!wavelengths.isEmpty() && !spectrumValues.isEmpty()) {
            insertSpectrumRecord(wavelengths, spectrumValues);
        }
        
        // 自动进行光谱预测
        writeToLog("准备进行自动预测");
        performAutoPrediction();
    } else {
        writeToLog("没有光谱数据或数据类型不匹配");
    }
}

void UpperComputerClient::updateSpectrumChart()
{
    writeToLog("📊 开始更新光谱图表...");
    
    if (!lastSensorData.isEmpty() && lastSensorData["type"].toString() == "spectrum_data") {
        using namespace QtCharts;
        QJsonArray wavelengths = lastSensorData["wavelengths"].toArray();
        QJsonArray spectrumValues = lastSensorData["spectrum_values"].toArray();
        
        writeToLog(QString("📏 光谱数据检查 - 波长点数:%1, 光谱点数:%2").arg(wavelengths.size()).arg(spectrumValues.size()));
        
        if (wavelengths.isEmpty() || spectrumValues.isEmpty()) {
            writeToLog("⚠️ 光谱数据为空，跳过图表更新");
            return;
        }

        // 若存在暗电流/白参考且已就绪，对光谱进行校准: (S - D) / (W - D)
        QJsonArray valuesToPlot = spectrumValues;
        writeToLog("🔄 开始光谱数据处理流程...");
        applyCalibrationIfReady(valuesToPlot);
        applyPreprocessing(valuesToPlot, wavelengths);

        // 如果图表不存在，创建新的图表
        if (!spectrumChart) {
            writeToLog("🆕 创建新的光谱图表...");
            auto *chart = new QChart();
            chart->setBackgroundBrush(QColor(250,250,250));
            chart->legend()->hide();

            auto *lineSeries = new QLineSeries(chart);
            
            // 设置光谱曲线的固定颜色和样式
            QPen linePen(QColor(0, 100, 200));  // 深蓝色
            linePen.setWidth(3);  // 增加线条宽度，提高可见性
            linePen.setStyle(Qt::SolidLine);
            linePen.setCapStyle(Qt::RoundCap);  // 设置圆形端点
            linePen.setJoinStyle(Qt::RoundJoin);  // 设置圆形连接
            lineSeries->setPen(linePen);
            
            // 确保线条使用固定颜色，禁用渐变
            lineSeries->setColor(QColor(0, 100, 200));  // 深蓝色
            lineSeries->setBrush(QBrush(QColor(0, 100, 200)));  // 设置填充色

            // 添加数据点
            writeToLog(QString("📈 添加光谱数据点 - 波长范围:[%.1f,%.1f], 数据点数:%1").arg(wavelengths[0].toDouble()).arg(wavelengths[wavelengths.size()-1].toDouble()).arg(qMin(wavelengths.size(), valuesToPlot.size())));
            
            for (int i = 0; i < wavelengths.size() && i < valuesToPlot.size(); ++i) {
                const qreal x = wavelengths[i].toDouble();
                const qreal y = valuesToPlot[i].toDouble();
                lineSeries->append(x, y);
            }
            
            writeToLog("✅ 光谱数据点添加完成");

            // 先创建坐标轴
            writeToLog("📐 创建坐标轴...");
            auto *axisX = new QValueAxis(chart);
            axisX->setTitleText("波长 (nm)");
            axisX->setLabelFormat("%.0f");
            axisX->setLabelsVisible(true);
            axisX->setTitleVisible(true);
            axisX->setTickCount(11);
            // X轴范围锁定为当前波长范围
            if (!wavelengths.isEmpty()) {
                axisX->setRange(wavelengths.first().toDouble(), wavelengths.last().toDouble());
            }
            
            // 设置X轴字体
            QFont axisXFont("Arial", 10);
            axisX->setLabelsFont(axisXFont);
            axisX->setTitleFont(axisXFont);
            
            auto *axisY = new QValueAxis(chart);
            axisY->setTitleText("光谱值");
            axisY->setLabelFormat("%.3f");
            axisY->setLabelsVisible(true);
            axisY->setTitleVisible(true);
            axisY->setTickCount(11);
            // Y轴根据数据留白
            {
                double minY = std::numeric_limits<double>::max();
                double maxY = std::numeric_limits<double>::lowest();
                for (int i = 0; i < valuesToPlot.size(); ++i) {
                    double v = valuesToPlot[i].toDouble();
                    minY = std::min(minY, v);
                    maxY = std::max(maxY, v);
                }
                if (!(minY < maxY)) { minY -= 0.5; maxY += 0.5; }
                double margin = std::max(1e-6, (maxY - minY) * 0.1);
                axisY->setRange(minY - margin, maxY + margin);
            }
            
            // 设置Y轴字体
            QFont axisYFont("Arial", 10);
            axisY->setLabelsFont(axisYFont);
            axisY->setTitleFont(axisYFont);
            
            writeToLog("✅ 坐标轴创建完成");

            // 先添加坐标轴到图表
            writeToLog("🔗 添加坐标轴到图表...");
            chart->addAxis(axisX, Qt::AlignBottom);
            chart->addAxis(axisY, Qt::AlignLeft);
            
            // 再添加系列到图表
            writeToLog("📊 添加系列到图表...");
            chart->addSeries(lineSeries);
            
            // 最后将系列附加到坐标轴
            writeToLog("🔗 附加系列到坐标轴...");
            if (!lineSeries->attachedAxes().contains(axisX)) {
                lineSeries->attachAxis(axisX);
            }
            if (!lineSeries->attachedAxes().contains(axisY)) {
                lineSeries->attachAxis(axisY);
            }
            
            writeToLog("✅ 光谱图表创建完成");

            spectrumChart = new QChartView(spectrumGroup);
            spectrumChart->setChart(chart);
            spectrumChart->setRenderHint(QPainter::Antialiasing);
            spectrumChart->setRenderHint(QPainter::SmoothPixmapTransform);  // 平滑像素变换
            spectrumChart->setMinimumHeight(200);
            spectrumChart->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            // spectrumChart->setOnDoubleClickCallback([this](ZoomableChartView* view){ openChartInWindow(view, " - 光谱曲线图"); });
            if (spectrumGroup && spectrumGroup->layout()) {
                spectrumGroup->layout()->addWidget(spectrumChart);
            }
        } else {
            // 如果图表已存在，更新现有图表的数据
            QChart *chart = spectrumChart->chart();
            if (chart) {
                // 确保图例隐藏
                chart->legend()->hide();
                // 清除现有系列
                QList<QAbstractSeries*> series = chart->series();
                for (QAbstractSeries* s : series) {
                    chart->removeSeries(s);
                    // 不删除系列对象，让Qt自动管理
                }

                // 创建新的系列
                auto *lineSeries = new QLineSeries(chart);
                
                // 设置光谱曲线的固定颜色和样式
                QPen linePen(QColor(0, 100, 200));  // 深蓝色
                linePen.setWidth(3);  // 增加线条宽度，提高可见性
                linePen.setStyle(Qt::SolidLine);
                linePen.setCapStyle(Qt::RoundCap);  // 设置圆形端点
                linePen.setJoinStyle(Qt::RoundJoin);  // 设置圆形连接
                lineSeries->setPen(linePen);
                
                // 确保线条使用固定颜色，禁用渐变
                lineSeries->setColor(QColor(0, 100, 200));  // 深蓝色
                lineSeries->setBrush(QBrush(QColor(0, 100, 200)));  // 设置填充色

                // 添加数据点
                for (int i = 0; i < wavelengths.size() && i < valuesToPlot.size(); ++i) {
                    const qreal x = wavelengths[i].toDouble();
                    const qreal y = valuesToPlot[i].toDouble();
                    lineSeries->append(x, y);
                }

                chart->addSeries(lineSeries);

                // 获取现有坐标轴
                QList<QAbstractAxis*> axes = chart->axes();
                QValueAxis *axisX = nullptr;
                QValueAxis *axisY = nullptr;
                
                for (QAbstractAxis* axis : axes) {
                    if (axis->orientation() == Qt::Horizontal) {
                        axisX = qobject_cast<QValueAxis*>(axis);
                    } else if (axis->orientation() == Qt::Vertical) {
                        axisY = qobject_cast<QValueAxis*>(axis);
                    }
                }

                // 如果坐标轴不存在，创建它们
                if (!axisX) {
                    axisX = new QValueAxis(chart);
                    axisX->setTitleText("波长 (nm)");
                    axisX->setLabelFormat("%.0f");
                    axisX->setLabelsVisible(true);
                    axisX->setTitleVisible(true);
                    axisX->setTickCount(11);
                    
                    QFont axisXFont("Arial", 10);
                    axisX->setLabelsFont(axisXFont);
                    axisX->setTitleFont(axisXFont);
                    
                    chart->addAxis(axisX, Qt::AlignBottom);
                }
                
                if (!axisY) {
                    axisY = new QValueAxis(chart);
                    axisY->setTitleText("光谱值");
                    axisY->setLabelFormat("%.3f");
                    axisY->setLabelsVisible(true);
                    axisY->setTitleVisible(true);
                    axisY->setTickCount(11);
                    
                    QFont axisYFont("Arial", 10);
                    axisY->setLabelsFont(axisYFont);
                    axisY->setTitleFont(axisYFont);
                    
                    chart->addAxis(axisY, Qt::AlignLeft);
                }

                // 将系列附加到坐标轴
                if (!lineSeries->attachedAxes().contains(axisX)) {
                    lineSeries->attachAxis(axisX);
                }
                if (!lineSeries->attachedAxes().contains(axisY)) {
                    lineSeries->attachAxis(axisY);
                }
                // 动态设置坐标轴范围
                if (!wavelengths.isEmpty()) axisX->setRange(wavelengths.first().toDouble(), wavelengths.last().toDouble());
                if (!valuesToPlot.isEmpty()) {
                    double minY = std::numeric_limits<double>::max();
                    double maxY = std::numeric_limits<double>::lowest();
                    for (int i = 0; i < valuesToPlot.size(); ++i) {
                        double v = valuesToPlot[i].toDouble();
                        minY = std::min(minY, v);
                        maxY = std::max(maxY, v);
                    }
                    if (!(minY < maxY)) { minY -= 0.5; maxY += 0.5; }
                    double margin = std::max(1e-6, (maxY - minY) * 0.1);
                    axisY->setRange(minY - margin, maxY + margin);
                }
            }
        }

        // 计算与更新质量指标
        writeToLog("📊 开始计算质量指标...");
        updateQualityMetrics(wavelengths, valuesToPlot);
        writeToLog("✅ 光谱图表更新完成");
        // 通知弹窗进行同频同步
        emit spectrumChartUpdated();
    } else {
        writeToLog("⚠️ 没有光谱数据，跳过图表更新");
    }
}

void UpperComputerClient::updateQualityMetrics(const QJsonArray &wavelengths, const QJsonArray &spectrumValues)
{
    writeToLog("📊 开始计算光谱质量指标...");
    
    // 简易统计：
    // - SNR: (max - min) / stddev
    // - 基线稳定性: 取前后各5%数据的均值差与整体均方差的比值
    // - 完整性: 非NaN且非空数据比例
    // - 质量评分: 基于以上指标0-100综合
    const int n = qMin(wavelengths.size(), spectrumValues.size());
    writeToLog(QString("📏 数据尺寸 - 波长点数:%1, 光谱点数:%2, 处理点数:%3").arg(wavelengths.size()).arg(spectrumValues.size()).arg(n));
    
    if (n <= 1) { 
        writeToLog("⚠️ 数据点太少，跳过质量指标计算");
        snrLabel->setText("--"); baselineLabel->setText("--"); integrityLabel->setText("--"); qualityScoreLabel->setText("--"); 
        return; 
    }

    QVector<double> y; y.reserve(n);
    double minV = std::numeric_limits<double>::infinity();
    double maxV = -std::numeric_limits<double>::infinity();
    int valid = 0;
    double sum = 0.0;
    
    for (int i = 0; i < n; ++i) {
        double v = spectrumValues[i].toDouble();
        if (std::isfinite(v)) {
            y.push_back(v); valid++; sum += v; if (v < minV) minV = v; if (v > maxV) maxV = v;
        }
    }
    
    writeToLog(QString("🔍 数据有效性检查 - 有效点数:%1/%2 (%3%)").arg(valid).arg(n).arg(100.0 * valid / n, 0, 'f', 1));
    
    if (valid < 2) { 
        writeToLog("❌ 有效数据点不足，跳过质量指标计算");
        snrLabel->setText("--"); baselineLabel->setText("--"); integrityLabel->setText("--"); qualityScoreLabel->setText("--"); 
        return; 
    }
    
    double mean = sum / valid;
    double var = 0.0; for (double v : y) { double d = v - mean; var += d * d; }
    var /= (valid - 1);
    double stddev = std::sqrt(std::max(0.0, var));
    double snr = (stddev > 0.0) ? (maxV - minV) / stddev : 0.0;
    
    writeToLog(QString("📈 基础统计 - 均值:%1, 标准差:%2, 范围:[%3,%4]").arg(mean, 0, 'f', 3).arg(stddev, 0, 'f', 3).arg(minV, 0, 'f', 3).arg(maxV, 0, 'f', 3));
    writeToLog(QString("📊 SNR计算 - 信噪比:%1").arg(snr, 0, 'f', 2));

    int edgeCount = qMax(1, int(valid * 0.05));
    double startMean = 0.0; for (int i = 0; i < edgeCount; ++i) startMean += y[i]; startMean /= edgeCount;
    double endMean = 0.0; for (int i = 0; i < edgeCount; ++i) endMean += y[valid - 1 - i]; endMean /= edgeCount;
    double baseline = std::abs(endMean - startMean) / (stddev > 1e-12 ? stddev : 1.0);
    
    writeToLog(QString("📏 基线稳定性 - 边缘点数:%1, 起始均值:%2, 结束均值:%3, 基线值:%4").arg(edgeCount).arg(startMean, 0, 'f', 3).arg(endMean, 0, 'f', 3).arg(baseline, 0, 'f', 3));

    double integrity = double(valid) / double(n);
    writeToLog(QString("✅ 数据完整性 - 完整性:%1%").arg(integrity * 100.0, 0, 'f', 1));

    // 评分：SNR越高越好，baseline越低越好，integrity越高越好
    double snrScore = qBound(0.0, (snr / 50.0) * 100.0, 100.0); // 假定SNR=50接近满分
    double baselineScore = qBound(0.0, (1.0 / (1.0 + baseline)) * 100.0, 100.0);
    double integrityScore = qBound(0.0, integrity * 100.0, 100.0);
    double finalScore = 0.5 * snrScore + 0.2 * baselineScore + 0.3 * integrityScore;
    
    writeToLog(QString("🎯 质量评分 - SNR:%1, 基线:%2, 完整性:%3, 综合:%4").arg(snrScore, 0, 'f', 1).arg(baselineScore, 0, 'f', 1).arg(integrityScore, 0, 'f', 1).arg(finalScore, 0, 'f', 1));

    snrLabel->setText(QString::number(snr, 'f', 2));
    baselineLabel->setText(QString::number(baseline, 'f', 2));
    integrityLabel->setText(QString::number(integrity * 100.0, 'f', 1) + "%");
    qualityScoreLabel->setText(QString::number(finalScore, 'f', 1));
    
    // 质量阈值判定
    lastQualityOk = (snr >= qualityLimits.snrMin) && (baseline <= qualityLimits.baselineMax) && (integrity >= qualityLimits.integrityMin);
    writeToLog(QString("质量阈值判定: %1 (snr %2 %3, baseline %4 %5, integrity %6 %7)")
               .arg(lastQualityOk ? "通过" : "失败")
               .arg(snr >= qualityLimits.snrMin ? ">=" : "<")
               .arg(qualityLimits.snrMin)
               .arg(baseline <= qualityLimits.baselineMax ? "<=" : ">")
               .arg(qualityLimits.baselineMax)
               .arg(integrity >= qualityLimits.integrityMin ? ">=" : "<")
               .arg(qualityLimits.integrityMin));
    writeToLog("✅ 光谱质量指标计算完成");
}

void UpperComputerClient::applyCalibrationIfReady(QJsonArray &spectrumValues)
{
    writeToLog("🔧 开始光谱校准处理...");
    
    if (!(hasDark && hasWhite)) {
        writeToLog("⚠️ 校准数据不完整 - 暗电流:" + QString(hasDark ? "有" : "无") + ", 白参考:" + QString(hasWhite ? "有" : "无"));
        return;
    }
    
    const int n = spectrumValues.size();
    const int dn = darkCurrent.size();
    const int wn = whiteReference.size();
    
    writeToLog(QString("📊 数据尺寸检查 - 光谱:%1, 暗电流:%2, 白参考:%3").arg(n).arg(dn).arg(wn));
    
    if (dn != n || wn != n) {
        writeToLog("❌ 校准数据尺寸不一致，跳过校准");
        return;
    }
    
    // 计算校准前后的统计信息
    double sumBefore = 0.0, sumAfter = 0.0;
    double minBefore = std::numeric_limits<double>::infinity();
    double maxBefore = -std::numeric_limits<double>::infinity();
    double minAfter = std::numeric_limits<double>::infinity();
    double maxAfter = -std::numeric_limits<double>::infinity();
    int validCalibrations = 0;
    
    for (int i = 0; i < n; ++i) {
        double s = spectrumValues[i].toDouble();
        double d = darkCurrent[i].toDouble();
        double w = whiteReference[i].toDouble();
        double denom = (w - d);
        double y = denom != 0.0 ? (s - d) / denom : 0.0;
        
        // 统计校准前数据
        sumBefore += s;
        if (s < minBefore) minBefore = s;
        if (s > maxBefore) maxBefore = s;
        
        // 统计校准后数据
        sumAfter += y;
        if (y < minAfter) minAfter = y;
        if (y > maxAfter) maxAfter = y;
        
        if (denom != 0.0) validCalibrations++;
        
        spectrumValues[i] = y;
    }
    
    double meanBefore = sumBefore / n;
    double meanAfter = sumAfter / n;
    
    writeToLog(QString("✅ 光谱校准完成 - 有效校准点:%1/%2").arg(validCalibrations).arg(n));
    writeToLog(QString("📈 校准前统计 - 均值:%.3f, 范围:[%.3f,%.3f]").arg(meanBefore).arg(minBefore).arg(maxBefore));
    writeToLog(QString("📈 校准后统计 - 均值:%.3f, 范围:[%.3f,%.3f]").arg(meanAfter).arg(minAfter).arg(maxAfter));
}

void UpperComputerClient::applyPreprocessing(QJsonArray &spectrumValues, const QJsonArray &)
{
    writeToLog("🔧 开始光谱预处理...");
    
    const int n = spectrumValues.size();
    if (n <= 2) {
        writeToLog("⚠️ 数据点太少，跳过预处理 (数据点数:" + QString::number(n) + ")");
        return;
    }
    
    writeToLog(QString("📊 预处理输入 - 数据点数:%1").arg(n));
    
    // 计算预处理前的统计信息
    double sumBefore = 0.0, minBefore = std::numeric_limits<double>::infinity(), maxBefore = -std::numeric_limits<double>::infinity();
    for (int i = 0; i < n; ++i) {
        double v = spectrumValues[i].toDouble();
        sumBefore += v;
        if (v < minBefore) minBefore = v;
        if (v > maxBefore) maxBefore = v;
    }
    double meanBefore = sumBefore / n;
    writeToLog(QString("📈 预处理前统计 - 均值:%1, 范围:[%2,%3]").arg(meanBefore, 0, 'f', 3).arg(minBefore, 0, 'f', 3).arg(maxBefore, 0, 'f', 3));

    auto applySmooth = [&](int w){
        writeToLog(QString("🔄 应用平滑处理 - 窗口大小:%1").arg(w));
        if (w % 2 == 0) w += 1; w = qBound(3, w, (n % 2 == 1 ? n : n - 1));
        int half = w / 2; QJsonArray out;
        for (int i = 0; i < n; ++i) { int l = qMax(0, i - half), r = qMin(n - 1, i + half); double s = 0.0; int c = 0; for (int k = l; k <= r; ++k) { s += spectrumValues[k].toDouble(); ++c; } out.append(c ? s / c : spectrumValues[i].toDouble()); }
        spectrumValues = out;
        writeToLog("✅ 平滑处理完成");
    };
    auto applyNormalize = [&](){ 
        writeToLog("🔄 应用归一化处理...");
        double minV = std::numeric_limits<double>::infinity(), maxV = -std::numeric_limits<double>::infinity(); 
        for (int i = 0; i < n; ++i) { double v = spectrumValues[i].toDouble(); if (v < minV) minV = v; if (v > maxV) maxV = v; } 
        double denom = (maxV - minV); 
        if (denom != 0.0) {
            for (int i = 0; i < n; ++i) spectrumValues[i] = (spectrumValues[i].toDouble() - minV) / denom;
            writeToLog(QString("✅ 归一化完成 - 范围:[%.3f,%.3f] -> [0,1]").arg(minV).arg(maxV));
        } else {
            writeToLog("⚠️ 数据范围为零，跳过归一化");
        }
    };
    auto applyBaseline = [&](int edgePercent){ 
        writeToLog(QString("🔄 应用基线校正 - 边缘百分比:%1%").arg(edgePercent));
        edgePercent = std::max(1, std::min(20, edgePercent)); int edge = std::max(1, n * edgePercent / 100); 
        double startMean = 0.0; for (int i = 0; i < edge; ++i) startMean += spectrumValues[i].toDouble(); startMean /= edge; 
        double endMean = 0.0; for (int i = 0; i < edge; ++i) endMean += spectrumValues[n - 1 - i].toDouble(); endMean /= edge; 
        for (int i = 0; i < n; ++i) { double t = double(i) / double(n - 1); double base = (1.0 - t) * startMean + t * endMean; spectrumValues[i] = spectrumValues[i].toDouble() - base; }
        writeToLog(QString("✅ 基线校正完成 - 起始均值:%.3f, 结束均值:%.3f").arg(startMean).arg(endMean));
    };
    auto applyDerivative = [&](int order){ QJsonArray out; out.append(0.0); for (int i = 1; i < n; ++i) { double d1 = spectrumValues[i].toDouble() - spectrumValues[i - 1].toDouble(); out.append(d1); } if (order == 2 && out.size() > 1) { QJsonArray out2; out2.append(0.0); for (int i = 1; i < n; ++i) { double d2 = out[i].toDouble() - out[i - 1].toDouble(); out2.append(d2); } spectrumValues = out2; } else { spectrumValues = out; } };

    // 按当前pipeline顺序应用
    writeToLog(QString("🔄 开始应用预处理管道 - 步骤数:%1").arg(preprocPipeline.size()));
    int stepCount = 0;
    for (const auto &item : preprocPipeline) {
        const QString &name = item.first; const QVariantMap &params = item.second;
        stepCount++;
        writeToLog(QString("📋 步骤%1: %2").arg(stepCount).arg(name));
        
        if (name == "平滑") applySmooth(params.value("window", 9).toInt());
        else if (name == "归一化") applyNormalize();
        else if (name == "基线校正") applyBaseline(params.value("edge_percent", 5).toInt());
        else if (name == "导数") applyDerivative(params.value("order", 1).toInt());
    }
    
    // 计算预处理后的统计信息
    double sumAfter = 0.0, minAfter = std::numeric_limits<double>::infinity(), maxAfter = -std::numeric_limits<double>::infinity();
    for (int i = 0; i < n; ++i) {
        double v = spectrumValues[i].toDouble();
        sumAfter += v;
        if (v < minAfter) minAfter = v;
        if (v > maxAfter) maxAfter = v;
    }
    double meanAfter = sumAfter / n;
    writeToLog(QString("📈 预处理后统计 - 均值:%1, 范围:[%2,%3]").arg(meanAfter, 0, 'f', 3).arg(minAfter, 0, 'f', 3).arg(maxAfter, 0, 'f', 3));
    writeToLog("✅ 光谱预处理完成");
}


void UpperComputerClient::setupUI()
{
    setWindowTitle("上位机客户端 - 下位机监控系统");
    setMinimumSize(1400, 900);
    resize(1600, 1000);
    
    QWidget *centralWidget = new QWidget(this); 
    setCentralWidget(centralWidget);
    
    // 主布局：垂直分割
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setSpacing(15);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    
    // 顶部状态栏
    QHBoxLayout *statusLayout = new QHBoxLayout();
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(10);
    
    statusLabel = new QLabel("未连接");
    statusLabel->setStyleSheet("QLabel { background-color: #ffcccc; padding: 5px; border-radius: 3px; font-weight: bold; }");
    statusLabel->setFixedHeight(30);  // 设置固定高度
    statusLabel->setAlignment(Qt::AlignCenter);  // 文字居中
    
    // 阈值告警容器（放在"已连接"旁边）
    thresholdAlarmLayout = new QHBoxLayout();
    thresholdAlarmLayout->setContentsMargins(6, 2, 6, 2);
    thresholdAlarmLayout->setSpacing(6);

    connectionProgress = new QProgressBar();
    connectionProgress->setVisible(false);
    connectionProgress->setRange(0, 0);
    connectionProgress->setTextVisible(false);
    connectionProgress->setFixedHeight(20);  // 设置固定高度
    
    statusLayout->addWidget(statusLabel);
    // 在状态标签后紧跟阈值告警标签组
    {
        QWidget *alarmContainer = new QWidget();
        alarmContainer->setLayout(thresholdAlarmLayout);
        alarmContainer->setStyleSheet("QWidget { border: 1px solid #cccccc; border-radius: 4px; background-color: transparent; }");
        statusLayout->addWidget(alarmContainer);
    }
    statusLayout->addWidget(connectionProgress);
    statusLayout->addStretch();
    
    // 创建状态栏容器，限制其高度
    QWidget *statusBarWidget = new QWidget();
    statusBarWidget->setLayout(statusLayout);
    statusBarWidget->setFixedHeight(40);  // 限制整个状态栏的高度
    statusBarWidget->setStyleSheet("QWidget { background-color: transparent; }");
    
    mainLayout->addWidget(statusBarWidget);
    // 窗口调整防抖定时器
    if (!resizeDebounceTimer) {
        resizeDebounceTimer = new QTimer(this);
        resizeDebounceTimer->setSingleShot(true);
        resizeDebounceTimer->setInterval(150); // 150ms 防抖
        connect(resizeDebounceTimer, &QTimer::timeout, this, [this]() {
            // 尺寸稳定后统一刷新图表，避免频繁重绘
            if (spectrumChart && spectrumChart->chart()) spectrumChart->chart()->update();
            if (realtimePredictionChart && realtimePredictionChart->chart()) realtimePredictionChart->chart()->update();
            if (predictionHistoryChart && predictionHistoryChart->chart()) predictionHistoryChart->chart()->update();
            // 刷新所有单属性弹窗
            for (auto it = propertyHistoryDialogs.begin(); it != propertyHistoryDialogs.end(); ++it) {
                refreshPropertyHistoryChart(it.key());
            }
        });
    }

    // 事件过滤器将在图表创建时安装
    // 初始化数据库（用于光谱与预测落库）
    initializeDatabase();
    
    // 主要内容区域：水平分割
    QSplitter *mainSplit = new QSplitter(Qt::Horizontal, centralWidget);
    mainSplit->setChildrenCollapsible(false);
    mainSplit->setHandleWidth(8);
    mainSplit->setStyleSheet(
        "QSplitter::handle { background-color: #cccccc; } "
        "QSplitter::handle:horizontal { width: 8px; } "
        "QSplitter::handle:vertical { height: 8px; }"
    );
    
    // 左侧控制面板
    QWidget *controlPanel = createControlPanel();
    controlPanel->setMaximumWidth(500);
    controlPanel->setMinimumWidth(300);
    
    // 右侧数据面板
    QSplitter *rightSplit = new QSplitter(Qt::Vertical, centralWidget);
    rightSplit->setChildrenCollapsible(false);
    rightSplit->setHandleWidth(8);
    rightSplit->setStyleSheet(
        "QSplitter::handle { background-color: #cccccc; } "
        "QSplitter::handle:horizontal { width: 8px; } "
        "QSplitter::handle:vertical { height: 8px; }"
    );
    
    QWidget *topDataPanel = createSpectrumDataPanel();
    QWidget *bottomTabs = createBottomTabs();
    
    rightSplit->addWidget(topDataPanel);
    rightSplit->addWidget(bottomTabs);
    rightSplit->setSizes({600, 300}); // 设置初始比例
    
    mainSplit->addWidget(controlPanel);
    mainSplit->addWidget(rightSplit);
    mainSplit->setSizes({350, 900}); // 设置初始比例
    
    mainLayout->addWidget(mainSplit);
}

QWidget* UpperComputerClient::createControlPanel()
{
    QWidget *panel = new QWidget(); 
    QVBoxLayout *layout = new QVBoxLayout(panel);
    layout->setSpacing(10);
    layout->setContentsMargins(10, 10, 10, 10);
    
    // 创建垂直分割器
    QSplitter *controlSplitter = new QSplitter(Qt::Vertical, panel);
    controlSplitter->setChildrenCollapsible(false);
    controlSplitter->setHandleWidth(6);
    controlSplitter->setStyleSheet(
        "QSplitter::handle { background-color: #cccccc; } "
        "QSplitter::handle:horizontal { width: 6px; } "
        "QSplitter::handle:vertical { height: 6px; }"
    );
    
    // 连接设置组
    QGroupBox *connectionGroup = new QGroupBox("连接设置");
    connectionGroup->setStyleSheet("QGroupBox { font-weight: bold; border: 2px solid #cccccc; border-radius: 5px; margin-top: 15px; padding-top: 15px; } QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }");
    QGridLayout *connLayout = new QGridLayout(connectionGroup);
    connLayout->setSpacing(10);
    connLayout->setContentsMargins(10, 15, 10, 10);
    
    connLayout->addWidget(new QLabel("服务器地址:"), 0, 0);
    hostEdit = new QLineEdit("127.0.0.1");
    hostEdit->setStyleSheet("QLineEdit { padding: 5px; border: 1px solid #cccccc; border-radius: 3px; }");
    connLayout->addWidget(hostEdit, 0, 1);
    
    connLayout->addWidget(new QLabel("端口:"), 1, 0);
    portSpinBox = new QSpinBox();
    portSpinBox->setRange(1, 65535);
    portSpinBox->setValue(8888);
    portSpinBox->setStyleSheet("QSpinBox { padding: 5px; border: 1px solid #cccccc; border-radius: 3px; }");
    connLayout->addWidget(portSpinBox, 1, 1);
    
    connectButton = new QPushButton("连接");
    connectButton->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; padding: 8px; border: none; border-radius: 4px; font-weight: bold; } QPushButton:hover { background-color: #45a049; } QPushButton:pressed { background-color: #3d8b40; }");
    connLayout->addWidget(connectButton, 2, 0, 1, 2);
    
    // 创建上半部分容器（连接设置和采集设置）
    QWidget *topControlWidget = new QWidget();
    QVBoxLayout *topControlLayout = new QVBoxLayout(topControlWidget);
    topControlLayout->setSpacing(10);
    topControlLayout->setContentsMargins(0, 0, 0, 0);
    
    topControlLayout->addWidget(connectionGroup);

    // 命令发送组
    QGroupBox *commandGroup = new QGroupBox("命令发送");
    commandGroup->setStyleSheet("QGroupBox { font-weight: bold; border: 2px solid #cccccc; border-radius: 5px; margin-top: 15px; padding-top: 15px; } QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }");
    QVBoxLayout *cmdLayout = new QVBoxLayout(commandGroup);
    cmdLayout->setSpacing(10);
    cmdLayout->setContentsMargins(10, 15, 10, 10);
    
    commandEdit = new QLineEdit();
    commandEdit->setPlaceholderText("输入命令 (如: GET_STATUS, GET_SPECTRUM, GET_SENSOR_DATA)");
    commandEdit->setStyleSheet("QLineEdit { padding: 5px; border: 1px solid #cccccc; border-radius: 3px; }");
    cmdLayout->addWidget(commandEdit);
    
    QPushButton *sendButton = new QPushButton("发送命令");
    sendButton->setStyleSheet("QPushButton { background-color: #2196F3; color: white; padding: 8px; border: none; border-radius: 4px; font-weight: bold; } QPushButton:hover { background-color: #1976D2; } QPushButton:pressed { background-color: #1565C0; }");
    cmdLayout->addWidget(sendButton);

    // 预设命令按钮
    QHBoxLayout *presetLayout = new QHBoxLayout();
    presetLayout->setSpacing(8);
    QPushButton *versionBtn = new QPushButton("获取版本");
    QPushButton *restartBtn = new QPushButton("重启");
    versionBtn->setStyleSheet("QPushButton { background-color: #FF9800; color: white; padding: 8px; border: none; border-radius: 3px; font-size: 12px; } QPushButton:hover { background-color: #F57C00; }");
    restartBtn->setStyleSheet("QPushButton { background-color: #F44336; color: white; padding: 8px; border: none; border-radius: 3px; font-size: 12px; } QPushButton:hover { background-color: #D32F2F; }");
    presetLayout->addWidget(versionBtn);
    presetLayout->addWidget(restartBtn);
    cmdLayout->addLayout(presetLayout);

    // 数据请求按钮
    QHBoxLayout *dataRequestLayout = new QHBoxLayout();
    dataRequestLayout->setSpacing(8);
    QPushButton *spectrumBtn = new QPushButton("获取光谱");
    QPushButton *sensorBtn = new QPushButton("获取传感器");
    spectrumBtn->setStyleSheet("QPushButton { background-color: #9C27B0; color: white; padding: 8px; border: none; border-radius: 3px; font-size: 12px; } QPushButton:hover { background-color: #7B1FA2; }");
    sensorBtn->setStyleSheet("QPushButton { background-color: #9C27B0; color: white; padding: 8px; border: none; border-radius: 3px; font-size: 12px; } QPushButton:hover { background-color: #7B1FA2; }");
    dataRequestLayout->addWidget(spectrumBtn);
    dataRequestLayout->addWidget(sensorBtn);
    cmdLayout->addLayout(dataRequestLayout);

    // 数据流控制按钮
    QHBoxLayout *streamLayout = new QHBoxLayout();
    streamLayout->setSpacing(8);
    QPushButton *spectrumStreamBtn = new QPushButton("开始流");
    QPushButton *stopStreamBtn = new QPushButton("停止流");
    spectrumStreamBtn->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; padding: 8px; border: none; border-radius: 3px; font-size: 12px; } QPushButton:hover { background-color: #45a049; }");
    stopStreamBtn->setStyleSheet("QPushButton { background-color: #F44336; color: white; padding: 8px; border: none; border-radius: 3px; font-size: 12px; } QPushButton:hover { background-color: #D32F2F; }");
    streamLayout->addWidget(spectrumStreamBtn);
    streamLayout->addWidget(stopStreamBtn);
    cmdLayout->addLayout(streamLayout);

    // 采集设置组
    QGroupBox *acqGroup = new QGroupBox("采集设置");
    acqGroup->setStyleSheet("QGroupBox { font-weight: bold; border: 2px solid #cccccc; border-radius: 5px; margin-top: 15px; padding-top: 15px; } QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }");
    QGridLayout *acqLayout = new QGridLayout(acqGroup);
    acqLayout->setSpacing(10);
    acqLayout->setContentsMargins(10, 15, 10, 10);
    
    acqLayout->addWidget(new QLabel("积分时间(ms):"), 0, 0);
    integrationSpin = new QSpinBox();
    integrationSpin->setRange(1, 60000);
    integrationSpin->setValue(100);
    integrationSpin->setStyleSheet("QSpinBox { padding: 5px; border: 1px solid #cccccc; border-radius: 3px; }");
    acqLayout->addWidget(integrationSpin, 0, 1);
    
    acqLayout->addWidget(new QLabel("平均次数:"), 1, 0);
    averageSpin = new QSpinBox();
    averageSpin->setRange(1, 1000);
    averageSpin->setValue(10);
    averageSpin->setStyleSheet("QSpinBox { padding: 5px; border: 1px solid #cccccc; border-radius: 3px; }");
    acqLayout->addWidget(averageSpin, 1, 1);
    
    sendAcqBtn = new QPushButton("发送采集设置");
    sendAcqBtn->setStyleSheet("QPushButton { background-color: #607D8B; color: white; padding: 8px; border: none; border-radius: 4px; font-weight: bold; } QPushButton:hover { background-color: #546E7A; } QPushButton:pressed { background-color: #455A64; }");
    acqLayout->addWidget(sendAcqBtn, 2, 0, 1, 2);
    
    topControlLayout->addWidget(acqGroup);
    
    // 创建下半部分容器（命令发送）
    QWidget *bottomControlWidget = new QWidget();
    QVBoxLayout *bottomControlLayout = new QVBoxLayout(bottomControlWidget);
    bottomControlLayout->setSpacing(10);
    bottomControlLayout->setContentsMargins(0, 0, 0, 0);
    
    bottomControlLayout->addWidget(commandGroup);
    
    // 将容器添加到分割器
    controlSplitter->addWidget(topControlWidget);
    controlSplitter->addWidget(bottomControlWidget);
    controlSplitter->setSizes({300, 200}); // 设置初始比例
    
    // 将分割器添加到主布局
    layout->addWidget(controlSplitter);

    connect(sendButton, &QPushButton::clicked, this, &UpperComputerClient::onSendCommandClicked);
    connect(versionBtn, &QPushButton::clicked, [this]() { commandEdit->setText("GET_VERSION"); });
    connect(restartBtn, &QPushButton::clicked, [this]() { commandEdit->setText("RESTART"); });

    connect(spectrumBtn, &QPushButton::clicked, [this]() {
        if (tcpSocket->state() == QAbstractSocket::ConnectedState) {
            tcpSocket->write("GET_SPECTRUM"); tcpSocket->write("\n");
            commandHistory->append(QString("[%1] 发送: %2").arg(QDateTime::currentDateTime().toString("hh:mm:ss")).arg("GET_SPECTRUM"));
        } else { commandEdit->setText("GET_SPECTRUM"); QMessageBox::warning(this, "未连接", "请先连接到下位机"); }
    });
    connect(sensorBtn, &QPushButton::clicked, [this]() {
        if (tcpSocket->state() == QAbstractSocket::ConnectedState) {
            tcpSocket->write("GET_SENSOR_DATA"); tcpSocket->write("\n");
            commandHistory->append(QString("[%1] 发送: %2").arg(QDateTime::currentDateTime().toString("hh:mm:ss")).arg("GET_SENSOR_DATA"));
        } else { commandEdit->setText("GET_SENSOR_DATA"); QMessageBox::warning(this, "未连接", "请先连接到下位机"); }
    });
    connect(spectrumStreamBtn, &QPushButton::clicked, [this]() {
        if (tcpSocket->state() == QAbstractSocket::ConnectedState) {
            tcpSocket->write("GET_SPECTRUM_STREAM"); tcpSocket->write("\n");
            commandHistory->append(QString("[%1] 发送: %2").arg(QDateTime::currentDateTime().toString("hh:mm:ss")).arg("GET_SPECTRUM_STREAM"));
            isStreaming = true; isPaused = false; qualityLimitWarned = false; refreshPropertyButtonsByState();
        } else { commandEdit->setText("GET_SPECTRUM_STREAM"); QMessageBox::warning(this, "未连接", "请先连接到下位机"); }
    });
    connect(stopStreamBtn, &QPushButton::clicked, [this]() {
        if (tcpSocket->state() == QAbstractSocket::ConnectedState) {
            sendStopStream();
        } else { commandEdit->setText("STOP_SPECTRUM_STREAM"); QMessageBox::warning(this, "未连接", "请先连接到下位机"); }
    });

    // 发送采集设置（JSON）
    connect(sendAcqBtn, &QPushButton::clicked, [this]() {
        if (tcpSocket->state() != QAbstractSocket::ConnectedState) {
            QMessageBox::warning(this, "未连接", "请先连接到下位机");
            return;
        }
        QJsonObject obj; obj["type"] = "SET_ACQ"; obj["integration_ms"] = integrationSpin->value(); obj["average"] = averageSpin->value();
        QJsonDocument doc(obj); QByteArray data = doc.toJson(QJsonDocument::Compact);
        tcpSocket->write(data); tcpSocket->write("\n");
        commandHistory->append(QString("[%1] 发送采集设置: 积分=%2ms, 平均=%3").arg(QDateTime::currentDateTime().toString("hh:mm:ss")).arg(integrationSpin->value()).arg(averageSpin->value()));
    });

    // 注意：采集校准按钮在 createSpectrumDataPanel 中创建，连接放到该函数中进行

    return panel;
}

QWidget* UpperComputerClient::createSpectrumDataPanel()
{
    QWidget *panel = new QWidget(); 
    QVBoxLayout *layout = new QVBoxLayout(panel);
    layout->setSpacing(15);
    layout->setContentsMargins(10, 10, 10, 10);
    
    QTabWidget *topTabs = new QTabWidget(panel);
    topTabs->setStyleSheet("QTabWidget::pane { border: 1px solid #cccccc; border-radius: 5px; } QTabBar::tab { background-color: #f0f0f0; padding: 8px 16px; margin-right: 2px; border-top-left-radius: 5px; border-top-right-radius: 5px; } QTabBar::tab:selected { background-color: white; border-bottom: 1px solid white; } QTabBar::tab:hover { background-color: #e0e0e0; }");
    
    // 系统状态页面（包含上位机和下位机状态）
    QWidget *systemPage = new QWidget(); 
    QVBoxLayout *systemLayout = new QVBoxLayout(systemPage);
    systemLayout->setSpacing(10);
    systemLayout->setContentsMargins(10, 10, 10, 10);
    
    // 创建垂直分割器
    QSplitter *statusSplitter = new QSplitter(Qt::Vertical, systemPage);
    statusSplitter->setChildrenCollapsible(false);
    
    // 上位机状态组
    QGroupBox *hostDataGroup = new QGroupBox("上位机状态");
    hostDataGroup->setStyleSheet("QGroupBox { font-weight: bold; border: 2px solid #cccccc; border-radius: 5px; margin-top: 15px; padding-top: 15px; } QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }");
    QVBoxLayout *hostDataLayout = new QVBoxLayout(hostDataGroup);
    hostDataLayout->setSpacing(10);
    hostDataLayout->setContentsMargins(10, 15, 10, 10);
    
    // 上位机状态表格
    hostStatusTable = new QTableWidget(7, 2);
    hostStatusTable->setHorizontalHeaderLabels(QStringList() << "参数" << "数值");
    hostStatusTable->verticalHeader()->setVisible(false);
    hostStatusTable->horizontalHeader()->setStretchLastSection(true);
    hostStatusTable->setAlternatingRowColors(true);
    hostStatusTable->setStyleSheet("QTableWidget { gridline-color: #cccccc; } QHeaderView::section { background-color: #f0f0f0; padding: 5px; border: 1px solid #cccccc; }");
    
    QStringList hostLabels = {"CPU使用率","内存使用率","磁盘使用率","系统运行时间","光谱采集速率","连接状态","心跳状态"};
    hostStatusTable->setRowCount(hostLabels.size());
    for (int i = 0; i < hostLabels.size(); ++i) { 
        hostStatusTable->setItem(i,0,new QTableWidgetItem(hostLabels[i])); 
        hostStatusTable->setItem(i,1,new QTableWidgetItem("--")); 
    }
    
    hostDataLayout->addWidget(hostStatusTable);
    statusSplitter->addWidget(hostDataGroup);
    
    // 下位机状态组
    QGroupBox *deviceDataGroup = new QGroupBox("下位机状态");
    deviceDataGroup->setStyleSheet("QGroupBox { font-weight: bold; border: 2px solid #cccccc; border-radius: 5px; margin-top: 15px; padding-top: 15px; } QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }");
    QVBoxLayout *deviceDataLayout = new QVBoxLayout(deviceDataGroup);
    deviceDataLayout->setSpacing(10);
    deviceDataLayout->setContentsMargins(10, 15, 10, 10);
    
    // 下位机状态表格（包含传感器数据）
    deviceStatusTable = new QTableWidget(10, 2);
    deviceStatusTable->setHorizontalHeaderLabels(QStringList() << "参数" << "数值");
    deviceStatusTable->verticalHeader()->setVisible(false);
    deviceStatusTable->horizontalHeader()->setStretchLastSection(true);
    deviceStatusTable->setAlternatingRowColors(true);
    deviceStatusTable->setStyleSheet("QTableWidget { gridline-color: #cccccc; } QHeaderView::section { background-color: #f0f0f0; padding: 5px; border: 1px solid #cccccc; }");
    
    // 合并传感器和下位机数据
    QStringList deviceLabels = {"时间戳", "罐内温度(°C)", "罐内湿度(%)", "罐内气压(hPa)", "罐内状态", "设备温度", "光源温度", "检测器状态", "光路状态", "运行时间"};
    deviceStatusTable->setRowCount(deviceLabels.size());
    for (int i = 0; i < deviceLabels.size(); ++i) { 
        deviceStatusTable->setItem(i,0,new QTableWidgetItem(deviceLabels[i])); 
        deviceStatusTable->setItem(i,1,new QTableWidgetItem("--")); 
    }
    
    deviceDataLayout->addWidget(deviceStatusTable);
    statusSplitter->addWidget(deviceDataGroup);
    
    // 设置分割器比例
    statusSplitter->setStretchFactor(0, 1);
    statusSplitter->setStretchFactor(1, 1);
    
    systemLayout->addWidget(statusSplitter);
    topTabs->addTab(systemPage, "系统状态");
    
    // 为了保持兼容性，创建隐藏的整合表格（用于更新函数）
    dataTable = new QTableWidget(17, 3);

    // 光谱页面
    QWidget *spectrumPage = new QWidget(); 
    QVBoxLayout *spectrumPageLayout = new QVBoxLayout(spectrumPage);
    spectrumPageLayout->setSpacing(10);
    spectrumPageLayout->setContentsMargins(10, 10, 10, 10);
    
    // 创建滚动区域
    QScrollArea *spectrumScrollArea = new QScrollArea();
    spectrumScrollArea->setWidgetResizable(true);
    spectrumScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    spectrumScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    spectrumScrollArea->setStyleSheet("QScrollArea { border: none; }");
    
    QWidget *spectrumContent = new QWidget();
    QVBoxLayout *spectrumContentLayout = new QVBoxLayout(spectrumContent);
    spectrumContentLayout->setSpacing(10);
    spectrumContentLayout->setContentsMargins(0, 0, 0, 0);
    
    spectrumGroup = new QGroupBox("光谱数据");
    spectrumGroup->setStyleSheet("QGroupBox { font-weight: bold; border: 2px solid #cccccc; border-radius: 5px; margin-top: 15px; padding-top: 15px; } QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }");
    QVBoxLayout *spectrumLayout = new QVBoxLayout(spectrumGroup);
    spectrumLayout->setSpacing(10);
    spectrumLayout->setContentsMargins(10, 15, 10, 10);
    
    // 创建水平分割器，左侧是光谱图表，中间是历史图表，右侧是预测结果
    QSplitter *chartAndPredictionSplitter = new QSplitter(Qt::Horizontal);
    chartAndPredictionSplitter->setChildrenCollapsible(false);
    chartAndPredictionSplitter->setHandleWidth(8);
    chartAndPredictionSplitter->setStyleSheet(
        "QSplitter::handle { background-color: #cccccc; } "
        "QSplitter::handle:horizontal { width: 8px; } "
        "QSplitter::handle:vertical { height: 8px; }"
    );
    
    // 光谱图表区域
    QWidget *spectrumChartWidget = new QWidget();
    QVBoxLayout *chartLayout = new QVBoxLayout(spectrumChartWidget);
    chartLayout->setSpacing(5);
    chartLayout->setContentsMargins(0, 0, 0, 0);
    
    spectrumInfo = nullptr; 
    spectrumTable = nullptr;
    spectrumChart = new QtCharts::QChartView();
    spectrumChart->setRenderHint(QPainter::Antialiasing);
    spectrumChart->setRenderHint(QPainter::SmoothPixmapTransform);  // 平滑像素变换
    spectrumChart->setMinimumHeight(200);
    spectrumChart->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    spectrumChart->setStyleSheet("QChartView { border: 1px solid #cccccc; border-radius: 3px; }");
    spectrumChart->installEventFilter(this);  // 安装事件过滤器
    chartLayout->addWidget(spectrumChart);
    
    // 预测历史数据折线图区域
    QGroupBox *historyGroup = new QGroupBox("预测历史趋势");
    historyGroup->setStyleSheet("QGroupBox { font-weight: bold; border: 2px solid #cccccc; border-radius: 5px; margin-top: 0px; padding-top: 15px; } QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }");
    QVBoxLayout *historyLayout = new QVBoxLayout(historyGroup);
    historyLayout->setContentsMargins(5, 15, 5, 5);
    
    predictionHistoryChart = new QtCharts::QChartView();
    predictionHistoryChart->setRenderHint(QPainter::Antialiasing);
    predictionHistoryChart->setMinimumHeight(200);
    predictionHistoryChart->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    predictionHistoryChart->setStyleSheet("QChartView { border: 1px solid #cccccc; border-radius: 3px; background-color: white; }");
    predictionHistoryChart->installEventFilter(this);  // 安装事件过滤器
    
    // 初始化历史数据图表（样式类似光谱图）
    QtCharts::QChart *historyChart = new QtCharts::QChart();
    historyChart->setTitle("预测值历史趋势 (最近10组数据)");
    historyChart->setTitleFont(QFont("Arial", 10, QFont::Bold));
    historyChart->legend()->setVisible(true);
    historyChart->legend()->setAlignment(Qt::AlignBottom);
    historyChart->legend()->setFont(QFont("Arial", 8));
    historyChart->setBackgroundBrush(QColor(250, 250, 250));  // 与光谱图相同的背景色
    historyChart->setMargins(QMargins(10, 10, 10, 10));
    
    predictionHistoryChart->setChart(historyChart);
    // predictionHistoryChart->setOnDoubleClickCallback([this](ZoomableChartView* view){ openChartInWindow(view, " - 历史趋势图(总)"); });
    historyLayout->addWidget(predictionHistoryChart);
    
    // 预测结果显示区域
    QGroupBox *realtimePredictionGroup = new QGroupBox("实时预测结果");
    realtimePredictionGroup->setStyleSheet("QGroupBox { font-weight: bold; border: 2px solid #4CAF50; border-radius: 5px; margin-top: 0px; padding-top: 15px; background-color: #f8f9fa; } QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; color: #4CAF50; }");
    QVBoxLayout *realtimePredictionLayout = new QVBoxLayout(realtimePredictionGroup);
    realtimePredictionLayout->setSpacing(10);
    realtimePredictionLayout->setContentsMargins(10, 15, 10, 10);
    
    // 预测状态标签
    realtimePredictionStatusLabel = new QLabel("等待光谱数据...");
    realtimePredictionStatusLabel->setStyleSheet("QLabel { color: #666; font-weight: bold; font-size: 14px; }");
    realtimePredictionStatusLabel->setAlignment(Qt::AlignCenter);
    realtimePredictionLayout->addWidget(realtimePredictionStatusLabel);
    
    // 预测结果柱状图
    realtimePredictionChart = new QtCharts::QChartView();
    realtimePredictionChart->setRenderHint(QPainter::Antialiasing);
    realtimePredictionChart->setMinimumHeight(200);
    realtimePredictionChart->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    realtimePredictionChart->setStyleSheet("QChartView { border: 1px solid #ddd; border-radius: 3px; background-color: white; }");
    realtimePredictionChart->installEventFilter(this);  // 安装事件过滤器
    
    // 创建初始图表
    QtCharts::QChart *chart = new QtCharts::QChart();
    chart->setBackgroundBrush(QColor(250, 250, 250));
    chart->legend()->hide();
    chart->setAnimationOptions(QtCharts::QChart::NoAnimation); // 关闭动画减少卡顿
    chart->setTitle("实时预测结果");
    chart->setTitleFont(QFont("Arial", 12, QFont::Bold));
    realtimePredictionChart->setChart(chart);
    // realtimePredictionChart->setOnDoubleClickCallback([this](ZoomableChartView* view){ openChartInWindow(view, " - 预测结果柱状图"); });
    
    realtimePredictionLayout->addWidget(realtimePredictionChart);
    
    // 预测时间标签
    realtimePredictionTimeLabel = new QLabel("最后预测时间: --");
    realtimePredictionTimeLabel->setStyleSheet("QLabel { color: #888; font-size: 12px; }");
    realtimePredictionTimeLabel->setAlignment(Qt::AlignCenter);
    realtimePredictionTimeLabel->setAlignment(Qt::AlignCenter);
    realtimePredictionLayout->addWidget(realtimePredictionTimeLabel);
    
    // 初始化历史数据相关变量
    maxHistoryPoints = 10;  // 最多保存10个数据点
    historyUpdateTimer = new QTimer(this);
    historyUpdateTimer->setSingleShot(true);  // 设置为单次触发
    connect(historyUpdateTimer, &QTimer::timeout, this, &UpperComputerClient::updatePredictionHistoryChart);
    
    // 将图表和预测结果添加到分割器
    chartLayout->setStretchFactor(spectrumChart, 1);
    historyLayout->setStretchFactor(predictionHistoryChart, 1);
    
    chartAndPredictionSplitter->addWidget(spectrumChartWidget);
    chartAndPredictionSplitter->addWidget(historyGroup);
    chartAndPredictionSplitter->addWidget(realtimePredictionGroup);
    
    // 设置分割器的初始比例 (2:2:1)
    chartAndPredictionSplitter->setSizes({400, 400, 200});
    
    // 创建垂直分割器，上方是图表区域，下方是控制区域
    QSplitter *spectrumVerticalSplitter = new QSplitter(Qt::Vertical);
    spectrumVerticalSplitter->setChildrenCollapsible(false);
    spectrumVerticalSplitter->setHandleWidth(8);
    spectrumVerticalSplitter->setStyleSheet(
        "QSplitter::handle { background-color: #cccccc; } "
        "QSplitter::handle:horizontal { width: 8px; } "
        "QSplitter::handle:vertical { height: 8px; }"
    );
    
    // 创建上方图表区域容器
    QWidget *chartAreaWidget = new QWidget();
    QVBoxLayout *chartAreaLayout = new QVBoxLayout(chartAreaWidget);
    chartAreaLayout->setSpacing(0);
    chartAreaLayout->setContentsMargins(0, 0, 0, 0);
    chartAreaLayout->addWidget(chartAndPredictionSplitter);
    
    // 创建下方控制区域容器
    QWidget *controlAreaWidget = new QWidget();
    QVBoxLayout *controlAreaLayout = new QVBoxLayout(controlAreaWidget);
    controlAreaLayout->setSpacing(10);
    controlAreaLayout->setContentsMargins(0, 0, 0, 0);
    
    // 创建光谱图表下方的区域，包含质量监控和校准控制
    QHBoxLayout *bottomLayout = new QHBoxLayout();
    bottomLayout->setSpacing(15);
    
    // 质量监控区
    qualityGroup = new QGroupBox("光谱质量监控");
    qualityGroup->setStyleSheet("QGroupBox { font-weight: bold; border: 2px solid #cccccc; border-radius: 5px; margin-top: 15px; padding-top: 15px; } QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }");
    QGridLayout *qualityLayout = new QGridLayout(qualityGroup);
    qualityLayout->setSpacing(10);
    qualityLayout->setContentsMargins(10, 15, 10, 10);
    
    qualityLayout->addWidget(new QLabel("信噪比(SNR):"), 0, 0);
    snrLabel = new QLabel("--");
    snrLabel->setStyleSheet("QLabel { background-color: #f0f0f0; padding: 3px; border: 1px solid #cccccc; border-radius: 3px; }");
    qualityLayout->addWidget(snrLabel, 0, 1);
    
    qualityLayout->addWidget(new QLabel("基线稳定性:"), 1, 0);
    baselineLabel = new QLabel("--");
    baselineLabel->setStyleSheet("QLabel { background-color: #f0f0f0; padding: 3px; border: 1px solid #cccccc; border-radius: 3px; }");
    qualityLayout->addWidget(baselineLabel, 1, 1);
    
    qualityLayout->addWidget(new QLabel("光谱完整性:"), 2, 0);
    integrityLabel = new QLabel("--");
    integrityLabel->setStyleSheet("QLabel { background-color: #f0f0f0; padding: 3px; border: 1px solid #cccccc; border-radius: 3px; }");
    qualityLayout->addWidget(integrityLabel, 2, 1);
    
    qualityLayout->addWidget(new QLabel("质量评分:"), 3, 0);
    qualityScoreLabel = new QLabel("--");
    qualityScoreLabel->setStyleSheet("QLabel { background-color: #f0f0f0; padding: 3px; border: 1px solid #cccccc; border-radius: 3px; font-weight: bold; }");
    qualityLayout->addWidget(qualityScoreLabel, 3, 1);
    
    // 校准控制
    QGroupBox *calibGroup = new QGroupBox("光谱校准");
    calibGroup->setStyleSheet("QGroupBox { font-weight: bold; border: 2px solid #cccccc; border-radius: 5px; margin-top: 15px; padding-top: 15px; } QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }");
    QVBoxLayout *calibLayout = new QVBoxLayout(calibGroup);
    calibLayout->setSpacing(10);
    calibLayout->setContentsMargins(10, 15, 10, 10);
    
    captureDarkBtn = new QPushButton("捕获暗电流");
    captureWhiteBtn = new QPushButton("捕获白参考");
    captureDarkBtn->setStyleSheet("QPushButton { background-color: #FF9800; color: white; padding: 6px; border: none; border-radius: 3px; font-size: 12px; } QPushButton:hover { background-color: #F57C00; }");
    captureWhiteBtn->setStyleSheet("QPushButton { background-color: #FF9800; color: white; padding: 6px; border: none; border-radius: 3px; font-size: 12px; } QPushButton:hover { background-color: #F57C00; }");
    
    calibStatusLabel = new QLabel("未校准");
    calibStatusLabel->setStyleSheet("QLabel { background-color: #ffcccc; padding: 3px; border: 1px solid #ff9999; border-radius: 3px; font-weight: bold; }");
    
    calibLayout->addWidget(captureDarkBtn);
    calibLayout->addWidget(captureWhiteBtn);
    calibLayout->addWidget(calibStatusLabel);
    
    // 将质量监控和校准控制添加到水平布局
    bottomLayout->addWidget(qualityGroup, 1);
    bottomLayout->addWidget(calibGroup, 1);
    
    // 将底部布局添加到控制区域容器
    controlAreaLayout->addLayout(bottomLayout);
    
    // 添加保存按钮到控制区域
    QPushButton *saveSpectrumBtn = new QPushButton("保存光谱");
    saveSpectrumBtn->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; padding: 6px; border: none; border-radius: 3px; font-size: 12px; } QPushButton:hover { background-color: #45a049; }");
    controlAreaLayout->addWidget(saveSpectrumBtn);
    // 简版预处理UI
    QGroupBox *ppGroup = new QGroupBox("光谱预处理");
    ppGroup->setStyleSheet("QGroupBox { font-weight: bold; border: 2px solid #cccccc; border-radius: 5px; margin-top: 15px; padding-top: 15px; } QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }");
    QVBoxLayout *ppMainLayout = new QVBoxLayout(ppGroup);
    ppMainLayout->setSpacing(10);
    ppMainLayout->setContentsMargins(10, 15, 10, 10);

    // 第一行：选择预处理方法和按钮
    QHBoxLayout *ppRow1 = new QHBoxLayout();
    ppRow1->setSpacing(10);
    
    preprocCombo = new QComboBox();
    preprocCombo->setEditable(false);
    preprocCombo->addItem("无");
    preprocCombo->addItem("平滑");
    preprocCombo->addItem("基线校正");
    preprocCombo->addItem("导数");
    preprocCombo->addItem("归一化");
    preprocCombo->setCurrentIndex(0);
    preprocCombo->setStyleSheet("QComboBox { padding: 5px; border: 1px solid #cccccc; border-radius: 3px; } QComboBox::drop-down { border: none; } QComboBox::down-arrow { image: none; border-left: 5px solid transparent; border-right: 5px solid transparent; border-top: 5px solid #666; margin-right: 5px; }");

    QPushButton *addProcBtn = new QPushButton("添加");
    QPushButton *clearProcBtn = new QPushButton("清空");
    addProcBtn->setStyleSheet("QPushButton { background-color: #2196F3; color: white; padding: 6px; border: none; border-radius: 3px; font-size: 12px; } QPushButton:hover { background-color: #1976D2; }");
    clearProcBtn->setStyleSheet("QPushButton { background-color: #F44336; color: white; padding: 6px; border: none; border-radius: 3px; font-size: 12px; } QPushButton:hover { background-color: #D32F2F; }");

    ppRow1->addWidget(new QLabel("选择:"));
    ppRow1->addWidget(preprocCombo);
    ppRow1->addWidget(addProcBtn);
    ppRow1->addWidget(clearProcBtn);
    ppRow1->addStretch();

    // 第二行：参数设置
    QHBoxLayout *ppRow2 = new QHBoxLayout();
    ppRow2->setSpacing(15);

    smoothWindow = new QSpinBox(); 
    smoothWindow->setRange(3, 51); 
    smoothWindow->setSingleStep(2); 
    smoothWindow->setValue(9);
    smoothWindow->setStyleSheet("QSpinBox { padding: 3px; border: 1px solid #cccccc; border-radius: 3px; }");
    
    baselineEdge = new QSpinBox(); 
    baselineEdge->setRange(1, 20); 
    baselineEdge->setValue(5);
    baselineEdge->setStyleSheet("QSpinBox { padding: 3px; border: 1px solid #cccccc; border-radius: 3px; }");
    
    derivativeOrder = new QSpinBox(); 
    derivativeOrder->setRange(1, 2); 
    derivativeOrder->setValue(1);
    derivativeOrder->setStyleSheet("QSpinBox { padding: 3px; border: 1px solid #cccccc; border-radius: 3px; }");
    
    lblSmoothWindow = new QLabel("平滑窗口:");
    lblBaselineEdge = new QLabel("基线边缘%:");
    lblDerivativeOrder = new QLabel("导数阶:");

    ppRow2->addWidget(lblSmoothWindow);
    ppRow2->addWidget(smoothWindow);
    ppRow2->addWidget(lblBaselineEdge);
    ppRow2->addWidget(baselineEdge);
    ppRow2->addWidget(lblDerivativeOrder);
    ppRow2->addWidget(derivativeOrder);
    ppRow2->addStretch();

    // 第三行：预处理摘要
    preprocSummary = new QTextEdit(); 
    preprocSummary->setReadOnly(true); 
    preprocSummary->setMaximumHeight(80);
    preprocSummary->setStyleSheet("QTextEdit { border: 1px solid #cccccc; border-radius: 3px; padding: 5px; }");

    // 添加到主布局
    ppMainLayout->addLayout(ppRow1);
    ppMainLayout->addLayout(ppRow2);
    ppMainLayout->addWidget(new QLabel("预处理摘要:"));
    ppMainLayout->addWidget(preprocSummary);
    
    controlAreaLayout->addWidget(ppGroup);
    
    // 将容器添加到分割器
    spectrumVerticalSplitter->addWidget(chartAreaWidget);
    spectrumVerticalSplitter->addWidget(controlAreaWidget);
    spectrumVerticalSplitter->setSizes({600, 300}); // 设置初始比例：图表区域600px，控制区域300px
    
    // 将分割器添加到光谱布局
    spectrumLayout->addWidget(spectrumVerticalSplitter);

    // 将光谱组添加到内容布局
    spectrumContentLayout->addWidget(spectrumGroup);
    
    // 将内容添加到滚动区域
    spectrumScrollArea->setWidget(spectrumContent);
    
    // 将滚动区域添加到页面布局
    spectrumPageLayout->addWidget(spectrumScrollArea);
    topTabs->addTab(spectrumPage, "光谱");

    // 连接校准按钮（此时控件已创建）
    connect(saveSpectrumBtn, &QPushButton::clicked, [this]() {
        if (!lastSpectrumData.isEmpty() && !lastWavelengthData.isEmpty()) {
            saveSpectrumData(lastSpectrumData, lastWavelengthData);
        } else {
            QMessageBox::information(this, "提示", "没有可保存的光谱数据");
        }
    });
    
    connect(captureDarkBtn, &QPushButton::clicked, [this]() {
        if (tcpSocket->state() != QAbstractSocket::ConnectedState) {
            QMessageBox::warning(this, "未连接", "请先连接到下位机");
            return;
        }
        QJsonObject obj; obj["type"] = "REQ_DARK";
        QJsonDocument doc(obj); QByteArray data = doc.toJson(QJsonDocument::Compact);
        tcpSocket->write(data); tcpSocket->write("\n");
        if (calibStatusLabel) calibStatusLabel->setText("已请求暗电流，等待下位机返回...");
        if (logText) logText->append(QString("[%1] 请求暗电流").arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
    });
    connect(captureWhiteBtn, &QPushButton::clicked, [this]() {
        if (tcpSocket->state() != QAbstractSocket::ConnectedState) {
            QMessageBox::warning(this, "未连接", "请先连接到下位机");
            return;
        }
        QJsonObject obj; obj["type"] = "REQ_WHITE";
        QJsonDocument doc(obj); QByteArray data = doc.toJson(QJsonDocument::Compact);
        tcpSocket->write(data); tcpSocket->write("\n");
        if (calibStatusLabel) calibStatusLabel->setText("已请求白参考，等待下位机返回...");
        if (logText) logText->append(QString("[%1] 请求白参考").arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
    });

    auto refreshParamVisibility = [this]() {
        const QString sel = preprocCombo->currentText();
        bool showSmooth = (sel == "平滑");
        bool showBaseline = (sel == "基线校正");
        bool showDeriv = (sel == "导数");
        lblSmoothWindow->setVisible(showSmooth); smoothWindow->setVisible(showSmooth);
        lblBaselineEdge->setVisible(showBaseline); baselineEdge->setVisible(showBaseline);
        lblDerivativeOrder->setVisible(showDeriv); derivativeOrder->setVisible(showDeriv);
    };
    refreshParamVisibility();
    connect(preprocCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [refreshParamVisibility](int){ refreshParamVisibility(); });

    // 预处理选择逻辑
    connect(addProcBtn, &QPushButton::clicked, [this]() {
        const QString sel = preprocCombo->currentText();
        if (sel == "无") return;
        QVariantMap params;
        if (sel == "平滑") params["window"] = smoothWindow->value();
        else if (sel == "基线校正") params["edge_percent"] = baselineEdge->value();
        else if (sel == "导数") params["order"] = derivativeOrder->value();
        preprocPipeline.append(qMakePair(sel, params));
        QString line = sel;
        if (!params.isEmpty()) {
            QStringList kv; for (auto it = params.begin(); it != params.end(); ++it) kv << it.key()+"="+QString::number(it.value().toInt());
            line += "(" + kv.join(",") + ")";
        }
        if (preprocSummary) preprocSummary->append(line);
        if (logText) logText->append(QString("[%1] 添加预处理: %2").arg(QDateTime::currentDateTime().toString("hh:mm:ss")).arg(line));
    });
    connect(clearProcBtn, &QPushButton::clicked, [this]() {
        selectedPreprocs.clear();
        preprocPipeline.clear();
        if (preprocSummary) preprocSummary->clear();
        if (logText) logText->append(QString("[%1] 清空预处理").arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
    });

    layout->addWidget(topTabs); return panel;
}

QWidget* UpperComputerClient::createBottomTabs()
{
    QWidget *panel = new QWidget(); 
    QVBoxLayout *layout = new QVBoxLayout(panel);
    layout->setSpacing(10);
    layout->setContentsMargins(10, 10, 10, 10);
    
    QTabWidget *tabs = new QTabWidget(panel);
    tabs->setStyleSheet("QTabWidget::pane { border: 1px solid #cccccc; border-radius: 5px; } QTabBar::tab { background-color: #f0f0f0; padding: 8px 16px; margin-right: 2px; border-top-left-radius: 5px; border-top-right-radius: 5px; } QTabBar::tab:selected { background-color: white; border-bottom: 1px solid white; } QTabBar::tab:hover { background-color: #e0e0e0; }");
    
    // 通信日志页面
    QWidget *logPage = new QWidget(); 
    QVBoxLayout *logLayout = new QVBoxLayout(logPage);
    logLayout->setSpacing(10);
    logLayout->setContentsMargins(10, 10, 10, 10);
    
    logText = new QTextEdit();
    logText->setReadOnly(true);
    logText->setStyleSheet("QTextEdit { background-color: #f8f8f8; border: 1px solid #cccccc; border-radius: 3px; font-family: 'Consolas', 'Monaco', monospace; font-size: 12px; }");
    
    QPushButton *clearLogBtn = new QPushButton("清空日志");
    clearLogBtn->setStyleSheet("QPushButton { background-color: #F44336; color: white; padding: 6px; border: none; border-radius: 3px; font-size: 12px; } QPushButton:hover { background-color: #D32F2F; }");
    
    logLayout->addWidget(logText);
    logLayout->addWidget(clearLogBtn);
    connect(clearLogBtn, &QPushButton::clicked, logText, &QTextEdit::clear);
    tabs->addTab(logPage, "通信日志");
    
    // 命令历史页面
    QWidget *histPage = new QWidget(); 
    QVBoxLayout *histLayout = new QVBoxLayout(histPage);
    histLayout->setSpacing(10);
    histLayout->setContentsMargins(10, 10, 10, 10);
    
    commandHistory = new QTextEdit();
    commandHistory->setReadOnly(true);
    commandHistory->setStyleSheet("QTextEdit { background-color: #f8f8f8; border: 1px solid #cccccc; border-radius: 3px; font-family: 'Consolas', 'Monaco', monospace; font-size: 12px; }");
    
    histLayout->addWidget(commandHistory);
    tabs->addTab(histPage, "命令历史");
    
    layout->addWidget(tabs);
    return panel;
}

void UpperComputerClient::setupConnections()
{
    if (connectButton) connect(connectButton, &QPushButton::clicked, this, &UpperComputerClient::onConnectClicked);
    if (commandEdit) connect(commandEdit, &QLineEdit::returnPressed, this, &UpperComputerClient::onSendCommandClicked);
}

void UpperComputerClient::updateSensorData(const QJsonObject &data)
{
    lastSensorData = data;
    QString dataType = data["type"].toString();
    writeToLog(QString("updateSensorData 被调用，数据类型: %1").arg(dataType));
    
    if (dataType == "spectrum_data") {
        writeToLog("检测到光谱数据，准备更新显示");
        lastWavelengthData = data["wavelengths"].toArray();
        lastSpectrumData = data["spectrum_values"].toArray();
        // 如果已有暗/白，则将接收到的光谱先行校准并存储为当前光谱
        if (hasDark && hasWhite) {
            QJsonArray tmp = lastSpectrumData;
            applyCalibrationIfReady(tmp);
            lastSpectrumData = tmp;
        }
        logText->append(QString("[%1] 接收光谱数据: 文件=%2, 数据点数=%3").arg(QDateTime::currentDateTime().toString("hh:mm:ss")).arg(data["file_name"].toString()).arg(data["data_points"].toInt()));
        writeToLog(QString("接收光谱数据: 文件=%1, 数据点数=%2").arg(data["file_name"].toString()).arg(data["data_points"].toInt()));
        
        // 保存完整的光谱数据
        if (data.contains("spectrum_values") && data.contains("wavelengths")) {
            QJsonArray spectrumValues = data["spectrum_values"].toArray();
            QJsonArray wavelengths = data["wavelengths"].toArray();
            QString filename = QString("spectrum_%1.csv").arg(data["file_name"].toString());
            saveSpectrumData(spectrumValues, wavelengths, filename);
        }
    } else if (dataType == "spectrum_data_point") {
        updateSpectrumDataPoint(data);
        return;
            } else if (dataType == "heartbeat") {
                // 心跳数据
                lastHeartbeatTime = QDateTime::currentDateTime();
                heartbeatReceived = true;
                heartbeatTimeoutCount = 0;
                logText->append(QString("[%1] 收到心跳").arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
                qDebug() << "Heartbeat received at:" << lastHeartbeatTime.toString("hh:mm:ss");
            } else {
        logText->append(QString("[%1] 接收传感器数据: 温度=%2°C, 湿度=%3%, 气压=%4hPa").arg(QDateTime::currentDateTime().toString("hh:mm:ss")).arg(data["temperature"].toDouble(), 0, 'f', 1).arg(data["humidity"].toDouble(), 0, 'f', 1).arg(data["pressure"].toDouble(), 0, 'f', 1));
    }
    updateDataDisplay();
}

void UpperComputerClient::updateSpectrumDataPoint(const QJsonObject &data)
{
    // 检查是否是新的光谱文件，如果是则清空累积的数据点
    QString fileName = data["file_name"].toString();
    int totalPoints = data["total_points"].toInt();
    int currentIndex = data["index"].toInt();
    
    if (fileName != currentSpectrumFileName || totalPoints != currentSpectrumTotalPoints) {
        spectrumDataPoints.clear();
        currentSpectrumFileName = fileName;
        currentSpectrumTotalPoints = totalPoints;
        writeToLog(QString("🆕 开始新的光谱数据流: %1 (总点数: %2)").arg(fileName).arg(totalPoints));
    }
    
    // 累积数据点
    double wavelength = data["wavelength"].toDouble();
    double spectrumValue = data["spectrum_value"].toDouble();
    spectrumDataPoints.append(QPointF(wavelength, spectrumValue));
    
    // 更新信息显示
    if (spectrumInfo) {
        QString info = QString("文件: %1\n当前数据点: %2/%3\n波长: %4 nm\n光谱值: %5\n时间: %6")
                          .arg(fileName)
                          .arg(currentIndex + 1)
                          .arg(totalPoints)
                          .arg(wavelength, 0, 'f', 2)
                          .arg(spectrumValue, 0, 'f', 4)
                          .arg(data["timestamp"].toString());
        spectrumInfo->setPlainText(info);
    }
    
    // 更新表格显示
    if (spectrumTable) {
        spectrumTable->setRowCount(1);
        spectrumTable->setItem(0, 0, new QTableWidgetItem(QString::number(wavelength, 'f', 2)));
        spectrumTable->setItem(0, 1, new QTableWidgetItem(QString::number(spectrumValue, 'f', 4)));
    }
    
    // 更新光谱图表
    if (spectrumChart) {
        QString chartText = QString("光谱数据流  |  点:%1/%2  波长:%3 nm  值:%4")
                              .arg(currentIndex + 1)
                              .arg(totalPoints)
                              .arg(wavelength, 0, 'f', 1)
                              .arg(spectrumValue, 0, 'f', 4);
        
        if (!spectrumChart->chart()) {
            // 创建新的图表
            auto *chart = new QtCharts::QChart();
            chart->setBackgroundBrush(QColor(250,250,250));
            chart->legend()->hide();
            
            // 创建折线系列
            auto *lineSeries = new QtCharts::QLineSeries();
            lineSeries->setName("光谱数据流");
            
            // 设置光谱曲线的固定颜色和样式
            QPen linePen(QColor(0, 100, 200));  // 深蓝色
            linePen.setWidth(2);
            linePen.setStyle(Qt::SolidLine);
            linePen.setCapStyle(Qt::RoundCap);
            linePen.setJoinStyle(Qt::RoundJoin);
            lineSeries->setPen(linePen);
            lineSeries->setColor(QColor(0, 100, 200));
            
            chart->addSeries(lineSeries);
            
            // 创建坐标轴
            auto *axisX = new QtCharts::QValueAxis();
            axisX->setTitleText("波长 (nm)");
            axisX->setLabelFormat("%.0f");
            axisX->setLabelsVisible(true);
            axisX->setTitleVisible(true);
            axisX->setTickCount(11);
            
            auto *axisY = new QtCharts::QValueAxis();
            axisY->setTitleText("光谱值");
            axisY->setLabelFormat("%.3f");
            axisY->setLabelsVisible(true);
            axisY->setTitleVisible(true);
            axisY->setTickCount(11);
            
            chart->addAxis(axisX, Qt::AlignBottom);
            chart->addAxis(axisY, Qt::AlignLeft);
            lineSeries->attachAxis(axisX);
            lineSeries->attachAxis(axisY);
            
            spectrumChart->setChart(chart);
        }
        
        // 更新图表标题
        spectrumChart->chart()->setTitle(chartText);
        
        // 更新折线系列数据
        QtCharts::QChart *chart = spectrumChart->chart();
        if (chart && !chart->series().isEmpty()) {
            if (auto *lineSeries = qobject_cast<QtCharts::QLineSeries*>(chart->series().first())) {
                lineSeries->replace(spectrumDataPoints);
                
                // 自动调整坐标轴范围
                if (!spectrumDataPoints.isEmpty()) {
                    double minX = spectrumDataPoints.first().x();
                    double maxX = spectrumDataPoints.last().x();
                    double minY = spectrumDataPoints.first().y();
                    double maxY = spectrumDataPoints.first().y();
                    
                    for (const QPointF &point : spectrumDataPoints) {
                        minY = qMin(minY, point.y());
                        maxY = qMax(maxY, point.y());
                    }
                    
                    // 添加一些边距
                    double xRange = maxX - minX;
                    double yRange = maxY - minY;
                    if (xRange > 0) {
                        minX -= xRange * 0.05;
                        maxX += xRange * 0.05;
                    }
                    if (yRange > 0) {
                        minY -= yRange * 0.1;
                        maxY += yRange * 0.1;
                    }
                    
                    // 更新坐标轴范围
                    for (auto *axis : chart->axes()) {
                        if (auto *valueAxis = qobject_cast<QtCharts::QValueAxis*>(axis)) {
                            if (axis->alignment() == Qt::AlignBottom) {
                                valueAxis->setRange(minX, maxX);
                            } else if (axis->alignment() == Qt::AlignLeft) {
                                valueAxis->setRange(minY, maxY);
                            }
                        }
                    }
                }
            }
        }
    }
    // 发射同频同步信号
    emit spectrumChartUpdated();
}



void UpperComputerClient::updateHeartbeatStatus()
{
    if (isDestroying) return;  // 如果正在析构，直接返回
    
    // 调试信息
    static int debugCounter = 0;
    if (++debugCounter % 10 == 0) {  // 每10秒输出一次调试信息
        qDebug() << "Heartbeat Debug - Received:" << heartbeatReceived 
                 << "LastTime:" << lastHeartbeatTime.toString("hh:mm:ss")
                 << "TimeoutCount:" << heartbeatTimeoutCount
                 << "Connection:" << (tcpSocket ? tcpSocket->state() : -1);
    }
    
    // 在宽限期内不累计超时
    const bool inGrace = heartbeatGraceUntil.isValid() && QDateTime::currentDateTime() < heartbeatGraceUntil;
    // 心跳超时处理
    if (heartbeatReceived && lastHeartbeatTime.isValid()) {
        qint64 secondsSinceHeartbeat = lastHeartbeatTime.secsTo(QDateTime::currentDateTime());
        if (secondsSinceHeartbeat <= 15) {
            heartbeatTimeoutCount = 0;
        } else if (secondsSinceHeartbeat <= 45) {
            if (!inGrace) heartbeatTimeoutCount++;
        } else {
            if (!inGrace) heartbeatTimeoutCount++;
        }
    } else {
        if (!inGrace) heartbeatTimeoutCount++;
    }
    
    // 如果心跳超时次数过多，尝试重连
    if (!inGrace && heartbeatTimeoutCount >= 10 && tcpSocket && tcpSocket->state() == QAbstractSocket::ConnectedState) {
        if (logText) {
            logText->append(QString("[%1] 心跳超时，尝试重连").arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
        }
        tcpSocket->disconnectFromHost();
        // 延迟重连
        QTimer::singleShot(2000, this, [this]() {
            if (tcpSocket && tcpSocket->state() != QAbstractSocket::ConnectedState && hostEdit && portSpinBox) {
                QString host = hostEdit->text();
                int port = portSpinBox->value();
                connectInfiniteRetry = true; // 心跳重连：改为无限重试
                startConnectWithRetry(host, port, false);
            }
        });
    }
}

// 初始化日志功能
void UpperComputerClient::initLogging()
{
    // 创建日志和数据目录
    QString appDir = QCoreApplication::applicationDirPath();
    dataDirPath = QDir(appDir).filePath("../logs");
    QDir().mkpath(dataDirPath);
    
    // 创建数据保存目录
    QString spectrumDataDir = QDir(appDir).filePath("../data/spectrum");
    QDir().mkpath(spectrumDataDir);
    
    // 初始化日志文件
    logFileCount = 0;
    logFilePath = QDir(dataDirPath).filePath(QString("communication_%1.log").arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss")));
    logFile = new QFile(logFilePath);
    
    if (logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        writeToLog("=== 上位机通信日志开始 ===");
        writeToLog(QString("日志文件: %1").arg(logFilePath));
        writeToLog(QString("数据目录: %1").arg(dataDirPath));
    }
}

// 写入日志
void UpperComputerClient::writeToLog(const QString &message)
{
    // 将日志写入限制在主线程，其他线程通过排队调用以避免并发访问QIODevice
    if (QThread::currentThread() != this->thread()) {
        const QString msgCopy = message;
        QMetaObject::invokeMethod(this, [this, msgCopy]() { writeToLog(msgCopy); }, Qt::QueuedConnection);
        return;
    }

    // 只输出到日志文件，不在终端显示
    if (!logFile || !logFile->isOpen()) return;

    const QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    const QString logEntry = QString("[%1] %2\n").arg(timestamp).arg(message);

    logFile->write(logEntry.toUtf8());
    logFile->flush();

    if (logFile->size() > logFileMaxSize) {
        rotateLogFile();
    }
}

// === 连接与重试辅助实现 ===
void UpperComputerClient::startConnectWithRetry(const QString &host, int port, bool userInitiated)
{
    connectUserInitiated = userInitiated;
    connectRetryCount = 0;
    if (!connectRetryTimer) {
        connectRetryTimer = new QTimer(this);
        connect(connectRetryTimer, &QTimer::timeout, this, [this]() { scheduleNextRetry(); });
    }
    // 若当前处于连接中或已连上，先断开再连，避免半开状态
    if (tcpSocket->state() != QAbstractSocket::UnconnectedState) {
        tcpSocket->abort();
    }
    // 强化 socket 选项
    tcpSocket->setProxy(QNetworkProxy::NoProxy);
    tcpSocket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
    // 首次立即尝试
    writeToLog("开始连接下位机（含自动重试）...");
    tcpSocket->connectToHost(host, port);
    connectRetryTimer->start(connectRetryBaseDelayMs);
}

void UpperComputerClient::scheduleNextRetry()
{
    if (!hostEdit || !portSpinBox) return;
    if (tcpSocket->state() == QAbstractSocket::ConnectedState) {
        cancelConnectRetry();
        return;
    }
    if (!connectInfiniteRetry && ++connectRetryCount > connectRetryMax) {
        cancelConnectRetry();
        if (connectionProgress) connectionProgress->setVisible(false);
        if (connectUserInitiated) {
            QMessageBox::critical(this, "连接失败", QString("多次重试仍未连接成功（%1次）\n请检查下位机或网络后重试").arg(connectRetryMax));
        }
        return;
    }
    const int delay = connectRetryBaseDelayMs * (1 << qMin(connectRetryCount, 4)); // 指数退避，封顶
    const QString host = hostEdit->text();
    const int port = portSpinBox->value();
    writeToLog(QString("连接重试 #%1，%2 ms 后尝试...").arg(connectRetryCount).arg(delay));
    if (statusLabel) {
        statusLabel->setText(QString("正在重试连接 #%1...(%2ms)").arg(connectRetryCount).arg(delay));
    }
    if (connectRetryTimer) {
        connectRetryTimer->start(delay);
    }
    // 尝试前确保 socket 为干净状态
    tcpSocket->abort();
    tcpSocket->connectToHost(host, port);
}

void UpperComputerClient::cancelConnectRetry()
{
    if (connectRetryTimer) {
        connectRetryTimer->stop();
    }
    connectRetryCount = 0;
}

// 轮转日志文件
void UpperComputerClient::rotateLogFile()
{
    if (!logFile) return;
    
    logFile->close();
    logFileCount++;
    
    // 重命名当前日志文件
    QString oldPath = logFilePath;
    QString newPath = QDir(dataDirPath).filePath(QString("communication_%1_%2.log")
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"))
        .arg(logFileCount));
    QFile::rename(oldPath, newPath);
    
    // 创建新的日志文件
    logFilePath = QDir(dataDirPath).filePath(QString("communication_%1.log")
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss")));
    logFile->setFileName(logFilePath);
    
    if (logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        writeToLog("=== 日志文件轮转 ===");
    }
}

// 保存光谱数据
void UpperComputerClient::saveSpectrumData(const QJsonArray &spectrumData, const QJsonArray &wavelengths, const QString &filename)
{
    QString appDir = QCoreApplication::applicationDirPath();
    QString dataDir = QDir(appDir).filePath("../data/spectrum");
    QDir().mkpath(dataDir);
    
    QString fileName = filename;
    if (fileName.isEmpty()) {
        fileName = QString("spectrum_%1.csv").arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
    }
    
    QString filePath = QDir(dataDir).filePath(fileName);
    QFile file(filePath);
    
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out.setCodec("UTF-8");
        
        // 写入CSV头部
        out << "Wavelength,Spectrum_Value\n";
        
        // 写入数据
        int minSize = qMin(spectrumData.size(), wavelengths.size());
        for (int i = 0; i < minSize; ++i) {
            out << wavelengths[i].toDouble() << "," << spectrumData[i].toDouble() << "\n";
        }
        
        file.close();
        writeToLog(QString("光谱数据已保存: %1 (数据点数: %2)").arg(filePath).arg(minSize));
        
        // 在UI中显示保存信息
        if (logText) {
            logText->append(QString("[%1] 光谱数据已保存: %2").arg(QDateTime::currentDateTime().toString("hh:mm:ss")).arg(fileName));
        }
    } else {
        writeToLog(QString("保存光谱数据失败: %1").arg(filePath));
    }
}

// 保存校准数据
void UpperComputerClient::saveCalibrationData(const QJsonArray &data, const QString &type)
{
    QString appDir = QCoreApplication::applicationDirPath();
    QString dataDir = QDir(appDir).filePath("../data/calibration");
    QDir().mkpath(dataDir);
    
    QString fileName = QString("%1_%2.csv").arg(type).arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
    QString filePath = QDir(dataDir).filePath(fileName);
    QFile file(filePath);
    
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out.setCodec("UTF-8");
        
        // 写入CSV头部
        out << "Index,Value\n";
        
        // 写入数据
        for (int i = 0; i < data.size(); ++i) {
            out << i << "," << data[i].toDouble() << "\n";
        }
        
        file.close();
        writeToLog(QString("%1数据已保存: %2 (数据点数: %3)").arg(type).arg(filePath).arg(data.size()));
        
        // 在UI中显示保存信息
        if (logText) {
            logText->append(QString("[%1] %2数据已保存: %3").arg(QDateTime::currentDateTime().toString("hh:mm:ss")).arg(type).arg(fileName));
        }
    } else {
        writeToLog(QString("保存%1数据失败: %2").arg(type).arg(filePath));
    }
}

// ==================== 光谱预测相关函数实现 ====================

/**
 * @brief 初始化光谱预测器
 * @details 加载LibTorch模型和相关信息
 */
void UpperComputerClient::initSpectrumPredictor()
{
    try {
        // 初始化预测器指针
        spectrumPredictor = nullptr;
        predictionActive = false;
        
        // 模型文件路径 - 使用绝对路径
        QString appDir = QCoreApplication::applicationDirPath();
        QString modelPath = QDir(appDir).filePath("../model/spectrum_model.jit");
        QString modelInfoPath = QDir(appDir).filePath("../model/model_info.json");
        QString preprocessingParamsPath = QDir(appDir).filePath("../model/preprocessing_params.json");
        
        // 检查模型文件是否存在
        if (!QFile::exists(modelPath) || !QFile::exists(modelInfoPath) || !QFile::exists(preprocessingParamsPath)) {
            writeToLog("模型文件或预处理参数文件不存在，请先运行Python训练脚本");
            return;
        }
        
        // 创建光谱预测器
        spectrumPredictor = new SpectrumPredictor(
            modelPath.toStdString(),
            modelInfoPath.toStdString(),
            preprocessingParamsPath.toStdString(),
            "cpu"  // 使用CPU进行推理
        );
        
        // 设置日志回调函数
        spectrumPredictor->setLogCallback([this](const std::string& message) {
            writeToLog(QString::fromStdString(message));
        });
        
        if (spectrumPredictor->isModelLoaded()) {
            writeToLog("光谱预测模型加载成功（使用LibTorch）");
            
            // 初始化预测工作线程
            initPredictionThread();
            
            // 获取属性标签（表格初始化将在UI创建后完成）
            auto propertyLabels = spectrumPredictor->getPropertyLabels();
            writeToLog(QString("加载了 %1 个属性标签").arg(propertyLabels.size()));
            // 提前初始化阈值告警UI
            {
                QStringList qlabels;
                qlabels.reserve(static_cast<int>(propertyLabels.size()));
                for (const auto &s : propertyLabels) qlabels << QString::fromStdString(s);
                initializeThresholdAlarms(qlabels);
            }
        } else {
            writeToLog("光谱预测模型加载失败");
            delete spectrumPredictor;
            spectrumPredictor = nullptr;
        }
        
    } catch (const std::exception& e) {
        writeToLog(QString("初始化光谱预测器时发生错误: %1").arg(e.what()));
        if (spectrumPredictor) {
            delete spectrumPredictor;
            spectrumPredictor = nullptr;
        }
    }
}

/**
 * @brief 初始化预测工作线程
 * @details 创建预测工作线程和工作对象
 */
void UpperComputerClient::initPredictionThread()
{
    writeToLog("开始初始化预测工作线程...");
    
    // 检查预测器是否有效
    if (!spectrumPredictor) {
        writeToLog("❌ 预测器为空，无法初始化预测工作线程");
        return;
    }
    
    writeToLog("预测器检查通过");
    
    // 创建预测工作线程
    writeToLog("创建预测工作线程...");
    predictionThread = new QThread(this);
    predictionWorker = new PredictionWorker();
    
    writeToLog("预测工作对象创建完成");
    
    // 将工作对象移动到工作线程
    writeToLog("移动工作对象到线程...");
    predictionWorker->moveToThread(predictionThread);
    
    writeToLog("设置预测器...");
    // 设置预测器
    predictionWorker->setPredictor(spectrumPredictor);
    
    writeToLog("启动线程...");
    // 启动线程
    predictionThread->start();
    
    writeToLog("预测工作线程初始化完成");
}

/**
 * @brief 开始光谱预测按钮点击事件
 * @details 使用当前光谱数据进行属性预测
 */
void UpperComputerClient::onStartPredictionClicked()
{
    if (!spectrumPredictor || !spectrumPredictor->isModelLoaded()) {
        QMessageBox::warning(this, "警告", "光谱预测模型未加载！");
        return;
    }
    
    if (lastSpectrumData.isEmpty()) {
        QMessageBox::warning(this, "警告", "没有可用的光谱数据！");
        return;
    }
    
    // 开始预测
    predictionActive = true;
    if (startPredictionBtn) startPredictionBtn->setEnabled(false);
    if (stopPredictionBtn) stopPredictionBtn->setEnabled(true);
    if (predictionStatusLabel) predictionStatusLabel->setText("预测中...");
    
    // 执行预测
    QMap<QString, float> results = performPrediction(lastSpectrumData);
    
    if (!results.isEmpty()) {
        lastPredictionResults = results;
        updatePredictionDisplay();
        writeToLog("光谱预测完成");
    } else {
        writeToLog("光谱预测失败");
    }
    
    // 停止预测状态
    predictionActive = false;
    if (startPredictionBtn) startPredictionBtn->setEnabled(true);
    if (stopPredictionBtn) stopPredictionBtn->setEnabled(false);
    if (predictionStatusLabel) predictionStatusLabel->setText("就绪");
}

/**
 * @brief 停止光谱预测按钮点击事件
 * @details 停止光谱预测过程
 */
void UpperComputerClient::onStopPredictionClicked()
{
    predictionActive = false;
    if (startPredictionBtn) startPredictionBtn->setEnabled(true);
    if (stopPredictionBtn) stopPredictionBtn->setEnabled(false);
    if (predictionStatusLabel) predictionStatusLabel->setText("已停止");
    writeToLog("光谱预测已停止");
}

/**
 * @brief 安全更新预测显示
 * @details 在主线程中安全地更新预测显示
 */
void UpperComputerClient::safeUpdatePredictionDisplay()
{
    writeToLog("开始安全更新预测显示...");
    
    if (lastPredictionResults.isEmpty()) {
        writeToLog("预测结果为空，跳过更新");
        return;
    }
    
    writeToLog("预测显示组件检查通过");
    
    // 暂时禁用predictionTable操作，只使用日志显示结果
    writeToLog("=== 预测结果详情 ===");
    int index = 1;
    for (auto it = lastPredictionResults.begin(); it != lastPredictionResults.end(); ++it, ++index) {
        writeToLog(QString("%1. %2: %3").arg(index).arg(it.key()).arg(it.value(), 0, 'f', 4));
    }
    writeToLog("=== 预测结果结束 ===");
    
    // 只更新实时预测显示（这个函数相对安全）
    try {
        updateRealtimePredictionDisplay(lastPredictionResults);
        writeToLog("实时预测显示更新成功");
    } catch (...) {
        writeToLog("❌ 更新实时预测显示时发生异常");
    }
    // 同步刷新阈值告警（安全）
    try {
        updateThresholdAlarms(lastPredictionResults);
    } catch (...) {
        writeToLog("❌ 更新阈值告警时发生异常");
    }
    
    writeToLog("✅ 安全更新预测显示完成");
}

/**
 * @brief 更新预测结果显示
 * @details 更新预测结果表格和图表
 */
void UpperComputerClient::updatePredictionDisplay()
{
    writeToLog("开始更新预测显示...");
    
    if (lastPredictionResults.isEmpty()) {
        writeToLog("预测结果为空，跳过更新");
        return;
    }
    
    if (!predictionTable) {
        writeToLog("❌ predictionTable为空，跳过更新");
        return;
    }
    
    writeToLog("预测显示组件检查通过");
    
    // 简化表格更新 - 直接设置行数而不检查现有行数
    writeToLog("开始更新预测结果表格...");
    
    // 设置表格行数
    int resultCount = lastPredictionResults.size();
    writeToLog(QString("设置表格行数为: %1").arg(resultCount));
    predictionTable->setRowCount(resultCount);
    
    // 填充数据
    int row = 0;
    for (auto it = lastPredictionResults.begin(); it != lastPredictionResults.end(); ++it, ++row) {
        writeToLog(QString("更新第%1行: %2 = %3").arg(row).arg(it.key()).arg(it.value()));
        
        // 设置属性名称（如果还没有设置）
        if (!predictionTable->item(row, 0)) {
            predictionTable->setItem(row, 0, new QTableWidgetItem(it.key()));
        }
        
        // 设置预测值
        predictionTable->setItem(row, 1, new QTableWidgetItem(QString::number(it.value(), 'f', 4)));
    }
    
    // 在日志中显示预测结果
    QString logMessage = "预测结果: ";
    for (auto it = lastPredictionResults.begin(); it != lastPredictionResults.end(); ++it) {
        logMessage += QString("%1=%2, ").arg(it.key()).arg(it.value(), 0, 'f', 4);
    }
    logMessage.chop(2); // 移除最后的逗号和空格
    writeToLog(logMessage);

    // 刷新阈值告警显示
    updateThresholdAlarms(lastPredictionResults);
}

/**
 * @brief 执行光谱预测
 * @param spectrumData 光谱数据
 * @return 预测结果映射
 * @details 使用LibTorch模型进行光谱属性预测
 */
QMap<QString, float> UpperComputerClient::performPrediction(const QJsonArray &spectrumData)
{
    writeToLog("🤖 开始光谱预测处理...");
    QMap<QString, float> results;
    
    if (!spectrumPredictor || !spectrumPredictor->isModelLoaded()) {
        writeToLog("❌ 预测器未加载或无效");
        return results;
    }
    
    writeToLog("✅ 预测器状态检查通过");
    
    try {
        // 将QJsonArray转换为std::vector<float>
        writeToLog(QString("🔄 开始数据格式转换 - 输入数据点数:%1").arg(spectrumData.size()));
        std::vector<float> spectrum;
        int validCount = 0, invalidCount = 0;
        double sum = 0.0, minVal = std::numeric_limits<double>::infinity(), maxVal = -std::numeric_limits<double>::infinity();
        
        for (const QJsonValue& value : spectrumData) {
            if (value.isDouble()) {
                double val = value.toDouble();
                if (std::isfinite(val)) {
                    spectrum.push_back(static_cast<float>(val));
                    validCount++;
                    sum += val;
                    if (val < minVal) minVal = val;
                    if (val > maxVal) maxVal = val;
                } else {
                    invalidCount++;
                }
            } else {
                invalidCount++;
            }
        }
        
        writeToLog(QString("📊 数据转换统计 - 有效:%1, 无效:%2, 总计:%3").arg(validCount).arg(invalidCount).arg(spectrumData.size()));
        if (validCount > 0) {
            writeToLog(QString("📈 转换后数据统计 - 均值:%.3f, 范围:[%.3f,%.3f]").arg(sum/validCount).arg(minVal).arg(maxVal));
        }
        
        if (spectrum.empty()) {
            writeToLog("❌ 光谱数据为空，无法进行预测");
            return results;
        }
        
        writeToLog(QString("🎯 开始执行预测 - 输入向量大小:%1").arg(spectrum.size()));
        // 执行预测
        auto predictionResults = spectrumPredictor->predict(spectrum);
        
        writeToLog(QString("📋 预测结果数量:%1").arg(predictionResults.size()));
        
        // 转换为QMap
        for (const auto& pair : predictionResults) {
            results[QString::fromStdString(pair.first)] = pair.second;
            writeToLog(QString("🔍 预测结果 - %1: %.4f").arg(QString::fromStdString(pair.first)).arg(pair.second));
        }
        
        writeToLog("✅ 光谱预测处理完成");
        
    } catch (const std::exception& e) {
        writeToLog(QString("❌ 预测过程中发生错误: %1").arg(e.what()));
    }
    
    return results;
}

/**
 * @brief 自动进行光谱预测
 * @details 当接收到新光谱数据时自动进行预测
 */
void UpperComputerClient::performAutoPrediction()
{
    writeToLog("🎯 尝试自动预测...");
    
    // 检查自动预测是否启用
    if (!autoPredictionEnabled) {
        writeToLog("❌ 自动预测已禁用，跳过预测");
        return;
    }
    
    // 检查预测器是否可用
    if (!spectrumPredictor || !spectrumPredictor->isModelLoaded()) {
        writeToLog("❌ 预测器未加载，跳过自动预测");
        return;
    }
    
    // 检查是否有光谱数据
    if (lastSensorData.isEmpty() || lastSensorData["type"].toString() != "spectrum_data") {
        writeToLog("❌ 没有光谱数据，跳过预测");
        return;
    }
    
    // 获取光谱数据
    QJsonArray spectrumData = lastSensorData["spectrum_values"].toArray();
    if (spectrumData.isEmpty()) {
        writeToLog("❌ 光谱数据为空，跳过预测");
        return;
    }
    
    // 每次检测前：若光谱质量异常累计次数已达上限，则先停止流再弹窗并终止本次检测
    if (spectrumQualityAnomalyCount >= spectrumQualityAnomalyLimit) {
        writeToLog(QString("⛔ 达到质量异常上限（%1/%2），终止检测").arg(spectrumQualityAnomalyCount).arg(spectrumQualityAnomalyLimit));
        sendStopStream();
        if (!qualityLimitWarned) {
            qualityLimitWarned = true;
            QMessageBox::warning(this, "质量异常", QString("光谱质量异常次数已达%1，上位机已下发停止流指令").arg(spectrumQualityAnomalyCount));
        }
        return;
    }

    writeToLog(QString("📊 开始预测处理，光谱数据点数: %1").arg(spectrumData.size()));
    
    try {
        // 质量不达标则阻止预测
        if (!lastQualityOk) {
            spectrumQualityAnomalyCount += 1;
            writeToLog(QString("⛔ 质量不达标，阻止本次预测（累计%1/%2）").arg(spectrumQualityAnomalyCount).arg(spectrumQualityAnomalyLimit));
            // 将所有物质按钮着色为紫色提示质量失败
            applyPurpleStyleToPropertyButtons();
            // 刷新所有已打开统计弹窗中的全局质量异常计数
            refreshQualityAnomalyCountInOpenDialogs();
            // 超阈值：弹窗提示并发送停止流
            if (spectrumQualityAnomalyCount >= spectrumQualityAnomalyLimit) {
                sendStopStream();
                if (!qualityLimitWarned) {
                    qualityLimitWarned = true;
                    QMessageBox::warning(this, "质量异常", QString("光谱质量异常次数已达%1，系统已停止流并终止检测").arg(spectrumQualityAnomalyCount));
                }
            }
            return;
        }
        // 将QJsonArray转换为std::vector<float>
        std::vector<float> spectrumVector;
        for (const QJsonValue& value : spectrumData) {
            spectrumVector.push_back(static_cast<float>(value.toDouble()));
        }
        
        writeToLog("🚀 提交预测任务到后台线程...");
        
        // 通过预测工作线程执行预测
        if (predictionWorker) {
            // 确保信号只连接一次
            if (!predictionCompletedConnected) {
                writeToLog("🔗 首次连接预测信号...");
                connect(predictionWorker, &PredictionWorker::predictionCompleted,
                        this, &UpperComputerClient::onPredictionCompleted,
                        Qt::QueuedConnection);
                connect(predictionWorker, &PredictionWorker::predictionError,
                        this, &UpperComputerClient::onPredictionError,
                        Qt::QueuedConnection);
                predictionCompletedConnected = true;
                writeToLog("✅ 预测信号连接完成");
            }
            
            // 提交预测任务
            QMetaObject::invokeMethod(predictionWorker, "performPrediction",
                                    Qt::QueuedConnection,
                                    Q_ARG(std::vector<float>, spectrumVector));
            writeToLog("📡 预测任务已提交到后台线程");
        } else {
            writeToLog("❌ 预测工作线程未初始化");
        }
        
    } catch (const std::exception& e) {
        writeToLog(QString("❌ 自动预测失败: %1").arg(e.what()));
    }
}

/**
 * @brief 预测完成回调
 * @details 当后台线程完成预测时调用
 */
void UpperComputerClient::onPredictionCompleted(const QMap<QString, float>& results)
{
    static int predictionCount = 0;
    static QMap<QString, float> lastResults;
    
    // 检查是否是重复的预测结果
    if (lastResults == results) {
        writeToLog("⚠️ 检测到重复的预测结果，跳过处理");
        return;
    }
    
    predictionCount++;
    lastResults = results;
    
    writeToLog(QString("🎉 预测完成！第 %1 次预测").arg(predictionCount));
    writeToLog("📊 预测结果详情:");
    
    // 显示每个属性的预测结果
    for (auto it = results.begin(); it != results.end(); ++it) {
        writeToLog(QString("  %1: %2").arg(it.key()).arg(it.value(), 0, 'f', 4));
    }
    
    // 更新预测结果
    lastPredictionResults = results;
    
    // 计算统计信息
    if (!results.isEmpty()) {
        float minVal = *std::min_element(results.begin(), results.end());
        float maxVal = *std::max_element(results.begin(), results.end());
        writeToLog(QString("📈 预测统计: 最小值=%1, 最大值=%2, 属性数=%3")
                  .arg(minVal, 0, 'f', 4)
                  .arg(maxVal, 0, 'f', 4)
                  .arg(results.size()));
    }
    
    // 直接添加预测数据到历史记录
    addPredictionToHistory(results);
    
    // 立即更新历史图表
    writeToLog("立即更新历史图表...");
    updatePredictionHistoryChart();

    // 同步刷新已打开的单属性弹窗
    for (auto it = propertyHistoryDialogs.begin(); it != propertyHistoryDialogs.end(); ++it) {
        refreshPropertyHistoryChart(it.key());
    }
    
    // 立即更新UI显示
    safeUpdatePredictionDisplay();
    
    writeToLog("✅ 预测结果显示更新完成");
    // 入库：预测结果与正常/异常状态
    insertPredictionRecord(results);
}
static QString normalizeKey(const QString &key)
{
    QString k = key;
    k.replace(" ", "");
    return k.toUpper();
}

void UpperComputerClient::loadThresholdsFromConfig(const QString &configPath)
{
    QFile f(configPath);
    if (!f.exists()) {
        writeToLog("未找到阈值配置文件: " + configPath);
        return;
    }
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        writeToLog("无法打开阈值配置文件: " + configPath);
        return;
    }
    QByteArray data = f.readAll();
    f.close();
    QJsonParseError err; QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        writeToLog("阈值配置解析失败: " + err.errorString());
        return;
    }
    QJsonObject obj = doc.object();
    // 期望结构: { "thresholds": { "BP50": {"min": 180, "max": 320}, ... } }
    if (!obj.contains("thresholds") || !obj["thresholds"].isObject()) {
        writeToLog("阈值配置缺少thresholds对象");
        // 继续尝试读取质量阈值
    }
    QJsonObject th = obj["thresholds"].toObject();
    for (auto it = th.begin(); it != th.end(); ++it) {
        const QString key = normalizeKey(it.key());
        if (!it.value().isObject()) continue;
        QJsonObject item = it.value().toObject();
        if (!item.contains("min") || !item.contains("max")) continue;
        float vmin = static_cast<float>(item.value("min").toDouble(std::numeric_limits<double>::quiet_NaN()));
        float vmax = static_cast<float>(item.value("max").toDouble(std::numeric_limits<double>::quiet_NaN()));
        if (std::isnan(vmin) || std::isnan(vmax)) continue;
        thresholdRanges.insert(key, qMakePair(vmin, vmax));
    }
    writeToLog("阈值配置已加载并覆盖默认值");
    // 读取质量阈值（可选）
    if (obj.contains("quality_limits") && obj["quality_limits"].isObject()) {
        QJsonObject q = obj["quality_limits"].toObject();
        if (q.contains("snrMin")) qualityLimits.snrMin = q.value("snrMin").toDouble(qualityLimits.snrMin);
        if (q.contains("baselineMax")) qualityLimits.baselineMax = q.value("baselineMax").toDouble(qualityLimits.baselineMax);
        if (q.contains("integrityMin")) qualityLimits.integrityMin = q.value("integrityMin").toDouble(qualityLimits.integrityMin);
        if (q.contains("anomalyLimit")) spectrumQualityAnomalyLimit = q.value("anomalyLimit").toInt(spectrumQualityAnomalyLimit);
        writeToLog(QString("质量阈值: snrMin=%1 baselineMax=%2 integrityMin=%3")
                   .arg(qualityLimits.snrMin).arg(qualityLimits.baselineMax).arg(qualityLimits.integrityMin));
        writeToLog(QString("质量异常停止阈值: %1").arg(spectrumQualityAnomalyLimit));
    }
}

void UpperComputerClient::initializeThresholdAlarms(const QStringList &propertyLabels)
{
    if (!thresholdAlarmLayout) return;
    // 清空旧的（完整移除layout内全部项，避免残留左侧标签）
    QLayoutItem *item;
    while ((item = thresholdAlarmLayout->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    thresholdAlarmLabels.clear();
    propertyButtons.clear();
    propertyStatsDialogs.clear();
    anomalyCounts.clear();
    detectCounts.clear();
    thresholdRanges.clear();

    // 默认七个物质（从模型info推断）: BP50, CN, D4052, FLASH, FREEZE, TOTAL, VISC
    // 默认范围可按工业常见经验设置，后续可改为从配置加载
    QMap<QString, QPair<float,float>> defaults;
    defaults.insert("BP50", qMakePair(180.0f, 320.0f));
    defaults.insert("CN", qMakePair(40.0f, 70.0f));
    defaults.insert("D4052", qMakePair(0.78f, 0.88f));
    defaults.insert("FLASH", qMakePair(40.0f, 100.0f));
    defaults.insert("FREEZE", qMakePair(-60.0f, 5.0f));
    defaults.insert("TOTAL", qMakePair(10.0f, 50.0f));
    defaults.insert("VISC", qMakePair(1.0f, 5.0f));

    // 先尝试从配置加载（优先覆盖默认值）
    {
        QString baseDir = QCoreApplication::applicationDirPath();
        // 尝试bin同级或项目根的config目录，优先bin/../config
        QStringList candidates;
        candidates << (baseDir + "/../config/thresholds.json");
        candidates << (baseDir + "/config/thresholds.json");
        candidates << (baseDir + "/../../config/thresholds.json");
        for (const QString &p : candidates) {
            loadThresholdsFromConfig(QDir(p).absolutePath());
        }
        // 若配置文件存在并被读取，thresholdRanges里已有覆盖项
        for (auto it = defaults.begin(); it != defaults.end(); ++it) {
            if (!thresholdRanges.contains(it.key())) {
                thresholdRanges.insert(it.key(), it.value());
            }
        }
    }

    // 为传入标签创建UI（大小写不敏感）：每个物质创建一个按钮
    for (const QString &name : propertyLabels) {
        const QString key = normalizeKey(name);
        if (!thresholdRanges.contains(key)) {
            thresholdRanges.insert(key, qMakePair(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max()));
        }
        anomalyCounts[key] = 0;
        detectCounts[key] = 0;

        QPushButton *btn = new QPushButton(name);
        btn->setObjectName(QString("prop_btn_%1").arg(key));
        // 与"已连接"按钮一致的样式（绿底白字，悬停/按下变色）
        btn->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; padding: 8px; border: none; border-radius: 4px; font-weight: bold; } QPushButton:hover { background-color: #45a049; } QPushButton:pressed { background-color: #3d8b40; }");
        thresholdAlarmLayout->addWidget(btn);
        propertyButtons.insert(key, btn);
        QObject::connect(btn, &QPushButton::clicked, this, [this, key, name]() {
            showPropertyStatsDialog(key, name);
        });
    }
    refreshPropertyButtonsByState();
}

void UpperComputerClient::refreshPropertyButtonsByState()
{
    for (auto it = propertyButtons.begin(); it != propertyButtons.end(); ++it) {
        refreshPropertyButton(it.key());
    }
}

void UpperComputerClient::refreshPropertyButton(const QString &key)
{
    if (!propertyButtons.contains(key)) return;
    // 颜色方案：
    // 未连接/未开始检测: 灰色；检测中: 绿色；检测暂停: 蓝色；当前异常: 红色；历史异常但当前正常: 橙色
    static const char* STYLE_GREEN = "QPushButton { background-color: #4CAF50; color: white; padding: 8px; border: none; border-radius: 4px; font-weight: bold; } QPushButton:hover { background-color: #45a049; } QPushButton:pressed { background-color: #3d8b40; }";
    static const char* STYLE_GRAY  = "QPushButton { background-color: #9E9E9E; color: white; padding: 8px; border: none; border-radius: 4px; font-weight: bold; } QPushButton:hover { background-color: #8E8E8E; } QPushButton:pressed { background-color: #7D7D7D; }";
    static const char* STYLE_BLUE  = "QPushButton { background-color: #2196F3; color: white; padding: 8px; border: none; border-radius: 4px; font-weight: bold; } QPushButton:hover { background-color: #1976D2; } QPushButton:pressed { background-color: #1565C0; }";
    static const char* STYLE_RED   = "QPushButton { background-color: #F44336; color: white; padding: 8px; border: none; border-radius: 4px; font-weight: bold; } QPushButton:hover { background-color: #D32F2F; } QPushButton:pressed { background-color: #B71C1C; }";
    static const char* STYLE_ORANGE= "QPushButton { background-color: #FF9800; color: white; padding: 8px; border: none; border-radius: 4px; font-weight: bold; } QPushButton:hover { background-color: #FB8C00; } QPushButton:pressed { background-color: #EF6C00; }";

    bool abnormalNow = currentAbnormal.value(key, false);
    int anomHist = anomalyCounts.value(key, 0);
    QPushButton *btn = propertyButtons[key];
    if (!isConnectedState || (!isStreaming && !isPaused)) {
        btn->setStyleSheet(STYLE_GRAY);
    } else if (isStreaming) {
        if (abnormalNow) btn->setStyleSheet(STYLE_RED);
        else if (anomHist > 0) btn->setStyleSheet(STYLE_ORANGE);
        else btn->setStyleSheet(STYLE_GREEN);
    } else if (isPaused) {
        btn->setStyleSheet(STYLE_BLUE);
    }
}

void UpperComputerClient::initializeDatabase()
{
    dbm.initialize();
}

void UpperComputerClient::createTablesIfNotExists()
{
    dbm.createTablesIfNotExists();
}

void UpperComputerClient::insertSpectrumRecord(const QJsonArray &wavelengths, const QJsonArray &rawSpectrum)
{
    dbm.insertSpectrumRecord(wavelengths, rawSpectrum);
}

void UpperComputerClient::insertPredictionRecord(const QMap<QString, float>& results)
{
    dbm.insertPredictionRecord(results, thresholdRanges, normalizeKey);
}

// 旧的阈值标签刷新逻辑已由按钮颜色替代

/**
 * @brief 预测错误回调
 * @details 当后台线程预测出错时调用
 */
void UpperComputerClient::onPredictionError(const QString& error)
{
    writeToLog(QString("❌ 预测失败: %1").arg(error));
    writeToLog("🔧 请检查模型文件和光谱数据");
}

/**
 * @brief 更新实时预测结果显示
 * @details 更新光谱图表右侧的实时预测结果
 */
void UpperComputerClient::updateRealtimePredictionDisplay(const QMap<QString, float>& results)
{
    writeToLog("开始更新实时预测显示...");
    
    if (!realtimePredictionChart || !realtimePredictionStatusLabel || !realtimePredictionTimeLabel) {
        writeToLog("❌ 实时预测显示组件为空，跳过更新");
        return;
    }
    
    writeToLog("实时预测显示组件检查通过");
    
    // 更新状态标签
    realtimePredictionStatusLabel->setText("预测完成");
    //         realaltimePredictionStatusLabel->setStyleSheet("QLabel { color: #4CAF50; font-weight: bold; font-size: 14px; }");
    
    writeToLog("状态标签更新完成");
    
    // 更新预测时间
    QString currentTime = QDateTime::currentDateTime().toString("hh:mm:ss");
    realtimePredictionTimeLabel->setText(QString("最后预测时间: %1").arg(currentTime));
    
    writeToLog("时间标签更新完成");
    
    // 获取图表对象
    QtCharts::QChart *chart = realtimePredictionChart->chart();
    if (!chart) {
        writeToLog("❌ 图表对象为空，跳过更新");
        return;
    }
    
    // 智能系列和坐标轴管理（避免重复创建）
    writeToLog("开始智能系列和坐标轴管理...");
    
    // 获取现有坐标轴
    QList<QtCharts::QAbstractAxis*> existingAxes = chart->axes();
    QtCharts::QBarCategoryAxis *axisY = nullptr;
    QtCharts::QValueAxis *axisX = nullptr;
    
    // 查找现有坐标轴
    for (QtCharts::QAbstractAxis* axis : existingAxes) {
        if (QtCharts::QBarCategoryAxis *categoryAxis = qobject_cast<QtCharts::QBarCategoryAxis*>(axis)) {
            axisY = categoryAxis;
            writeToLog("找到现有Y轴（属性轴）");
        } else if (QtCharts::QValueAxis *valueAxis = qobject_cast<QtCharts::QValueAxis*>(axis)) {
            axisX = valueAxis;
            writeToLog("找到现有X轴（数值轴）");
        }
    }
    
    // 清除现有系列
    chart->removeAllSeries();
    
    // 创建横向柱状图系列
    QtCharts::QBarSet *barSet = new QtCharts::QBarSet("预测值");
    QtCharts::QHorizontalBarSeries *barSeries = new QtCharts::QHorizontalBarSeries();
    
    // 定义颜色方案
    QList<QColor> colors = {
        QColor(76, 175, 80),   // 绿色
        QColor(33, 150, 243),  // 蓝色
        QColor(255, 152, 0),   // 橙色
        QColor(156, 39, 176),  // 紫色
        QColor(244, 67, 54),   // 红色
        QColor(0, 188, 212),   // 青色
        QColor(255, 193, 7),   // 黄色
        QColor(96, 125, 139)   // 蓝灰色
    };
    
    // 填充数据
    QStringList categories;
    int colorIndex = 0;
    for (auto it = results.begin(); it != results.end(); ++it) {
        writeToLog(QString("添加柱状图数据: %1 = %2").arg(it.key()).arg(it.value()));
        
        *barSet << it.value();
        categories << it.key();
        
        // 设置柱子颜色
        barSet->setColor(colors[colorIndex % colors.size()]);
        colorIndex++;
    }
    
    barSeries->append(barSet);
    chart->addSeries(barSeries);
    
    // 创建或更新坐标轴
    if (!axisY) {
        writeToLog("创建新的Y轴（属性轴）");
        axisY = new QtCharts::QBarCategoryAxis();
        axisY->setTitleText("属性");
        
        // 设置坐标轴字体
        QFont axisFont("Arial", 10);
        axisY->setLabelsFont(axisFont);
        axisY->setTitleFont(axisFont);
        
        chart->addAxis(axisY, Qt::AlignLeft);
    } else {
        writeToLog("重用现有Y轴（属性轴）");
    }
    
    if (!axisX) {
        writeToLog("创建新的X轴（数值轴）");
        axisX = new QtCharts::QValueAxis();
        axisX->setTitleText("预测值");
        axisX->setLabelFormat("%.1f");
        
        // 设置坐标轴字体
        QFont axisFont("Arial", 10);
        axisX->setLabelsFont(axisFont);
        axisX->setTitleFont(axisFont);
        
        chart->addAxis(axisX, Qt::AlignBottom);
    } else {
        writeToLog("重用现有X轴（数值轴）");
    }
    
    // 更新Y轴类别（属性名称）
    axisY->clear();
    axisY->append(categories);
    
    // 计算X轴范围（根据数据动态调整）
    if (!results.isEmpty()) {
        double minValue = std::numeric_limits<double>::max();
        double maxValue = std::numeric_limits<double>::min();
        
        for (auto it = results.begin(); it != results.end(); ++it) {
            double value = it.value();
            minValue = qMin(minValue, value);
            maxValue = qMax(maxValue, value);
        }
        
        // 添加一些边距，确保数据点不会贴边
        double range = maxValue - minValue;
        double margin = range * 0.1;  // 10%的边距
        if (margin == 0) margin = 1.0;  // 如果所有值相同，添加固定边距
        
        double axisMin = minValue - margin;
        double axisMax = maxValue + margin;
        
        writeToLog(QString("X轴范围计算 - 数据范围:[%1,%2], 轴范围:[%3,%4]")
                  .arg(minValue, 0, 'f', 2).arg(maxValue, 0, 'f', 2).arg(axisMin, 0, 'f', 2).arg(axisMax, 0, 'f', 2));
        
        // 设置X轴范围
        axisX->setRange(axisMin, axisMax);
        writeToLog(QString("X轴范围已设置: [%1, %2]").arg(axisMin, 0, 'f', 2).arg(axisMax, 0, 'f', 2));
    }
    
    // 将系列附加到坐标轴（避免重复附加）
    if (!barSeries->attachedAxes().contains(axisY)) {
        barSeries->attachAxis(axisY);
        writeToLog("系列附加到Y轴");
    }
    if (!barSeries->attachedAxes().contains(axisX)) {
        barSeries->attachAxis(axisX);
        writeToLog("系列附加到X轴");
    }
    
    // 在柱状图顶部显示数值
    barSeries->setLabelsVisible(true);
    barSeries->setLabelsFormat("@value");
    barSeries->setLabelsPosition(QtCharts::QAbstractBarSeries::LabelsCenter);
    
    // 设置标签颜色为黑色（通过QBarSet）
    barSet->setLabelColor(QColor(0, 0, 0));  // 设置标签颜色为黑色
    
    writeToLog("实时预测柱状图更新完成");
}

/**
 * @brief 添加预测数据到历史记录
 * @param results 预测结果数据
 * @details 将新的预测结果添加到历史数据中
 */
void UpperComputerClient::addPredictionToHistory(const QMap<QString, float>& results)
{
    static int historyAddCount = 0;
    historyAddCount++;
    
    writeToLog(QString("开始添加预测数据到历史记录... (第 %1 次添加)").arg(historyAddCount));
    
    if (!predictionHistoryChart) {
        writeToLog("❌ 历史图表组件为空，无法添加数据");
        return;
    }
    
    writeToLog("✅ 历史图表组件检查通过");
    
    // 获取当前时间戳（使用微秒精度避免重复）
    QDateTime currentTime = QDateTime::currentDateTime();
    static qint64 lastTimestamp = 0;
    qint64 baseTimestamp = currentTime.toMSecsSinceEpoch();
    
    // 确保时间戳唯一性（如果相同或更小则递增1毫秒）
    if (baseTimestamp <= lastTimestamp) {
        baseTimestamp = lastTimestamp + 1;
    }
    lastTimestamp = baseTimestamp;
    
    // 直接使用毫秒时间戳确保唯一性
    double timestamp = baseTimestamp;
    
    writeToLog(QString("当前时间戳: %1 (毫秒), 对应时间: %2")
              .arg(timestamp)
              .arg(currentTime.toString("yyyy-MM-dd hh:mm:ss.zzz")));
    
    // 为每个属性添加数据点
    writeToLog(QString("添加预测数据到历史记录，共 %1 个属性").arg(results.size()));
    for (auto it = results.begin(); it != results.end(); ++it) {
        const QString& propertyName = it.key();
        float value = it.value();
        
        // 直接添加数据到历史记录（移除重复检测，每次预测都添加新数据）
        predictionHistoryData[propertyName].append(QPointF(timestamp, value));
        
        writeToLog(QString("添加属性 %1: 值=%2, 时间=%3, 历史数据点数量=%4")
                  .arg(propertyName)
                  .arg(value)
                  .arg(QDateTime::fromMSecsSinceEpoch(timestamp).toString("hh:mm:ss.zzz"))
                  .arg(predictionHistoryData[propertyName].size()));
        
        // 限制历史数据点数量
        if (predictionHistoryData[propertyName].size() > maxHistoryPoints) {
            predictionHistoryData[propertyName].removeFirst();
            writeToLog(QString("属性 %1 数据点数量超过限制，移除最旧的数据点").arg(propertyName));
        }
    }
    
    // 检查数据是否成功添加
    writeToLog(QString("数据添加完成，当前历史数据状态:"));
    writeToLog(QString("  - 属性数量: %1").arg(predictionHistoryData.size()));
    for (const auto& data : predictionHistoryData) {
        writeToLog(QString("  - 属性数据点数量: %1").arg(data.size()));
    }
    
    // 数据添加完成，图表更新由调用者负责
    writeToLog("✅ 历史数据添加完成");
    // 如果有打开的单属性弹窗，实时刷新
    for (auto it = propertyHistoryDialogs.begin(); it != propertyHistoryDialogs.end(); ++it) {
        refreshPropertyHistoryChart(it.key());
    }
}

/**
 * @brief 更新预测历史数据图表
 * @details 更新折线图显示预测历史数据
 */
void UpperComputerClient::updatePredictionHistoryChart()
{
    static int chartUpdateCount = 0;
    chartUpdateCount++;
    
    writeToLog(QString("开始更新预测历史数据图表... (第 %1 次更新)").arg(chartUpdateCount));
    
    try {
    
    if (!predictionHistoryChart) {
        writeToLog("❌ 历史图表组件为空，跳过更新");
        return;
    }
    
    if (predictionHistoryData.isEmpty()) {
        writeToLog("❌ 历史数据为空，跳过更新");
        writeToLog("当前 predictionHistoryData 状态:");
        writeToLog(QString("  - 数据容器大小: %1").arg(predictionHistoryData.size()));
        return;
    }
    
    writeToLog(QString("历史数据包含 %1 个属性").arg(predictionHistoryData.size()));
    
    // 统计总数据点数量
    int totalDataPoints = 0;
    for (const auto& data : predictionHistoryData) {
        totalDataPoints += data.size();
    }
    writeToLog(QString("历史数据总点数: %1").arg(totalDataPoints));
    
    QtCharts::QChart *chart = predictionHistoryChart->chart();
    if (!chart) {
        writeToLog("❌ 图表对象为空，跳过更新");
        return;
    }
    
    writeToLog("✅ 图表对象检查通过");
    
    // 智能系列管理（避免图例闪烁）
    writeToLog("开始智能系列管理...");
    
    // 获取现有系列
    QList<QtCharts::QAbstractSeries*> existingSeries = chart->series();
    QMap<QString, QtCharts::QLineSeries*> existingLineSeries;
    QMap<QString, QtCharts::QScatterSeries*> existingScatterSeries;
    
    // 分类现有系列
    for (QtCharts::QAbstractSeries* series : existingSeries) {
        if (QtCharts::QLineSeries* lineSeries = qobject_cast<QtCharts::QLineSeries*>(series)) {
            QString seriesName = lineSeries->name();
            if (!seriesName.isEmpty()) {
                existingLineSeries[seriesName] = lineSeries;
            }
        } else if (QtCharts::QScatterSeries* scatterSeries = qobject_cast<QtCharts::QScatterSeries*>(series)) {
            // 通过颜色匹配找到对应的折线系列
            for (auto it = existingLineSeries.begin(); it != existingLineSeries.end(); ++it) {
                if (it.value()->color() == scatterSeries->color()) {
                    existingScatterSeries[it.key()] = scatterSeries;
                    break;
                }
            }
        }
    }
    
    writeToLog(QString("找到现有折线系列: %1 个").arg(existingLineSeries.size()));
    writeToLog(QString("找到现有散点系列: %1 个").arg(existingScatterSeries.size()));
    
    // 检查是否已有坐标轴，如果没有则创建
    writeToLog("开始检查坐标轴...");
    QList<QtCharts::QAbstractAxis*> existingAxes = chart->axes();
    writeToLog(QString("现有坐标轴数量: %1").arg(existingAxes.size()));
    
    QtCharts::QDateTimeAxis *axisX = nullptr;
    QtCharts::QValueAxis *axisY = nullptr;
    
    // 查找现有坐标轴
    for (QtCharts::QAbstractAxis* axis : existingAxes) {
        if (QtCharts::QDateTimeAxis *dateAxis = qobject_cast<QtCharts::QDateTimeAxis*>(axis)) {
            axisX = dateAxis;
            writeToLog("找到现有时间轴");
        } else if (QtCharts::QValueAxis *valueAxis = qobject_cast<QtCharts::QValueAxis*>(axis)) {
            axisY = valueAxis;
            writeToLog("找到现有数值轴");
        }
    }
    
    // 如果没有坐标轴，则创建新的
    if (!axisX || !axisY) {
        writeToLog("创建新的坐标轴...");
        
        // 清除现有坐标轴
        for (QtCharts::QAbstractAxis* axis : existingAxes) {
            chart->removeAxis(axis);
            delete axis;
        }
        
        // 创建新的坐标轴
        axisX = new QtCharts::QDateTimeAxis();
        axisX->setTitleText("时间");
        axisX->setFormat("hh:mm:ss");
        axisX->setTickCount(6);
        axisX->setLabelsAngle(-45);
        axisX->setLabelsVisible(true);  // 确保标签可见
        axisX->setTitleVisible(true);   // 确保标题可见
        
        // 设置时间轴字体
        QFont timeAxisFont("Arial", 10);
        axisX->setLabelsFont(timeAxisFont);
        axisX->setTitleFont(timeAxisFont);
        
        // 设置网格线
        axisX->setGridLineVisible(true);
        axisX->setMinorGridLineVisible(true);
        
        // 设置默认时间范围（当前时间前后1小时）
        QDateTime now = QDateTime::currentDateTime();
        axisX->setRange(now.addSecs(-3600), now.addSecs(3600));
        writeToLog(QString("✅ 时间轴已设置默认范围: %1 到 %2")
                  .arg(now.addSecs(-3600).toString("hh:mm:ss"))
                  .arg(now.addSecs(3600).toString("hh:mm:ss")));
        
        axisY = new QtCharts::QValueAxis();
        axisY->setTitleText("预测值");
        axisY->setLabelFormat("%.1f");  // 显示一位小数
        axisY->setTickCount(11);
        axisY->setLabelsVisible(true);  // 确保标签可见
        axisY->setTitleVisible(true);   // 确保标题可见
        axisY->setRange(0, 1000);       // 设置默认范围
        
        // 设置坐标轴字体
        QFont axisFont("Arial", 10);
        axisY->setLabelsFont(axisFont);
        axisY->setTitleFont(axisFont);
        
        // 设置网格线
        axisY->setGridLineVisible(true);
        axisY->setMinorGridLineVisible(true);
        
        // 将坐标轴添加到图表
        chart->addAxis(axisX, Qt::AlignBottom);
        chart->addAxis(axisY, Qt::AlignLeft);
        
        writeToLog("✅ 数值轴已设置默认范围: 0-1000");
        
        writeToLog("✅ 新坐标轴创建完成");
    } else {
        writeToLog("使用现有坐标轴");
    }
    
    // 定义颜色列表（类似光谱图的颜色风格）
    QList<QColor> colors = {
        QColor(0, 123, 255),    // 蓝色（类似光谱图主色调）
        QColor(40, 167, 69),    // 绿色
        QColor(255, 193, 7),    // 黄色
        QColor(220, 53, 69),    // 红色
        QColor(111, 66, 193),   // 紫色
        QColor(253, 126, 20),   // 橙色
        QColor(108, 117, 125)   // 灰色
    };
    
    int colorIndex = 0;
    
    writeToLog("开始创建数据系列...");
    writeToLog(QString("需要处理的属性数量: %1").arg(predictionHistoryData.size()));
    
    // 为每个属性创建或更新折线系列和散点系列（避免图例闪烁）
    for (auto it = predictionHistoryData.begin(); it != predictionHistoryData.end(); ++it) {
        const QString& propertyName = it.key();
        const QVector<QPointF>& dataPoints = it.value();
        
        if (dataPoints.isEmpty()) {
            continue;
        }
        
        QtCharts::QLineSeries *lineSeries = nullptr;
        QtCharts::QScatterSeries *scatterSeries = nullptr;
        
        // 检查是否已存在该属性的系列
        if (existingLineSeries.contains(propertyName)) {
            // 使用现有系列，只更新数据
            lineSeries = existingLineSeries[propertyName];
            writeToLog(QString("✅ 重用现有折线系列: %1").arg(propertyName));
            
            // 查找对应的散点系列
            if (existingScatterSeries.contains(propertyName)) {
                scatterSeries = existingScatterSeries[propertyName];
                writeToLog(QString("✅ 重用现有散点系列: %1").arg(propertyName));
            }
        } else {
            // 创建新的系列
            writeToLog(QString("🆕 创建新系列: %1").arg(propertyName));
            
            // 创建折线系列（连接不同时间的数据点）
            lineSeries = new QtCharts::QLineSeries();
            lineSeries->setName(propertyName);
            lineSeries->setColor(colors[colorIndex % colors.size()]);
            
            // 创建散点系列（显示数据点）
            scatterSeries = new QtCharts::QScatterSeries();
            scatterSeries->setMarkerShape(QtCharts::QScatterSeries::MarkerShapeCircle);
            scatterSeries->setMarkerSize(6.0);
            scatterSeries->setColor(colors[colorIndex % colors.size()]);
            
            // 设置线条样式
            QPen pen(colors[colorIndex % colors.size()]);
            pen.setWidth(2);
            pen.setStyle(Qt::SolidLine);
            lineSeries->setPen(pen);
            
            // 添加系列到图表
            chart->addSeries(lineSeries);
            chart->addSeries(scatterSeries);
            
            colorIndex++;
        }
        
        // 清除现有数据并添加新数据点（按时间顺序排序）
        lineSeries->clear();
        if (scatterSeries) {
            scatterSeries->clear();
        }
        
        QVector<QPointF> sortedPoints = dataPoints;
        std::sort(sortedPoints.begin(), sortedPoints.end(), [](const QPointF& a, const QPointF& b) {
            return a.x() < b.x();
        });
        
        writeToLog(QString("属性 %1 有 %2 个数据点").arg(propertyName).arg(sortedPoints.size()));
        
        for (const QPointF& point : sortedPoints) {
            lineSeries->append(point);
            if (scatterSeries) {
                scatterSeries->append(point);
            }
        }
        
        writeToLog(QString("✅ 属性 %1 数据点更新完成，折线系列点数: %2, 散点系列点数: %3")
                  .arg(propertyName)
                  .arg(lineSeries->count())
                  .arg(scatterSeries ? scatterSeries->count() : 0));
        
        // 更新系列映射
        predictionHistorySeries[propertyName] = lineSeries;
    }
    
    // 确保所有系列都附加到坐标轴（避免重复附加）
    writeToLog("开始附加系列到坐标轴...");
    for (auto it = predictionHistorySeries.begin(); it != predictionHistorySeries.end(); ++it) {
        QtCharts::QLineSeries* lineSeries = it.value();
        if (lineSeries && !lineSeries->attachedAxes().contains(axisX)) {
            lineSeries->attachAxis(axisX);
            writeToLog(QString("🔗 折线系列 %1 附加到X轴").arg(it.key()));
        }
        if (lineSeries && !lineSeries->attachedAxes().contains(axisY)) {
            lineSeries->attachAxis(axisY);
            writeToLog(QString("🔗 折线系列 %1 附加到Y轴").arg(it.key()));
        }

        // 连接点击信号：点击该系列后绘制独立趋势图
        // 使用 Qt::UniqueConnection 防止重复连接
        QObject::connect(lineSeries, &QtCharts::QLineSeries::clicked,
                         this, &UpperComputerClient::onHistorySeriesClicked,
                         Qt::UniqueConnection);
    }

    // 为图例项添加点击交互：点击图例名称，打开该属性独立趋势图
    for (QtCharts::QLegendMarker* marker : chart->legend()->markers()) {
        QObject::connect(marker, &QtCharts::QLegendMarker::clicked, this, [this, marker]() {
            if (marker && marker->series()) {
                QString name;
                if (QtCharts::QLineSeries* s = qobject_cast<QtCharts::QLineSeries*>(marker->series())) {
                    name = s->name();
                }
                if (!name.isEmpty()) {
                    showPropertyHistoryChart(name);
                }
            }
        });
    }
    
    // 附加散点系列到坐标轴
    QList<QtCharts::QAbstractSeries*> allSeries = chart->series();
    for (QtCharts::QAbstractSeries* series : allSeries) {
        if (QtCharts::QScatterSeries* scatterSeries = qobject_cast<QtCharts::QScatterSeries*>(series)) {
            if (!scatterSeries->attachedAxes().contains(axisX)) {
                scatterSeries->attachAxis(axisX);
                writeToLog("🔗 散点系列附加到X轴");
            }
            if (!scatterSeries->attachedAxes().contains(axisY)) {
                scatterSeries->attachAxis(axisY);
                writeToLog("🔗 散点系列附加到Y轴");
            }
        }
    }
    
    writeToLog("✅ 系列附加到坐标轴完成");
    
    // 坐标轴已在上面创建或获取，这里只需要更新范围
    
    // 设置坐标轴范围
    if (!predictionHistoryData.isEmpty()) {
        double minTime = std::numeric_limits<double>::max();
        double maxTime = std::numeric_limits<double>::min();
        
        // 只计算时间范围，不计算数值范围
        for (const auto& data : predictionHistoryData) {
            if (data.isEmpty()) continue;
            
            for (const QPointF& point : data) {
                minTime = qMin(minTime, point.x());
                maxTime = qMax(maxTime, point.x());
            }
        }
        
        // 设置时间轴范围（只在范围变化时更新）
        if (minTime != maxTime) {
            // 为时间轴添加一些边距，使数据点不会贴边
            double timeRange = maxTime - minTime;
            double timeMargin = timeRange * 0.1;  // 10%的边距，确保数据点不会贴边
            QDateTime minDateTime = QDateTime::fromMSecsSinceEpoch(minTime - timeMargin);
            QDateTime maxDateTime = QDateTime::fromMSecsSinceEpoch(maxTime + timeMargin);
            
            writeToLog(QString("时间范围计算: minTime=%1, maxTime=%2, timeMargin=%3")
                      .arg(minTime)
                      .arg(maxTime)
                      .arg(timeMargin));
            writeToLog(QString("转换后的时间: %1 到 %2")
                      .arg(minDateTime.toString("hh:mm:ss.zzz"))
                      .arg(maxDateTime.toString("hh:mm:ss.zzz")));
            
            // 只在范围变化时更新
            if (axisX->min() != minDateTime || axisX->max() != maxDateTime) {
                axisX->setRange(minDateTime, maxDateTime);
                writeToLog(QString("历史图表时间轴范围更新: %1 到 %2").arg(minDateTime.toString("hh:mm:ss")).arg(maxDateTime.toString("hh:mm:ss")));
            }
        } else {
            // 如果只有一个时间点，设置一个合理的时间范围
            QDateTime singleTime = QDateTime::fromMSecsSinceEpoch(minTime);
            QDateTime minTime = singleTime.addSecs(-60);
            QDateTime maxTime = singleTime.addSecs(60);
            
            if (axisX->min() != minTime || axisX->max() != maxTime) {
                axisX->setRange(minTime, maxTime);
                writeToLog(QString("历史图表单点时间范围更新: %1").arg(singleTime.toString("hh:mm:ss")));
            }
        }
        
        // 检查数据范围并设置合适的数值轴范围
        double minValue = std::numeric_limits<double>::max();
        double maxValue = std::numeric_limits<double>::min();
        
        for (const auto& data : predictionHistoryData) {
            if (data.isEmpty()) continue;
            
            for (const QPointF& point : data) {
                minValue = qMin(minValue, point.y());
                maxValue = qMax(maxValue, point.y());
            }
        }
        
        writeToLog(QString("数据值范围: 最小值=%1, 最大值=%2").arg(minValue).arg(maxValue));
        
        // 检查是否有有效的数据值
        if (minValue == std::numeric_limits<double>::max() || maxValue == std::numeric_limits<double>::min()) {
            writeToLog("❌ 警告：没有找到有效的数据值！");
        }
        
        // 设置数值轴范围，确保包含所有数据点（只在范围变化时更新）
        if (minValue != maxValue) {
            double valueRange = maxValue - minValue;
            double valueMargin = valueRange * 0.1;  // 10%的边距
            double newMin = minValue - valueMargin;
            double newMax = maxValue + valueMargin;
            
            // 只在范围变化时更新
            if (axisY->min() != newMin || axisY->max() != newMax) {
                axisY->setRange(newMin, newMax);
                writeToLog(QString("历史图表数值轴范围更新: %1 到 %2").arg(newMin).arg(newMax));
                writeToLog(QString("数值轴标签格式: %1, 刻度数: %2").arg(axisY->labelFormat()).arg(axisY->tickCount()));
            }
        } else {
            // 如果所有值都相同，设置一个合理的范围
            double newMin = minValue - 1;
            double newMax = minValue + 1;
            
            if (axisY->min() != newMin || axisY->max() != newMax) {
                axisY->setRange(newMin, newMax);
                writeToLog(QString("历史图表数值轴范围更新: %1 到 %2").arg(newMin).arg(newMax));
            }
        }
    } else {
        // 没有数据时设置默认范围
        if (axisY->min() != 0 || axisY->max() != 1000) {
            axisY->setRange(0, 1000);
            writeToLog("历史图表数值轴范围默认设置为: 0-1000");
            writeToLog(QString("默认数值轴标签格式: %1, 刻度数: %2").arg(axisY->labelFormat()).arg(axisY->tickCount()));
        }
    }
    
    // 将系列附加到坐标轴（包括折线和散点系列）
    int lineSeriesCount = 0;
    int scatterSeriesCount = 0;
    
    writeToLog(QString("开始将系列附加到坐标轴，图表中共有 %1 个系列").arg(chart->series().size()));
    
    for (QtCharts::QAbstractSeries *series : chart->series()) {
        if (QtCharts::QLineSeries *lineSeries = qobject_cast<QtCharts::QLineSeries*>(series)) {
            if (!lineSeries->attachedAxes().contains(axisX)) {
                lineSeries->attachAxis(axisX);
            }
            if (!lineSeries->attachedAxes().contains(axisY)) {
                lineSeries->attachAxis(axisY);
            }
            lineSeriesCount++;
            writeToLog(QString("折线系列 %1 已附加到坐标轴，数据点数: %2").arg(lineSeries->name()).arg(lineSeries->count()));
        } else if (QtCharts::QScatterSeries *scatterSeries = qobject_cast<QtCharts::QScatterSeries*>(series)) {
            if (!scatterSeries->attachedAxes().contains(axisX)) {
                scatterSeries->attachAxis(axisX);
            }
            if (!scatterSeries->attachedAxes().contains(axisY)) {
                scatterSeries->attachAxis(axisY);
            }
            scatterSeriesCount++;
            writeToLog(QString("散点系列已附加到坐标轴，数据点数: %1").arg(scatterSeries->count()));
        }
    }
    
    writeToLog(QString("✅ 坐标轴附加完成，折线系列: %1, 散点系列: %2").arg(lineSeriesCount).arg(scatterSeriesCount));
    
    // 强制更新坐标轴标签
    if (axisX) {
        axisX->setLabelsVisible(true);
        axisX->setTitleVisible(true);
        writeToLog("✅ 时间轴标签已强制显示");
    }
    
    if (axisY) {
        axisY->setLabelsVisible(true);
        axisY->setTitleVisible(true);
        writeToLog(QString("✅ 数值轴标签已强制显示，范围: %1 到 %2").arg(axisY->min()).arg(axisY->max()));
    }
    
    // 立即刷新图表显示
    if (predictionHistoryChart) {
        predictionHistoryChart->update();
        predictionHistoryChart->repaint();
        writeToLog("✅ 图表已立即刷新");
    }
    
    writeToLog("✅ 历史数据图表更新完成");
    
    } catch (const std::exception& e) {
        writeToLog(QString("❌ 图表更新异常: %1").arg(e.what()));
    } catch (...) {
        writeToLog("❌ 图表更新发生未知异常");
    }
}

void UpperComputerClient::onHistorySeriesClicked(const QPointF &)
{
    // 根据发送者系列名确定属性名
    if (!sender()) return;
    if (QtCharts::QLineSeries* series = qobject_cast<QtCharts::QLineSeries*>(sender())) {
        const QString propertyName = series->name();
        if (!propertyName.isEmpty()) {
            showPropertyHistoryChart(propertyName);
        }
    }
}

void UpperComputerClient::showPropertyHistoryChart(const QString &propertyName)
{
    if (!predictionHistoryData.contains(propertyName)) {
        writeToLog(QString("未找到属性的历史数据: %1").arg(propertyName));
        return;
    }
    const QVector<QPointF> dataPoints = predictionHistoryData.value(propertyName);
    if (dataPoints.isEmpty()) {
        writeToLog(QString("属性 %1 历史数据为空").arg(propertyName));
        return;
    }

    // 如果已存在弹窗，则只刷新数据并置顶
    if (propertyHistoryDialogs.contains(propertyName)) {
        refreshPropertyHistoryChart(propertyName);
        QDialog *dlgExisting = propertyHistoryDialogs.value(propertyName);
        dlgExisting->raise();
        dlgExisting->activateWindow();
        return;
    }

    // 创建一个独立非模态窗口显示该属性的趋势图
    QDialog *dlg = new QDialog(nullptr);
    dlg->setWindowTitle(QString("%1 - 预测历史趋势图").arg(propertyName));
    dlg->resize(900, 500);
    dlg->setWindowFlags(Qt::Window | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint | Qt::CustomizeWindowHint);

    QVBoxLayout *layout = new QVBoxLayout(dlg);
    ZoomableChartView *chartView = new ZoomableChartView(dlg);
    chartView->setRenderHint(QPainter::Antialiasing);
    layout->addWidget(chartView);

    // 构建图表
    QtCharts::QChart *chart = new QtCharts::QChart();
    chart->setTitle(QString("%1 - 历史趋势").arg(propertyName));
    chart->legend()->hide();
    // 适当增加右侧边距，避免文本被裁剪
    chart->setMargins(QMargins(10, 10, 80, 10));

    QtCharts::QLineSeries *line = new QtCharts::QLineSeries(chart);
    line->setName(propertyName);
    for (const QPointF &p : dataPoints) line->append(p);
    chart->addSeries(line);

    // 时间轴与数值轴
    QtCharts::QDateTimeAxis *axisX = new QtCharts::QDateTimeAxis();
    axisX->setTitleText("时间");
    axisX->setFormat("hh:mm:ss");
    axisX->setTickCount(6);
    axisX->setLabelsAngle(-45);
    axisX->setLabelsVisible(true);
    axisX->setTitleVisible(true);

    QtCharts::QValueAxis *axisY = new QtCharts::QValueAxis();
    axisY->setTitleText("预测值");
    axisY->setLabelFormat("%.3f");

    chart->addAxis(axisX, Qt::AlignBottom);
    chart->addAxis(axisY, Qt::AlignLeft);
    line->attachAxis(axisX);
    line->attachAxis(axisY);

    // 设置时间范围
    double minTime = std::numeric_limits<double>::max();
    double maxTime = std::numeric_limits<double>::min();
    double minVal = std::numeric_limits<double>::max();
    double maxVal = std::numeric_limits<double>::min();
    for (const QPointF &p : dataPoints) {
        minTime = qMin(minTime, p.x()); maxTime = qMax(maxTime, p.x());
        minVal = qMin(minVal, p.y()); maxVal = qMax(maxVal, p.y());
    }
    // 将阈值纳入Y轴范围
    const QString keyRangeInit = normalizeKey(propertyName);
    if (thresholdRanges.contains(keyRangeInit)) {
        auto r = thresholdRanges.value(keyRangeInit);
        minVal = qMin(minVal, (double)r.first);
        maxVal = qMax(maxVal, (double)r.second);
    }
    // 加一点上下留白
    if (minVal < maxVal) {
        const double pad = (maxVal - minVal) * 0.1;
        minVal -= pad; maxVal += pad;
    }
    if (minTime < maxTime) axisX->setRange(QDateTime::fromMSecsSinceEpoch(minTime), QDateTime::fromMSecsSinceEpoch(maxTime));
    if (minVal < maxVal) axisY->setRange(minVal, maxVal);

    // 阈值上下限线（若存在阈值配置）
    const QString key = normalizeKey(propertyName);
    if (thresholdRanges.contains(key)) {
        auto range = thresholdRanges.value(key);
        // 下限线
        QtCharts::QLineSeries *minLine = new QtCharts::QLineSeries(chart);
        minLine->setName("下限");
        QPen penMin(Qt::DashLine); penMin.setColor(QColor(198, 40, 40)); penMin.setWidth(1);
        minLine->setPen(penMin);
        minLine->append(minTime, range.first);
        minLine->append(maxTime, range.first);
        chart->addSeries(minLine);
        // 上限线
        QtCharts::QLineSeries *maxLine = new QtCharts::QLineSeries(chart);
        maxLine->setName("上限");
        QPen penMax(Qt::DashLine); penMax.setColor(QColor(198, 40, 40)); penMax.setWidth(1);
        maxLine->setPen(penMax);
        maxLine->append(minTime, range.second);
        maxLine->append(maxTime, range.second);
        chart->addSeries(maxLine);
        // 轴附着
        minLine->attachAxis(axisX); minLine->attachAxis(axisY);
        maxLine->attachAxis(axisX); maxLine->attachAxis(axisY);
        // 保存引用
        propertyHistoryMinLines[propertyName] = minLine;
        propertyHistoryMaxLines[propertyName] = maxLine;

        // 使用QGraphicsSimpleTextItem在末端显示阈值文本
        auto *minText = new QGraphicsSimpleTextItem(chart);
        minText->setBrush(QBrush(QColor(198, 40, 40)));
        minText->setText(QString("下限:%1").arg(range.first, 0, 'f', 3));
        auto *maxText = new QGraphicsSimpleTextItem(chart);
        maxText->setBrush(QBrush(QColor(198, 40, 40)));
        maxText->setText(QString("上限:%1").arg(range.second, 0, 'f', 3));
        // 初始位置：根据plotArea进行边界保护
        const int rightMargin = 10;
        auto placeText = [&](QGraphicsSimpleTextItem* item, const QPointF& scene){
            QRectF plot = chart->plotArea();
            QRectF br = item->boundingRect();
            qreal x = scene.x() + rightMargin;
            qreal y = scene.y() - 10;
            if (x + br.width() > plot.right() - 2) x = plot.right() - br.width() - 2;
            if (x < plot.left() + 2) x = plot.left() + 2;
            if (y < plot.top() + 2) y = plot.top() + 2;
            if (y > plot.bottom() - br.height() - 2) y = plot.bottom() - br.height() - 2;
            item->setPos(x, y);
        };
        QPointF minScene = chart->mapToPosition(QPointF(maxTime, range.first));
        QPointF maxScene = chart->mapToPosition(QPointF(maxTime, range.second));
        placeText(minText, minScene);
        placeText(maxText, maxScene);
        propertyHistoryMinTexts[propertyName] = minText;
        propertyHistoryMaxTexts[propertyName] = maxText;
    }

    chartView->setChart(chart);

    // 添加导出按钮（可选）
    QPushButton *exportBtn = new QPushButton("导出CSV", dlg);
    QPushButton *resetZoomBtn = new QPushButton("重置缩放", dlg);
    QObject::connect(exportBtn, &QPushButton::clicked, this, [this, propertyName]() {
        QString def = QDir::home().filePath(QString("%1_history.csv").arg(propertyName));
        QString path = QFileDialog::getSaveFileName(nullptr, "保存CSV", def, "CSV Files (*.csv)");
        if (!path.isEmpty()) {
            exportPropertyHistoryToCsv(propertyName, path);
        }
    });
    QObject::connect(resetZoomBtn, &QPushButton::clicked, this, [chartView]() {
        if (auto chart = chartView->chart()) chart->zoomReset();
    });
    QHBoxLayout *btns = new QHBoxLayout();
    btns->addWidget(exportBtn);
    btns->addWidget(resetZoomBtn);
    btns->addStretch();
    layout->addLayout(btns);

    // 记录弹窗与系列以支持实时刷新
    propertyHistoryDialogs[propertyName] = dlg;
    propertyHistoryViews[propertyName] = chartView;
    propertyHistoryLines[propertyName] = line;

    dlg->setAttribute(Qt::WA_DeleteOnClose, true);
    QObject::connect(dlg, &QDialog::destroyed, this, [this, propertyName]() {
        propertyHistoryDialogs.remove(propertyName);
        propertyHistoryViews.remove(propertyName);
        propertyHistoryLines.remove(propertyName);
        if (propertyHistoryMinLines.contains(propertyName)) {
            propertyHistoryMinLines.remove(propertyName);
        }
        if (propertyHistoryMaxLines.contains(propertyName)) {
            propertyHistoryMaxLines.remove(propertyName);
        }
        if (propertyHistoryMinTexts.contains(propertyName)) {
            if (auto *t = propertyHistoryMinTexts[propertyName]) { delete t; }
            propertyHistoryMinTexts.remove(propertyName);
        }
        if (propertyHistoryMaxTexts.contains(propertyName)) {
            if (auto *t = propertyHistoryMaxTexts[propertyName]) { delete t; }
            propertyHistoryMaxTexts.remove(propertyName);
        }
    });

    dlg->setModal(false);
    dlg->show();
}

void UpperComputerClient::refreshPropertyHistoryChart(const QString &propertyName)
{
    if (!propertyHistoryDialogs.contains(propertyName)) return;
    if (!predictionHistoryData.contains(propertyName)) return;
    QtCharts::QChartView *view = propertyHistoryViews.value(propertyName, nullptr);
    QtCharts::QLineSeries *line = propertyHistoryLines.value(propertyName, nullptr);
    if (!view || !line) return;

    const QVector<QPointF> dataPoints = predictionHistoryData.value(propertyName);
    if (dataPoints.isEmpty()) return;

    // 更新线数据
    line->replace(dataPoints);

    // 同步坐标轴范围
    if (QtCharts::QChart *chart = view->chart()) {
        chart->setAnimationOptions(QtCharts::QChart::NoAnimation);
        QList<QtCharts::QAbstractAxis*> axes = chart->axes();
        QtCharts::QDateTimeAxis *axisX = nullptr; QtCharts::QValueAxis *axisY = nullptr;
        for (QtCharts::QAbstractAxis* axis : axes) {
            if (auto ax = qobject_cast<QtCharts::QDateTimeAxis*>(axis)) axisX = ax;
            if (auto ay = qobject_cast<QtCharts::QValueAxis*>(axis)) axisY = ay;
        }
        double minTime = std::numeric_limits<double>::max();
        double maxTime = std::numeric_limits<double>::min();
        double minVal = std::numeric_limits<double>::max();
        double maxVal = std::numeric_limits<double>::min();
        for (const QPointF &p : dataPoints) { minTime = qMin(minTime, p.x()); maxTime = qMax(maxTime, p.x()); minVal = qMin(minVal, p.y()); maxVal = qMax(maxVal, p.y()); }
        if (axisX && minTime < maxTime) axisX->setRange(QDateTime::fromMSecsSinceEpoch(minTime), QDateTime::fromMSecsSinceEpoch(maxTime));
        if (axisY && minVal < maxVal) {
            // 纳入阈值范围并留白
            const QString keyRange = normalizeKey(propertyName);
            if (thresholdRanges.contains(keyRange)) {
                auto r = thresholdRanges.value(keyRange);
                minVal = qMin(minVal, (double)r.first);
                maxVal = qMax(maxVal, (double)r.second);
            }
            const double pad = (maxVal - minVal) * 0.1;
            axisY->setRange(minVal - pad, maxVal + pad);
        }

        // 更新阈值线（若存在）
        const QString key = normalizeKey(propertyName);
        if (thresholdRanges.contains(key)) {
            auto range = thresholdRanges.value(key);
            auto *minLine = propertyHistoryMinLines.value(propertyName, nullptr);
            auto *maxLine = propertyHistoryMaxLines.value(propertyName, nullptr);
            if (minLine) {
                QVector<QPointF> pts; pts.reserve(2); pts.append(QPointF(minTime, range.first)); pts.append(QPointF(maxTime, range.first));
                minLine->replace(pts);
            }
            if (maxLine) {
                QVector<QPointF> pts; pts.reserve(2); pts.append(QPointF(minTime, range.second)); pts.append(QPointF(maxTime, range.second));
                maxLine->replace(pts);
            }
            // 移动与更新文本标注位置
            auto updateText = [&](QGraphicsSimpleTextItem* item, double val){
                if (!item) return;
                QPointF scene = chart->mapToPosition(QPointF(maxTime, val));
                QRectF plot = chart->plotArea();
                QRectF br = item->boundingRect();
                qreal x = scene.x() + 10;
                qreal y = scene.y() - 10;
                if (x + br.width() > plot.right() - 2) x = plot.right() - br.width() - 2;
                if (x < plot.left() + 2) x = plot.left() + 2;
                if (y < plot.top() + 2) y = plot.top() + 2;
                if (y > plot.bottom() - br.height() - 2) y = plot.bottom() - br.height() - 2;
                item->setPos(x, y);
            };
            if (auto *minText = propertyHistoryMinTexts.value(propertyName, nullptr)) {
                minText->setText(QString("下限:%1").arg(range.first, 0, 'f', 3));
                updateText(minText, range.first);
            }
            if (auto *maxText = propertyHistoryMaxTexts.value(propertyName, nullptr)) {
                maxText->setText(QString("上限:%1").arg(range.second, 0, 'f', 3));
                updateText(maxText, range.second);
            }
        }
    }
}

void UpperComputerClient::exportPropertyHistoryToCsv(const QString &propertyName, const QString &filePath)
{
    if (!predictionHistoryData.contains(propertyName)) return;
    const QVector<QPointF> dataPoints = predictionHistoryData.value(propertyName);
    if (dataPoints.isEmpty()) return;
    QString path = filePath;
    if (path.isEmpty()) {
        path = QDir::home().filePath(QString("%1_history.csv").arg(propertyName));
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    QTextStream ts(&f);
    ts << "timestamp_ms,value\n";
    for (const QPointF &p : dataPoints) {
        ts << QString::number((qint64)p.x()) << "," << QString::number(p.y(), 'f', 6) << "\n";
    }
    f.close();
    writeToLog(QString("已导出 %1 历史数据到: %2").arg(propertyName).arg(path));
}

/**
 * @brief 创建预测控制面板
 * @return 预测控制面板控件指针
 * @details 创建光谱预测相关的控制界面
 */
QWidget* UpperComputerClient::createPredictionPanel()
{
    QGroupBox* predictionGroup = new QGroupBox("光谱预测");
    QVBoxLayout* predictionLayout = new QVBoxLayout(predictionGroup);
    
    // 预测控制按钮
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    startPredictionBtn = new QPushButton("开始预测");
    stopPredictionBtn = new QPushButton("停止预测");
    stopPredictionBtn->setEnabled(false);
    
    buttonLayout->addWidget(startPredictionBtn);
    buttonLayout->addWidget(stopPredictionBtn);
    buttonLayout->addStretch();
    
    // 自动预测开关
    QHBoxLayout* autoPredictionLayout = new QHBoxLayout();
    QCheckBox* autoPredictionCheckBox = new QCheckBox("自动预测");
    autoPredictionCheckBox->setChecked(autoPredictionEnabled);
    autoPredictionCheckBox->setStyleSheet("QCheckBox { color: #333; font-weight: bold; }");
    autoPredictionLayout->addWidget(autoPredictionCheckBox);
    autoPredictionLayout->addStretch();
    
    // 连接自动预测开关信号
    connect(autoPredictionCheckBox, &QCheckBox::toggled, this, [this](bool enabled) {
        autoPredictionEnabled = enabled;
        writeToLog(QString("自动预测%1").arg(enabled ? "已启用" : "已禁用"));
    });
    
    // 预测状态标签
    predictionStatusLabel = new QLabel("就绪");
    predictionStatusLabel->setStyleSheet("QLabel { color: #666; font-weight: bold; }");
    
    // 预测结果表格
    predictionTable = new QTableWidget();
    predictionTable->setColumnCount(2);
    predictionTable->setHorizontalHeaderLabels(QStringList() << "属性" << "预测值");
    predictionTable->setAlternatingRowColors(true);
    predictionTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    predictionTable->verticalHeader()->setVisible(false);
    predictionTable->horizontalHeader()->setStretchLastSection(true);
    
    // 如果预测器已加载，初始化表格行数
    if (spectrumPredictor && spectrumPredictor->isModelLoaded()) {
        auto propertyLabels = spectrumPredictor->getPropertyLabels();
        if (!propertyLabels.empty()) {
            predictionTable->setRowCount(static_cast<int>(propertyLabels.size()));
            for (size_t i = 0; i < propertyLabels.size(); ++i) {
                predictionTable->setItem(static_cast<int>(i), 0, new QTableWidgetItem(QString::fromStdString(propertyLabels[i])));
                predictionTable->setItem(static_cast<int>(i), 1, new QTableWidgetItem("--"));
            }
        }
    }
    
    // 布局组装
    predictionLayout->addLayout(buttonLayout);
    predictionLayout->addLayout(autoPredictionLayout);
    predictionLayout->addWidget(predictionStatusLabel);
    predictionLayout->addWidget(predictionTable);
    
    // 连接信号槽
    connect(startPredictionBtn, &QPushButton::clicked, this, &UpperComputerClient::onStartPredictionClicked);
    connect(stopPredictionBtn, &QPushButton::clicked, this, &UpperComputerClient::onStopPredictionClicked);
    
    return predictionGroup;
}

// === 加密相关函数实现 ===

void UpperComputerClient::initializeEncryption()
{
    // 创建加密工具实例
    cryptoUtils = new CryptoUtils(this);
    encryptionEnabled = false;
    encryptionPassword = "spectrum_system_2024"; // 默认密码
    
    // 设置默认密钥
    QByteArray key = CryptoUtils::generateKeyFromPassword(encryptionPassword);
    if (cryptoUtils->setKey(key)) {
        // 加密系统初始化成功，稍后在日志中记录
        qDebug() << "🔐 加密系统初始化成功";
    } else {
        // 加密系统初始化失败，稍后在日志中记录
        qDebug() << "❌ 加密系统初始化失败";
    }
}

bool UpperComputerClient::setEncryption(bool enabled, const QString &password)
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

bool UpperComputerClient::isEncryptionEnabled() const
{
    return encryptionEnabled;
}

QString UpperComputerClient::getEncryptionStatus() const
{
    if (!encryptionEnabled) {
        return "加密未启用";
    }
    return QString("加密已启用 - %1").arg(cryptoUtils->getStatus());
}

QByteArray UpperComputerClient::encryptData(const QByteArray &data)
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

QByteArray UpperComputerClient::decryptData(const QByteArray &data)
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

void UpperComputerClient::sendStopStream()
{
    if (tcpSocket && tcpSocket->state() == QAbstractSocket::ConnectedState) {
        tcpSocket->write("STOP_SPECTRUM_STREAM\n");
        commandHistory->append(QString("[%1] 发送: %2").arg(QDateTime::currentDateTime().toString("hh:mm:ss")).arg("STOP_SPECTRUM_STREAM"));
        isPaused = true; isStreaming = false; refreshPropertyButtonsByState();
    }
}

void UpperComputerClient::applyPurpleStyleToPropertyButtons()
{
    static const char* STYLE_PURPLE = "QPushButton { background-color: #7E57C2; color: white; padding: 8px; border: none; border-radius: 4px; font-weight: bold; } QPushButton:hover { background-color: #5E35B1; } QPushButton:pressed { background-color: #4527A0; }";
    for (auto it = propertyButtons.begin(); it != propertyButtons.end(); ++it) {
        it.value()->setStyleSheet(STYLE_PURPLE);
    }
}

void UpperComputerClient::refreshQualityAnomalyCountInOpenDialogs()
{
    for (auto it = propertyStatsDialogs.begin(); it != propertyStatsDialogs.end(); ++it) {
        QDialog *dlg = it.value();
        if (dlg && dlg->isVisible()) {
            if (auto *q = dlg->findChild<QLabel*>("q_anom_val")) {
                q->setText(QString::number(spectrumQualityAnomalyCount));
            }
        }
    }
}

void UpperComputerClient::showPropertyStatsDialog(const QString &normalizedKey, const QString &displayName)
{
    QDialog *&dlg = propertyStatsDialogs[normalizedKey];
    if (!dlg) {
        dlg = new QDialog(this);
        dlg->setWindowTitle(displayName + " - 实时统计");
        dlg->resize(360, 150);
        QVBoxLayout *v = new QVBoxLayout(dlg);
        QLabel *lblTitle = new QLabel(displayName + " 的统计信息");
        lblTitle->setStyleSheet("QLabel { font-weight:bold; }");
        v->addWidget(lblTitle);
        QHBoxLayout *row1 = new QHBoxLayout();
        row1->addWidget(new QLabel("该物质异常次数:"));
        QLabel *anom = new QLabel(QString::number(anomalyCounts.value(normalizedKey, 0)));
        anom->setObjectName("anom_val");
        row1->addWidget(anom);
        row1->addStretch();
        v->addLayout(row1);
        QHBoxLayout *row2 = new QHBoxLayout();
        row2->addWidget(new QLabel("该物质检测总次数:"));
        QLabel *detc = new QLabel(QString::number(detectCounts.value(normalizedKey, 0)));
        detc->setObjectName("detc_val");
        row2->addWidget(detc);
        row2->addStretch();
        v->addLayout(row2);

        // 全局光谱质量异常计数
        QHBoxLayout *row3 = new QHBoxLayout();
        row3->addWidget(new QLabel("光谱质量异常累计次数:"));
        QLabel *qanom = new QLabel(QString::number(spectrumQualityAnomalyCount));
        qanom->setObjectName("q_anom_val");
        row3->addWidget(qanom);
        row3->addStretch();
        v->addLayout(row3);

        // 清除按钮区域
        QPushButton *clearHistBtn = new QPushButton("清除该物质异常统计");
        QPushButton *clearQAnomBtn = new QPushButton("清除光谱异常计数");
        v->addWidget(clearHistBtn);
        v->addWidget(clearQAnomBtn);
        QObject::connect(clearHistBtn, &QPushButton::clicked, this, [this, normalizedKey, displayName, dlg]() {
            anomalyCounts[normalizedKey] = 0; // 清空历史异常计数
            // 若当前不异常，则从橙色恢复为绿色
            if (!currentAbnormal.value(normalizedKey, false)) {
                refreshPropertyButton(normalizedKey);
            }
            if (auto *anom = dlg->findChild<QLabel*>("anom_val")) anom->setText(QString::number(0));
        });
        QObject::connect(clearQAnomBtn, &QPushButton::clicked, this, [this, dlg]() {
            spectrumQualityAnomalyCount = 0;
            if (auto *q = dlg->findChild<QLabel*>("q_anom_val")) q->setText(QString::number(0));
            refreshQualityAnomalyCountInOpenDialogs();
        });
    }
    // 每次打开前刷新数值
    if (auto *anom = propertyStatsDialogs[normalizedKey]->findChild<QLabel*>("anom_val")) {
        anom->setText(QString::number(anomalyCounts.value(normalizedKey, 0)));
    }
    if (auto *detc = propertyStatsDialogs[normalizedKey]->findChild<QLabel*>("detc_val")) {
        detc->setText(QString::number(detectCounts.value(normalizedKey, 0)));
    }
    if (auto *q = propertyStatsDialogs[normalizedKey]->findChild<QLabel*>("q_anom_val")) {
        q->setText(QString::number(spectrumQualityAnomalyCount));
    }
    propertyStatsDialogs[normalizedKey]->show();
    propertyStatsDialogs[normalizedKey]->raise();
    propertyStatsDialogs[normalizedKey]->activateWindow();
}

void UpperComputerClient::updateThresholdAlarms(const QMap<QString, float> &results)
{
    if (propertyButtons.isEmpty()) return;
    for (auto it = results.begin(); it != results.end(); ++it) {
        const QString key = normalizeKey(it.key());
        float val = it.value();
        auto range = thresholdRanges.value(key, qMakePair(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max()));
        bool ok = (val >= range.first && val <= range.second);
        // 检测总次数
        detectCounts[key] = detectCounts.value(key, 0) + 1;
        // 异常边沿：仅在从正常->异常时计数+1
        bool prevAbnormal = currentAbnormal.value(key, false);
        bool abnormalNow = !ok;
        if (abnormalNow && !prevAbnormal) {
            anomalyCounts[key] = anomalyCounts.value(key, 0) + 1;
        }
        currentAbnormal[key] = abnormalNow;
        refreshPropertyButton(key);
        if (propertyStatsDialogs.contains(key)) {
            if (auto *dlg = propertyStatsDialogs[key]) {
                if (dlg->isVisible()) {
                    if (auto *anom = dlg->findChild<QLabel*>("anom_val")) anom->setText(QString::number(anomalyCounts.value(key, 0)));
                    if (auto *detc = dlg->findChild<QLabel*>("detc_val")) detc->setText(QString::number(detectCounts.value(key, 0)));
                }
            }
        }
    }
}

// 注：预测完成回调在文件前部已有定义，这里只保留计数逻辑由前部实现接管

