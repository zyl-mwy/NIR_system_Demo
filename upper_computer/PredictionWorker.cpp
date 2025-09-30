#include "PredictionWorker.h"
#include "SpectrumPredictor.h"
#include <QDebug>
#include <QMap>
#include <QString>
#include <QThread>

PredictionWorker::PredictionWorker(QObject *parent)
    : QObject(parent), predictor_(nullptr)
{
}

PredictionWorker::~PredictionWorker()
{
    // 清理资源
    if (predictor_) {
        predictor_ = nullptr;
    }
}

void PredictionWorker::setPredictor(SpectrumPredictor* predictor)
{
    predictor_ = predictor;
}

void PredictionWorker::performPrediction(const std::vector<float>& spectrum)
{
    qDebug() << "=== 光谱预测开始 ===";
    qDebug() << "预测线程ID:" << QThread::currentThreadId();
    qDebug() << "光谱数据点数:" << spectrum.size();
    
    if (!predictor_) {
        qDebug() << "❌ 预测器为空，无法执行预测";
        emit predictionError("预测器未初始化");
        return;
    }

    try {
        qDebug() << "🚀 开始执行LibTorch预测...";
        
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
            qDebug() << spectrumPreview;
        }
        
        // 执行预测
        auto results = predictor_->predict(spectrum);
        
        qDebug() << "✅ LibTorch预测执行完成";
        qDebug() << "📊 预测结果详情:";
        
        // 转换为QMap格式并显示详细结果
        QMap<QString, float> qResults;
        for (const auto& pair : results) {
            qResults[QString::fromStdString(pair.first)] = pair.second;
            qDebug() << QString("  %1: %2").arg(QString::fromStdString(pair.first)).arg(pair.second, 0, 'f', 4);
        }
        
        qDebug() << "📈 预测结果统计:";
        qDebug() << "  - 结果数量:" << qResults.size();
        if (!qResults.isEmpty()) {
            float minVal = *std::min_element(qResults.begin(), qResults.end());
            float maxVal = *std::max_element(qResults.begin(), qResults.end());
            qDebug() << "  - 最小值:" << minVal;
            qDebug() << "  - 最大值:" << maxVal;
        }
        
        // 发出预测完成信号
        emit predictionCompleted(qResults);
        qDebug() << "📡 预测完成信号已发出";
        qDebug() << "=== 光谱预测结束 ===";
        
    } catch (const std::exception& e) {
        qDebug() << "❌ 预测过程中发生异常:" << e.what();
        emit predictionError(QString("预测失败: %1").arg(e.what()));
        qDebug() << "=== 光谱预测异常结束 ===";
    }
}
