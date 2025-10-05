#include "DataConversionUtils.h"

namespace upper_computer {
namespace basic {

QVector<double> DataConversionUtils::jsonArrayToQVectorDouble(const QJsonArray& jsonArray)
{
    QVector<double> result;
    result.reserve(jsonArray.size());
    
    for (const QJsonValue& value : jsonArray) {
        if (value.isDouble()) {
            result.append(value.toDouble());
        }
    }
    
    return result;
}

std::vector<float> DataConversionUtils::jsonArrayToStdVectorFloat(const QJsonArray& jsonArray)
{
    std::vector<float> result;
    result.reserve(jsonArray.size());
    
    for (const QJsonValue& value : jsonArray) {
        if (value.isDouble()) {
            result.push_back(static_cast<float>(value.toDouble()));
        }
    }
    
    return result;
}

std::vector<float> DataConversionUtils::qVectorDoubleToStdVectorFloat(const QVector<double>& qvec)
{
    std::vector<float> result;
    result.reserve(qvec.size());
    
    for (double value : qvec) {
        result.push_back(static_cast<float>(value));
    }
    
    return result;
}

QVector<double> DataConversionUtils::stdVectorFloatToQVectorDouble(const std::vector<float>& stdvec)
{
    QVector<double> result;
    result.reserve(stdvec.size());
    
    for (float value : stdvec) {
        result.append(static_cast<double>(value));
    }
    
    return result;
}

std::vector<int> DataConversionUtils::qVectorIntToStdVectorInt(const QVector<int>& qvec)
{
    return std::vector<int>(qvec.begin(), qvec.end());
}

std::vector<std::vector<float>> DataConversionUtils::qVector2DToStdVector2D(const QVector<QVector<double>>& qvec2d)
{
    std::vector<std::vector<float>> result;
    result.reserve(qvec2d.size());
    
    for (const QVector<double>& row : qvec2d) {
        std::vector<float> stdRow;
        stdRow.reserve(row.size());
        for (double value : row) {
            stdRow.push_back(static_cast<float>(value));
        }
        result.push_back(stdRow);
    }
    
    return result;
}

} // namespace basic
} // namespace upper_computer
