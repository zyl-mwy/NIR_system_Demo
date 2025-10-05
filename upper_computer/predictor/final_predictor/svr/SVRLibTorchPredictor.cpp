#include "SVRLibTorchPredictor.h"
#include "SVRSpectrumPredictor.h"
#include <QDebug>

SVRLibTorchPredictor::SVRLibTorchPredictor(QObject *parent)
    : QObject(parent)
    , m_svrPredictor(nullptr)
{
    m_svrPredictor = new SVRSpectrumPredictor(this);
    
    // 连接信号
    connect(m_svrPredictor, &SVRSpectrumPredictor::predictionCompleted,
            this, &SVRLibTorchPredictor::predictionCompleted);
    connect(m_svrPredictor, &SVRSpectrumPredictor::errorOccurred,
            this, &SVRLibTorchPredictor::errorOccurred);
}

SVRLibTorchPredictor::~SVRLibTorchPredictor()
{
    if (m_svrPredictor) {
        delete m_svrPredictor;
        m_svrPredictor = nullptr;
    }
}

bool SVRLibTorchPredictor::initialize(const QString &modelPath, const QString &modelInfoPath, 
                                     const QString &preprocessingParamsPath, const QString &device)
{
    if (!m_svrPredictor) {
        qDebug() << "SVR预测器未创建";
        return false;
    }
    
    return m_svrPredictor->initialize(modelPath, modelInfoPath, preprocessingParamsPath, device);
}

QJsonObject SVRLibTorchPredictor::predict(const QVector<double> &spectrumData)
{
    if (!m_svrPredictor) {
        QJsonObject result;
        result["success"] = false;
        result["error"] = "SVR预测器未初始化";
        return result;
    }
    
    return m_svrPredictor->predict(spectrumData);
}

bool SVRLibTorchPredictor::isModelLoaded() const
{
    return m_svrPredictor ? m_svrPredictor->isInitialized() : false;
}

QStringList SVRLibTorchPredictor::getPropertyLabels() const
{
    return m_svrPredictor ? m_svrPredictor->getPropertyLabels() : QStringList();
}

QStringList SVRLibTorchPredictor::getWavelengthLabels() const
{
    return m_svrPredictor ? m_svrPredictor->getWavelengthLabels() : QStringList();
}
