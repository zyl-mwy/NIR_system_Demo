#pragma once
#include <vector>

namespace predictor {
namespace basic {

// 按照索引列表提取特征波段
std::vector<float> applyVipSelection(const std::vector<float>& spectrum, const std::vector<int>& indices);

} // namespace basic
} // namespace predictor


