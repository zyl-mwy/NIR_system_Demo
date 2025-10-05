#include "SVRSpectrumPredictor.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QDir>
#include <QStandardPaths>
#include <QCoreApplication>
#include <cmath>
#include <algorithm>

// 包含basic模块的算法
#include "pre_processing.h"
#include "feature_selection.h"
#include "feature_reduction.h"

SVRSpectrumPredictor::SVRSpectrumPredictor(QObject *parent)
    : QObject(parent)
    , m_initialized(false)
    , m_device("cpu")
    , m_inputSize(0)
    , m_outputSize(0)
    , m_hasPCA(false)
    , m_pcaNComponents(0)
{
}

SVRSpectrumPredictor::~SVRSpectrumPredictor()
{
}

bool SVRSpectrumPredictor::initialize(const QString &modelPath, const QString &modelInfoPath, 
                                     const QString &preprocessingParamsPath, const QString &device)
{
    qDebug() << "正在初始化SVR光谱预测器...";
    
    m_device = device;
    m_initialized = false;
    
    // 加载模型信息
    if (!loadModelInfo(modelInfoPath)) {
        emit errorOccurred("加载模型信息失败");
        return false;
    }
    
    // 加载预处理参数
    if (!loadPreprocessingParams(preprocessingParamsPath)) {
        emit errorOccurred("加载预处理参数失败");
        return false;
    }
    
    // 加载SVR模型
    if (!loadSVRModel(modelPath)) {
        emit errorOccurred("加载SVR模型失败");
        return false;
    }
    
    m_initialized = true;
    qDebug() << "SVR光谱预测器初始化成功";
    qDebug() << "输入特征数:" << m_inputSize;
    qDebug() << "输出属性数:" << m_outputSize;
    qDebug() << "属性标签:" << m_propertyLabels;
    
    return true;
}

bool SVRSpectrumPredictor::loadModelInfo(const QString &modelInfoPath)
{
    QFile file(modelInfoPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "无法打开模型信息文件:" << modelInfoPath;
        return false;
    }
    
    QByteArray data = file.readAll();
    file.close();
    
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError) {
        qDebug() << "解析模型信息JSON失败:" << error.errorString();
        return false;
    }
    
    QJsonObject obj = doc.object();
    
    m_inputSize = obj["input_size"].toInt();
    m_outputSize = obj["output_size"].toInt();
    
    // 加载属性标签
    QJsonArray propertyArray = obj["property_labels"].toArray();
    m_propertyLabels.clear();
    for (const QJsonValue &value : propertyArray) {
        m_propertyLabels.append(value.toString());
    }
    
    // 加载波长标签
    QJsonArray wavelengthArray = obj["wavelength_labels"].toArray();
    m_wavelengthLabels.clear();
    for (const QJsonValue &value : wavelengthArray) {
        m_wavelengthLabels.append(value.toString());
    }
    
    // 加载VIP选择的特征索引
    QJsonArray featureArray = obj["selected_feature_indices"].toArray();
    m_selectedFeatureIndices.clear();
    for (const QJsonValue &value : featureArray) {
        m_selectedFeatureIndices.append(value.toInt());
    }
    
    qDebug() << "模型信息加载成功";
    qDebug() << "输入大小:" << m_inputSize;
    qDebug() << "输出大小:" << m_outputSize;
    qDebug() << "属性标签数量:" << m_propertyLabels.size();
    qDebug() << "波长标签数量:" << m_wavelengthLabels.size();
    qDebug() << "选择特征数量:" << m_selectedFeatureIndices.size();
    
    return true;
}

