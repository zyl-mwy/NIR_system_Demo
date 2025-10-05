#pragma once
#include <vector>

namespace predictor {
namespace basic {

// 使用提供的 mean 和 components 对特征进行PCA投影
// components 形状: n_components x n_features
std::vector<float> applyPcaProject(const std::vector<float>& features,
                                   const std::vector<float>& mean,
                                   const std::vector<std::vector<float>>& components);

} // namespace basic
} // namespace predictor


