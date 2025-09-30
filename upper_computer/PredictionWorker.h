/**
 * @file PredictionWorker.h
 * @brief 预测工作线程头文件
 * @details 定义预测工作线程类，用于在后台线程中执行光谱预测任务
 *          避免阻塞主UI线程，提高用户体验
 * @author 系统设计项目组
 * @date 2024
 */

#pragma once

// Qt核心模块
#include <QObject>    // Qt对象基类
#include <QMap>       // Qt映射容器
#include <QString>    // Qt字符串类

// 标准库
#include <vector>     // 标准向量容器

// 前向声明
class SpectrumPredictor;

/**
 * @class PredictionWorker
 * @brief 预测工作线程类
 * @details 继承自QObject，用于在后台线程中执行光谱预测任务
 *          通过信号槽机制与主线程通信，避免阻塞UI界面
 *          支持异步预测，提高系统响应性
 */
class PredictionWorker : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父对象指针
     * @details 初始化预测工作线程对象
     */
    explicit PredictionWorker(QObject *parent = nullptr);
    
    /**
     * @brief 析构函数
     * @details 清理资源，确保线程安全退出
     */
    ~PredictionWorker();

    // === 预测器管理接口 ===
    /**
     * @brief 设置光谱预测器
     * @param predictor 光谱预测器指针
     * @details 设置用于执行预测的预测器实例
     *          预测器必须在主线程中创建，但可以在工作线程中使用
     */
    void setPredictor(SpectrumPredictor* predictor);

public slots:
    // === 预测任务执行槽函数 ===
    /**
     * @brief 执行光谱预测任务
     * @param spectrum 输入光谱数据向量
     * @details 在后台线程中执行光谱预测，避免阻塞主UI线程
     *          预测完成后通过predictionCompleted信号返回结果
     *          如果预测失败，通过predictionError信号返回错误信息
     */
    void performPrediction(const std::vector<float>& spectrum);

signals:
    // === 预测结果信号 ===
    /**
     * @brief 预测完成信号
     * @param results 预测结果映射（属性名->预测值）
     * @details 当预测任务成功完成时发出此信号
     *          主线程可以连接此信号来接收预测结果
     */
    void predictionCompleted(const QMap<QString, float>& results);

    /**
     * @brief 预测错误信号
     * @param error 错误信息字符串
     * @details 当预测任务执行失败时发出此信号
     *          主线程可以连接此信号来处理错误情况
     */
    void predictionError(const QString& error);

private:
    // === 私有成员变量 ===
    /**
     * @brief 光谱预测器指针
     * @details 用于执行实际预测任务的预测器实例
     *          由主线程设置，在工作线程中使用
     */
    SpectrumPredictor* predictor_;
};

