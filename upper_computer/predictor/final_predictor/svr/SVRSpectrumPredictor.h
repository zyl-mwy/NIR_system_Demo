#ifndef SVRSpectrumPredictor_H
#define SVRSpectrumPredictor_H

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <QVector>
#include <QMap>
#include <memory>

/**
 * @brief SVR光谱预测器
 * @details 基于支持向量回归的光谱预测模型
 * 
 * 预测流程：
 * 1) SNV标准化：对光谱数据进行标准正态变量标准化
 * 2) VIP特征选择：根据训练时保存的特征索引选择重要波段
 * 3) PCA降维：将VIP后的特征降维到固定维度
 * 4) 特征标准化：对PCA后的特征进行标准化
 * 5) SVR预测：使用训练好的SVR模型进行预测
 * 6) 反标准化：将预测结果反标准化到原始尺度
 */
class SVRSpectrumPredictor : public QObject
{
    Q_OBJECT

public:
    explicit SVRSpectrumPredictor(QObject *parent = nullptr);
    ~SVRSpectrumPredictor();

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
    bool isInitialized() const;

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
    /**
     * @brief 加载模型信息
     * @param modelInfoPath 模型信息文件路径
     * @return 是否加载成功
     */
    bool loadModelInfo(const QString &modelInfoPath);

    /**
     * @brief 加载预处理参数
     * @param preprocessingParamsPath 预处理参数文件路径
     * @return 是否加载成功
     */
    bool loadPreprocessingParams(const QString &preprocessingParamsPath);

    /**
     * @brief 加载SVR模型
     * @param modelPath 模型文件路径
     * @return 是否加载成功
     */
    bool loadSVRModel(const QString &modelPath);


    /**
     * @brief 执行SVR预测
     * @param features 标准化后的特征
     * @return 预测结果
     */
    QVector<double> executeSVRPrediction(const QVector<double> &features);

    /**
     * @brief 类型转换辅助函数
     */
    std::vector<float> qVectorToStdVector(const QVector<double> &qvec);
    QVector<double> stdVectorToQVector(const std::vector<float> &stdvec);
    std::vector<int> qVectorToStdVectorInt(const QVector<int> &qvec);
    std::vector<std::vector<float>> qVector2DToStdVector2D(const QVector<QVector<double>> &qvec2d);

private:
    bool m_initialized;                    // 是否已初始化
    QString m_device;                      // 计算设备
    
    // 模型信息
    int m_inputSize;                       // 输入特征数量
    int m_outputSize;                      // 输出属性数量
    QStringList m_propertyLabels;          // 属性标签
    QStringList m_wavelengthLabels;        // 波长标签
    QVector<int> m_selectedFeatureIndices; // VIP选择的特征索引
    
    // 预处理参数
    QVector<double> m_spectrumMean;        // 光谱数据均值（用于验证）
    QVector<double> m_spectrumStd;         // 光谱数据标准差（用于验证）
    QVector<double> m_propertyMean;        // 属性数据均值
    QVector<double> m_propertyScale;       // 属性数据标准差
    
    // PCA参数
    bool m_hasPCA;                         // 是否使用PCA
    QVector<double> m_pcaMean;             // PCA均值
    QVector<QVector<double>> m_pcaComponents; // PCA主成分
    int m_pcaNComponents;                  // PCA成分数
    
    // 特征标准化参数
    QVector<double> m_featureMean;         // 特征均值
    QVector<double> m_featureScale;        // 特征标准差
    
    // SVR模型参数（简化版本，实际使用时需要集成libsvm或其他SVR库）
    struct SVRModel {
        double C;                          // 惩罚参数
        double gamma;                      // RBF核参数
        double epsilon;                    // 不敏感损失参数
        QVector<double> supportVectors;    // 支持向量
        QVector<double> dualCoefficients;  // 对偶系数
        double bias;                       // 偏置项
    };
    
    QVector<SVRModel> m_svrModels;         // SVR模型列表（每个属性一个）
};

#endif // SVRSpectrumPredictor_H
