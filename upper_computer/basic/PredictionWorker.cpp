#include "PredictionWorker.h"
#include "ExampleSpectrumPredictor.h"
#include "SVRLibTorchPredictor.h"
#include "log.h"
#include <QDebug>
#include <QMap>
#include <QString>
#include <QThread>

PredictionWorker::PredictionWorker(QObject *parent)
    : QObject(parent), predictor_(nullptr), svrPredictor_(nullptr)
{
}

PredictionWorker::~PredictionWorker()
{
    // 清理资源
    if (predictor_) {
        predictor_ = nullptr;
    }
    if (svrPredictor_) {
        svrPredictor_ = nullptr;
    }
}

void PredictionWorker::setPredictor(ExampleSpectrumPredictor* predictor)
{
    predictor_ = predictor;
}

void PredictionWorker::setSVRPredictor(SVRLibTorchPredictor* predictor)
{
    svrPredictor_ = predictor;
}

void PredictionWorker::performPrediction(const std::vector<float>& spectrum)
{
    upper_computer::basic::LogManager::debug(QString("=== 光谱预测开始 ==="));
    upper_computer::basic::LogManager::debug(QString("预测线程ID:") + QString::number(reinterpret_cast<qulonglong>(QThread::currentThreadId())));
    upper_computer::basic::LogManager::debug(QString("光谱数据点数:") + QString::number(spectrum.size()));
    
    if (!predictor_) {
        upper_computer::basic::LogManager::debug(QString("❌ 预测器为空，无法执行预测"));
        emit predictionError("预测器未初始化");
        return;
    }

    try {
        upper_computer::basic::LogManager::debug(QString("🚀 开始执行LibTorch预测..."));
        
        // 显示光谱数据的前几个值用于调试
        if (spectrum.size() > 0) {
            QString spectrumPreview = "光谱数据预览: [";
            for (size_t i = 0; i < std::min(spectrum.size(), size_t(5)); ++i) {
                spectrumPreview += QString::number(spectrum[i], 'f', 3);
                if (i < std::min(spectrum.size(), size_t(5)) - 1) {
                    spectrumPreview += ", ";
                }
            }
            spectrumPreview += "...]";
            upper_computer::basic::LogManager::debug(spectrumPreview);
        }
        
        // 执行预测
        auto results = predictor_->predict(spectrum);
        
        upper_computer::basic::LogManager::debug(QString("✅ LibTorch预测执行完成"));
        upper_computer::basic::LogManager::debug(QString("📊 预测结果详情:"));
        
        // 转换为QMap格式并显示详细结果
        QMap<QString, float> qResults;
        for (const auto& pair : results) {
            qResults[QString::fromStdString(pair.first)] = pair.second;
            upper_computer::basic::LogManager::debug(QString("  %1: %2").arg(QString::fromStdString(pair.first)).arg(pair.second, 0, 'f', 4));
        }
        
        upper_computer::basic::LogManager::debug(QString("📈 预测结果统计:"));
        upper_computer::basic::LogManager::debug(QString("  - 结果数量:") + QString::number(qResults.size()));
        if (!qResults.isEmpty()) {
            float minVal = *std::min_element(qResults.begin(), qResults.end());
            float maxVal = *std::max_element(qResults.begin(), qResults.end());
            upper_computer::basic::LogManager::debug(QString("  - 最小值:") + QString::number(minVal));
            upper_computer::basic::LogManager::debug(QString("  - 最大值:") + QString::number(maxVal));
        }
        
        // 发出预测完成信号
        emit predictionCompleted(qResults);
        upper_computer::basic::LogManager::debug(QString("📡 预测完成信号已发出"));
        upper_computer::basic::LogManager::debug(QString("=== 光谱预测结束 ==="));
        
    } catch (const std::exception& e) {
        upper_computer::basic::LogManager::debug(QString("❌ 预测过程中发生异常:") + QString::fromStdString(e.what()));
        emit predictionError(QString("预测失败: %1").arg(e.what()));
        upper_computer::basic::LogManager::debug(QString("=== 光谱预测异常结束 ==="));
    }
}

void PredictionWorker::performSVRPrediction(const QVector<double>& spectrum)
{
    upper_computer::basic::LogManager::debug(QString("=== SVR光谱预测开始 ==="));
    upper_computer::basic::LogManager::debug(QString("预测线程ID:") + QString::number(reinterpret_cast<qulonglong>(QThread::currentThreadId())));
    upper_computer::basic::LogManager::debug(QString("光谱数据点数:") + QString::number(spectrum.size()));
    
    if (!svrPredictor_) {
        upper_computer::basic::LogManager::debug(QString("❌ SVR预测器为空，无法执行预测"));
        emit predictionError("SVR预测器未初始化");
        return;
    }

    try {
        upper_computer::basic::LogManager::debug(QString("🚀 开始执行SVR预测..."));
        
        // 显示光谱数据的前几个值用于调试
        if (spectrum.size() > 0) {
            QString spectrumPreview = "光谱数据预览: [";
            for (int i = 0; i < std::min(spectrum.size(), 5); ++i) {
                spectrumPreview += QString::number(spectrum[i], 'f', 3);
                if (i < std::min(spectrum.size(), 5) - 1) {
                    spectrumPreview += ", ";
                }
            }
            spectrumPreview += "...]";
            upper_computer::basic::LogManager::debug(spectrumPreview);
        }
        
        // 执行SVR预测
        QJsonObject predictionResult = svrPredictor_->predict(spectrum);
        
        if (!predictionResult["success"].toBool()) {
            upper_computer::basic::LogManager::debug(QString("❌ SVR预测失败"));
            emit predictionError("SVR预测失败");
            return;
        }
        
        upper_computer::basic::LogManager::debug(QString("✅ SVR预测执行完成"));
        upper_computer::basic::LogManager::debug(QString("📊 预测结果详情:"));
        
        // 转换为QMap格式并显示详细结果
        QMap<QString, float> qResults;
        QJsonArray predictions = predictionResult["predictions"].toArray();
        for (const QJsonValue &value : predictions) {
            QJsonObject prediction = value.toObject();
            QString property = prediction["property"].toString();
            float predValue = prediction["value"].toDouble();
            qResults[property] = predValue;
            upper_computer::basic::LogManager::debug(QString("  %1: %2").arg(property).arg(predValue, 0, 'f', 4));
        }
        
        upper_computer::basic::LogManager::debug(QString("📈 预测结果统计:"));
        upper_computer::basic::LogManager::debug(QString("  - 结果数量:") + QString::number(qResults.size()));
        if (!qResults.isEmpty()) {
            float minVal = *std::min_element(qResults.begin(), qResults.end());
            float maxVal = *std::max_element(qResults.begin(), qResults.end());
            upper_computer::basic::LogManager::debug(QString("  - 最小值:") + QString::number(minVal));
            upper_computer::basic::LogManager::debug(QString("  - 最大值:") + QString::number(maxVal));
        }
        
        // 发出预测完成信号
        emit predictionCompleted(qResults);
        upper_computer::basic::LogManager::debug(QString("📡 预测完成信号已发出"));
        upper_computer::basic::LogManager::debug(QString("=== SVR光谱预测结束 ==="));
        
    } catch (const std::exception& e) {
        upper_computer::basic::LogManager::debug(QString("❌ SVR预测过程中发生异常:") + QString::fromStdString(e.what()));
        emit predictionError(QString("SVR预测失败: %1").arg(e.what()));
        upper_computer::basic::LogManager::debug(QString("=== SVR光谱预测异常结束 ==="));
    }
}
