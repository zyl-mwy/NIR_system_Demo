#pragma once
#include <QJsonArray>
#include <QJsonValue>
#include <QVector>
#include <vector>

namespace upper_computer {
namespace basic {

/**
 * @class DataConversionUtils
 * @brief 数据转换工具类
 * @details 提供各种数据格式之间的转换功能，避免重复代码
 */
class DataConversionUtils
{
public:
    /**
     * @brief 将QJsonArray转换为QVector<double>
     * @param jsonArray JSON数组
     * @return QVector<double>向量
     */
    static QVector<double> jsonArrayToQVectorDouble(const QJsonArray& jsonArray);
    
    /**
     * @brief 将QJsonArray转换为std::vector<float>
     * @param jsonArray JSON数组
     * @return std::vector<float>向量
     */
    static std::vector<float> jsonArrayToStdVectorFloat(const QJsonArray& jsonArray);
    
    /**
     * @brief 将QVector<double>转换为std::vector<float>
     * @param qvec QVector<double>向量
     * @return std::vector<float>向量
     */
    static std::vector<float> qVectorDoubleToStdVectorFloat(const QVector<double>& qvec);
    
    /**
     * @brief 将std::vector<float>转换为QVector<double>
     * @param stdvec std::vector<float>向量
     * @return QVector<double>向量
     */
    static QVector<double> stdVectorFloatToQVectorDouble(const std::vector<float>& stdvec);
    
    /**
     * @brief 将QVector<int>转换为std::vector<int>
     * @param qvec QVector<int>向量
     * @return std::vector<int>向量
     */
    static std::vector<int> qVectorIntToStdVectorInt(const QVector<int>& qvec);
    
    /**
     * @brief 将QVector<QVector<double>>转换为std::vector<std::vector<float>>
     * @param qvec2d 二维QVector
     * @return 二维std::vector
     */
    static std::vector<std::vector<float>> qVector2DToStdVector2D(const QVector<QVector<double>>& qvec2d);
};

} // namespace basic
} // namespace upper_computer
