#ifndef SVRLibTorchPredictor_H
#define SVRLibTorchPredictor_H

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <QVector>
#include <QStringList>

/**
 * @brief SVR LibTorch预测器包装类
 * @details 提供与ExampleLibTorchPredictor相同的接口，但内部使用SVR模型
 */
class SVRLibTorchPredictor : public QObject
{
    Q_OBJECT

public:
    explicit SVRLibTorchPredictor(QObject *parent = nullptr);
    ~SVRLibTorchPredictor();

    /**
     * @brief 初始化SVR预测器
     * @param modelPath 模型文件目录路径
     * @param modelInfoPath 模型信息文件路径
     * @param preprocessingParamsPath 预处理参数文件路径
     * @param device 计算设备 ("cpu" 或 "cuda")
     * @return 是否初始化成功
     */
    bool initialize(const QString &modelPath, const QString &modelInfoPath, 
                   const QString &preprocessingParamsPath, const QString &device = "cpu");

    /**
     * @brief 预测光谱属性
     * @param spectrumData 光谱数据向量
     * @return 预测结果JSON对象
     */
    QJsonObject predict(const QVector<double> &spectrumData);

    /**
     * @brief 检查模型是否已加载
     * @return 是否已加载
     */
    bool isModelLoaded() const;

    /**
     * @brief 获取属性标签
     * @return 属性标签列表
     */
    QStringList getPropertyLabels() const;

    /**
     * @brief 获取波长标签
     * @return 波长标签列表
     */
    QStringList getWavelengthLabels() const;

signals:
    /**
     * @brief 预测完成信号
     * @param result 预测结果
     */
    void predictionCompleted(const QJsonObject &result);

    /**
     * @brief 错误发生信号
     * @param errorMessage 错误信息
     */
    void errorOccurred(const QString &errorMessage);

private:
    class SVRSpectrumPredictor *m_svrPredictor;  // SVR预测器实例
};

#endif // SVRLibTorchPredictor_H
