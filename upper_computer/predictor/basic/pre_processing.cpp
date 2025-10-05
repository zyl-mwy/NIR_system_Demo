#include "pre_processing.h"
#include <numeric>
#include <cmath>

namespace predictor {
namespace basic {

std::vector<float> applySNV(const std::vector<float>& spectrum)
{
    std::vector<float> snv_spectrum(spectrum.size());
    if (spectrum.empty()) return snv_spectrum;

    float mean_val = std::accumulate(spectrum.begin(), spectrum.end(), 0.0f) / spectrum.size();
    float variance = 0.0f;
    for (float value : spectrum) {
        variance += (value - mean_val) * (value - mean_val);
    }
    float std_val = std::sqrt(variance / spectrum.size());

    if (std_val > 0.0f) {
        for (size_t i = 0; i < spectrum.size(); ++i) {
            snv_spectrum[i] = (spectrum[i] - mean_val) / std_val;
        }
    } else {
        for (size_t i = 0; i < spectrum.size(); ++i) {
            snv_spectrum[i] = spectrum[i] - mean_val;
        }
    }
    return snv_spectrum;
}

std::vector<float> applyFeatureScaling(const std::vector<float>& features,
                                      const std::vector<float>& mean,
                                      const std::vector<float>& scale)
{
    if (features.empty() || mean.empty() || scale.empty()) return features;
    if (features.size() != mean.size() || features.size() != scale.size()) return features;
    
    std::vector<float> scaled_features(features.size());
    for (size_t i = 0; i < features.size(); ++i) {
        if (std::abs(scale[i]) > 1e-8f) {
            scaled_features[i] = (features[i] - mean[i]) / scale[i];
        } else {
            scaled_features[i] = features[i] - mean[i];
        }
    }
    return scaled_features;
}

std::vector<float> inverseTransformPredictions(const std::vector<float>& predictions,
                                              const std::vector<float>& mean,
                                              const std::vector<float>& scale)
{
    if (predictions.empty() || mean.empty() || scale.empty()) return predictions;
    if (predictions.size() != mean.size() || predictions.size() != scale.size()) return predictions;
    
    std::vector<float> original_predictions(predictions.size());
    for (size_t i = 0; i < predictions.size(); ++i) {
        original_predictions[i] = predictions[i] * scale[i] + mean[i];
    }
    return original_predictions;
}

} // namespace basic
} // namespace predictor