bool SVRSpectrumPredictor::loadPreprocessingParams(const QString &preprocessingParamsPath)
{
    QFile file(preprocessingParamsPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "无法打开预处理参数文件:" << preprocessingParamsPath;
        return false;
    }
    
    QByteArray data = file.readAll();
    file.close();
    
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError) {
        qDebug() << "解析预处理参数JSON失败:" << error.errorString();
        return false;
    }
    
    QJsonObject obj = doc.object();
    
    // 加载光谱统计信息
    QJsonObject spectrumStats = obj["spectrum_stats"].toObject();
    QJsonArray meanArray = spectrumStats["mean"].toArray();
    QJsonArray stdArray = spectrumStats["std"].toArray();
    
    m_spectrumMean.clear();
    m_spectrumStd.clear();
    for (const QJsonValue &value : meanArray) {
        m_spectrumMean.append(value.toDouble());
    }
    for (const QJsonValue &value : stdArray) {
        m_spectrumStd.append(value.toDouble());
    }
    
    // 加载属性标准化参数
    QJsonObject propertyScaler = obj["property_scaler"].toObject();
    QJsonArray propertyMeanArray = propertyScaler["mean"].toArray();
    QJsonArray propertyScaleArray = propertyScaler["scale"].toArray();
    
    m_propertyMean.clear();
    m_propertyScale.clear();
    for (const QJsonValue &value : propertyMeanArray) {
        m_propertyMean.append(value.toDouble());
    }
    for (const QJsonValue &value : propertyScaleArray) {
        m_propertyScale.append(value.toDouble());
    }
    
    // 加载PCA参数（如果存在）
    if (obj.contains("pca")) {
        QJsonObject pcaObj = obj["pca"].toObject();
        m_hasPCA = true;
        m_pcaNComponents = pcaObj["n_components"].toInt();
        
        QJsonArray pcaMeanArray = pcaObj["mean"].toArray();
        m_pcaMean.clear();
        for (const QJsonValue &value : pcaMeanArray) {
            m_pcaMean.append(value.toDouble());
        }
        
        QJsonArray componentsArray = pcaObj["components"].toArray();
        m_pcaComponents.clear();
        for (const QJsonValue &value : componentsArray) {
            QJsonArray componentArray = value.toArray();
            QVector<double> component;
            for (const QJsonValue &compValue : componentArray) {
                component.append(compValue.toDouble());
            }
            m_pcaComponents.append(component);
        }
    } else {
        m_hasPCA = false;
    }
    
    qDebug() << "预处理参数加载成功";
    qDebug() << "光谱统计信息大小:" << m_spectrumMean.size();
    qDebug() << "属性标准化参数大小:" << m_propertyMean.size();
    qDebug() << "是否使用PCA:" << m_hasPCA;
    if (m_hasPCA) {
        qDebug() << "PCA成分数:" << m_pcaNComponents;
    }
    
    return true;
}

bool SVRSpectrumPredictor::loadSVRModel(const QString &modelPath)
{
    // 注意：这里需要集成libsvm或其他SVR库来加载实际的模型
    // 目前使用简化的实现，实际项目中需要替换为真实的SVR模型加载
    
    qDebug() << "正在加载SVR模型（简化实现）...";
    
    // 为每个属性创建简化的SVR模型
    m_svrModels.clear();
    for (int i = 0; i < m_outputSize; ++i) {
        SVRModel model;
        model.C = 1.0;
        model.gamma = 0.1;
        model.epsilon = 0.1;
        model.bias = 0.0;
        // 在实际实现中，这里应该从.pkl文件加载真实的模型参数
        m_svrModels.append(model);
    }
    
    qDebug() << "SVR模型加载完成（简化实现）";
    qDebug() << "模型数量:" << m_svrModels.size();
    
    return true;
}

