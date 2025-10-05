#include "feature_selection.h"

namespace predictor {
namespace basic {

std::vector<float> applyVipSelection(const std::vector<float>& spectrum, const std::vector<int>& indices)
{
    if (indices.empty()) return spectrum;
    std::vector<float> filtered;
    filtered.reserve(indices.size());
    for (int idx : indices) {
        if (idx >= 0 && static_cast<size_t>(idx) < spectrum.size()) {
            filtered.push_back(spectrum[static_cast<size_t>(idx)]);
        }
    }
    return filtered;
}

} // namespace basic
} // namespace predictor


