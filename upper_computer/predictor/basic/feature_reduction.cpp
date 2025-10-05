#include "feature_reduction.h"

namespace predictor {
namespace basic {

std::vector<float> applyPcaProject(const std::vector<float>& features,
                                   const std::vector<float>& mean,
                                   const std::vector<std::vector<float>>& components)
{
    if (mean.empty() || components.empty()) return features;
    if (features.size() != mean.size()) return features;
    // x_centered = x - mean
    std::vector<float> centered(features.size());
    for (size_t i = 0; i < features.size(); ++i) centered[i] = features[i] - mean[i];

    const size_t n_components = components.size();
    const size_t expected_cols = centered.size();
    std::vector<float> projected(n_components, 0.0f);

    for (size_t r = 0; r < n_components; ++r) {
        const auto &row = components[r];
        if (row.size() != expected_cols) return features; // 维度不一致时直接返回原始特征
        float acc = 0.0f;
        for (size_t c = 0; c < expected_cols; ++c) acc += centered[c] * row[c];
        projected[r] = acc;
    }
    return projected;
}

} // namespace basic
} // namespace predictor