QJsonObject SVRSpectrumPredictor::predict(const QVector<double> &spectrumData)
{
    QJsonObject result;
    
    if (!m_initialized) {
        result["success"] = false;
        result["error"] = "预测器未初始化";
        emit errorOccurred("预测器未初始化");
        return result;
    }
    
    if (spectrumData.size() != m_wavelengthLabels.size()) {
        result["success"] = false;
        result["error"] = QString("光谱数据长度不匹配，期望%1，实际%2")
                         .arg(m_wavelengthLabels.size()).arg(spectrumData.size());
        emit errorOccurred(result["error"].toString());
        return result;
    }
    
    try {
        // 1. 应用SNV标准化
        std::vector<float> stdSpectrum = qVectorToStdVector(spectrumData);
        std::vector<float> snvSpectrum = predictor::basic::applySNV(stdSpectrum);
        
        // 2. 应用VIP特征选择
        std::vector<int> stdIndices = qVectorToStdVectorInt(m_selectedFeatureIndices);
        std::vector<float> selectedFeatures = predictor::basic::applyVipSelection(snvSpectrum, stdIndices);
        
        // 3. 应用PCA降维（如果使用）
        std::vector<float> pcaFeatures;
        if (m_hasPCA && selectedFeatures.size() == m_pcaMean.size()) {
            std::vector<float> stdPcaMean = qVectorToStdVector(m_pcaMean);
            std::vector<std::vector<float>> stdPcaComponents = qVector2DToStdVector2D(m_pcaComponents);
            pcaFeatures = predictor::basic::applyPcaProject(selectedFeatures, stdPcaMean, stdPcaComponents);
        } else {
            pcaFeatures = selectedFeatures;
        }
        
        // 4. 应用特征标准化
        std::vector<float> stdFeatureMean = qVectorToStdVector(m_featureMean);
        std::vector<float> stdFeatureScale = qVectorToStdVector(m_featureScale);
        std::vector<float> scaledFeatures = predictor::basic::applyFeatureScaling(pcaFeatures, stdFeatureMean, stdFeatureScale);
        
        // 5. 执行SVR预测
        QVector<double> qScaledFeatures = stdVectorToQVector(scaledFeatures);
        QVector<double> predictions = executeSVRPrediction(qScaledFeatures);
        
        // 6. 反标准化预测结果
        std::vector<float> stdPredictions = qVectorToStdVector(predictions);
        std::vector<float> stdPropertyMean = qVectorToStdVector(m_propertyMean);
        std::vector<float> stdPropertyScale = qVectorToStdVector(m_propertyScale);
        std::vector<float> finalPredictions = predictor::basic::inverseTransformPredictions(stdPredictions, stdPropertyMean, stdPropertyScale);
        
        // 构建结果
        result["success"] = true;
        QJsonArray predictionsArray;
        
        for (size_t i = 0; i < finalPredictions.size() && i < m_propertyLabels.size(); ++i) {
            QJsonObject prediction;
            prediction["property"] = m_propertyLabels[i];
            prediction["value"] = static_cast<double>(finalPredictions[i]);
            predictionsArray.append(prediction);
        }
        
        result["predictions"] = predictionsArray;
        
        qDebug() << "SVR预测完成，预测了" << finalPredictions.size() << "个属性";
        
    } catch (const std::exception &e) {
        result["success"] = false;
        result["error"] = QString("预测过程中发生错误: %1").arg(e.what());
        emit errorOccurred(result["error"].toString());
    }
    
    emit predictionCompleted(result);
    return result;
}

bool SVRSpectrumPredictor::isInitialized() const
{
    return m_initialized;
}

QStringList SVRSpectrumPredictor::getPropertyLabels() const
{
    return m_propertyLabels;
}

QStringList SVRSpectrumPredictor::getWavelengthLabels() const
{
    return m_wavelengthLabels;
}


QVector<double> SVRSpectrumPredictor::executeSVRPrediction(const QVector<double> &features)
{
    QVector<double> predictions(m_svrModels.size());
    
    // 简化的SVR预测实现
    // 在实际项目中，这里应该使用libsvm或其他SVR库
    for (int i = 0; i < m_svrModels.size(); ++i) {
        const SVRModel &model = m_svrModels[i];
        
        // 简化的线性预测（实际应该是RBF核）
        double prediction = model.bias;
        for (int j = 0; j < features.size(); ++j) {
            prediction += features[j] * 0.1; // 简化的权重
        }
        
        predictions[i] = prediction;
    }
    
    return predictions;
}

// 类型转换辅助函数实现
std::vector<float> SVRSpectrumPredictor::qVectorToStdVector(const QVector<double> &qvec)
{
    std::vector<float> result;
    result.reserve(qvec.size());
    for (double value : qvec) {
        result.push_back(static_cast<float>(value));
    }
    return result;
}

QVector<double> SVRSpectrumPredictor::stdVectorToQVector(const std::vector<float> &stdvec)
{
    QVector<double> result;
    result.reserve(stdvec.size());
    for (float value : stdvec) {
        result.append(static_cast<double>(value));
    }
    return result;
}

std::vector<int> SVRSpectrumPredictor::qVectorToStdVectorInt(const QVector<int> &qvec)
{
    return std::vector<int>(qvec.begin(), qvec.end());
}

std::vector<std::vector<float>> SVRSpectrumPredictor::qVector2DToStdVector2D(const QVector<QVector<double>> &qvec2d)
{
    std::vector<std::vector<float>> result;
    result.reserve(qvec2d.size());
    for (const QVector<double> &row : qvec2d) {
        std::vector<float> stdRow;
        stdRow.reserve(row.size());
        for (double value : row) {
            stdRow.push_back(static_cast<float>(value));
        }
        result.push_back(stdRow);
    }
    return result;
}
