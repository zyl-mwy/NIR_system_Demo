#pragma once
#include <vector>

namespace predictor {
namespace basic {

// 对单条光谱应用SNV标准化
std::vector<float> applySNV(const std::vector<float>& spectrum);

// 对特征进行标准化（使用均值和标准差）
std::vector<float> applyFeatureScaling(const std::vector<float>& features,
                                      const std::vector<float>& mean,
                                      const std::vector<float>& scale);

// 反标准化预测结果（从标准化空间恢复到原始空间）
std::vector<float> inverseTransformPredictions(const std::vector<float>& predictions,
                                              const std::vector<float>& mean,
                                              const std::vector<float>& scale);

} // namespace basic
} // namespace predictor


