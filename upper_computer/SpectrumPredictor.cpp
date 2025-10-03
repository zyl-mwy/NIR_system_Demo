/**
 * @file SpectrumPredictor.cpp
 * @brief 光谱预测类实现文件 - 基于LibTorch
 * @details 实现光谱数据预测功能，包括SNV标准化和模型推理
 * @author 系统设计项目组
 * @date 2024
 */

#include "SpectrumPredictor.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <numeric>
#include <cmath>
// Qt JSON 解析
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

SpectrumPredictor::SpectrumPredictor(const std::string& model_path, 
                                     const std::string& model_info_path,
                                     const std::string& preprocessing_params_path,
                                     const std::string& device)
    : device_(device), model_loaded_(false), input_size_(0), output_size_(0), 
      preprocessing_loaded_(false), log_callback_(nullptr)
{
    try {
        // 创建LibTorch预测器
        libtorch_predictor_ = std::make_unique<LibTorchPredictor>(model_path, model_info_path, device);
        
        // 设置LibTorch预测器的日志回调
        if (libtorch_predictor_) {
            libtorch_predictor_->setLogCallback([this](const std::string& message) {
                logMessage(message);
            });
        }
        
        if (libtorch_predictor_ && libtorch_predictor_->isModelLoaded()) {
            model_loaded_ = true;
            input_size_ = libtorch_predictor_->getInputSize();
            output_size_ = libtorch_predictor_->getOutputSize();
            property_labels_ = libtorch_predictor_->getPropertyLabels();
            
            // 解析模型信息以加载selected_feature_indices
            try {
                loadModelInfo(model_info_path);
            } catch (const std::exception& e) {
                logMessage(std::string("加载模型信息失败: ") + e.what());
            }

            // 加载预处理参数
            loadPreprocessingParams(preprocessing_params_path);
            
            logMessage("光谱预测模型加载成功（使用LibTorch）");
            logMessage("设备: " + device_);
            logMessage("输入特征数: " + std::to_string(input_size_));
            logMessage("输出属性数: " + std::to_string(output_size_));
            logMessage(std::string("预处理参数加载: ") + (preprocessing_loaded_ ? "成功" : "失败"));
        } else {
            model_loaded_ = false;
            logMessage("LibTorch预测器加载失败");
        }
        
    } catch (const std::exception& e) {
        logMessage("模型加载失败: " + std::string(e.what()));
        model_loaded_ = false;
    }
}

SpectrumPredictor::~SpectrumPredictor()
{
    // 析构函数，LibTorch会自动清理资源
}

std::map<std::string, float> SpectrumPredictor::predict(const std::vector<float>& spectrum)
{
    std::map<std::string, float> results;
    
    if (!model_loaded_) {
        std::cerr << "模型未加载，无法进行预测" << std::endl;
        return results;
    }
    
    // 尺寸检查延后到预处理/特征选择之后
    
    try {
        // 应用SNV预处理
        std::vector<float> preprocessed_spectrum = applySNV(spectrum);
        // 应用VIP特征筛选（若有索引）
        if (!selected_feature_indices_.empty()) {
            size_t max_idx = static_cast<size_t>(*std::max_element(selected_feature_indices_.begin(), selected_feature_indices_.end()));
            if (spectrum.size() <= max_idx) {
                std::cerr << "输入光谱长度不足以进行特征筛选，长度: " << spectrum.size() << ", 最大索引: " << max_idx << std::endl;
                return results;
            }
            preprocessed_spectrum = applyFeatureSelection(preprocessed_spectrum);
        }
        
        // 使用LibTorch预测器进行预测（若加载了PCA，则先做投影）
        // 处理顺序与训练侧一致：SNV -> VIP筛选 -> PCA投影 -> 推理 -> 输出反标准化
        if (pca_loaded_) {
            // 以训练侧PCA的mean长度作为期望特征数
            const size_t expected_features = pca_mean_.size();
            if (expected_features > 0 && preprocessed_spectrum.size() != expected_features) {
                if (preprocessed_spectrum.size() > expected_features) {
                    // 截断到期望维度
                    std::vector<float> trimmed(expected_features);
                    std::copy(preprocessed_spectrum.begin(), preprocessed_spectrum.begin() + expected_features, trimmed.begin());
                    preprocessed_spectrum.swap(trimmed);
                } else {
                    std::cerr << "VIP后特征数(" << preprocessed_spectrum.size() << ") 小于PCA期望特征数(" << expected_features << ")，跳过PCA投影" << std::endl;
                }
            }
            if (pca_mean_.size() == preprocessed_spectrum.size() && !pca_components_.empty()) {
                // x_centered = x - mean
                std::vector<float> centered(preprocessed_spectrum.size());
                for (size_t i = 0; i < preprocessed_spectrum.size(); ++i) centered[i] = preprocessed_spectrum[i] - pca_mean_[i];
                // proj = centered dot components^T  => size = n_components
                const size_t n_components = pca_components_.size();
                std::vector<float> projected(n_components, 0.0f);
                // 保护：确保每一行主成分长度与特征数一致，否则跳过PCA（避免错误地投影到1维）
                const size_t expected_cols = centered.size();
                bool row_len_ok = true;
                for (const auto &row : pca_components_) { if (row.size() != expected_cols) { row_len_ok = false; break; } }
                if (!row_len_ok) {
                    std::cerr << "PCA主成分列数与特征数不一致，跳过PCA投影: row_len=" << (pca_components_.empty()?0:pca_components_[0].size()) << ", features=" << expected_cols << std::endl;
                } else {
                    for (size_t r = 0; r < n_components; ++r) {
                        const auto &row = pca_components_[r];
                        float acc = 0.0f;
                        for (size_t c = 0; c < expected_cols; ++c) acc += centered[c] * row[c];
                        projected[r] = acc;
                    }
                    // 替换为PCA后的特征
                    preprocessed_spectrum.swap(projected);
                }
            } else {
                std::cerr << "PCA维度不匹配，跳过PCA投影" << std::endl;
            }
        }
        // 最终尺寸应匹配模型输入（在VIP与PCA之后）
        if (preprocessed_spectrum.size() != static_cast<size_t>(input_size_)) {
            std::cerr << "特征处理后维度与模型输入不一致: " << preprocessed_spectrum.size() << " vs " << input_size_ << std::endl;
            return results;
        }
        results = libtorch_predictor_->predict(preprocessed_spectrum);
        // 反标准化属性数据
        if (preprocessing_loaded_) {
            // 调试：打印标准化输出预览
            logMessage("[调试] 已加载预处理参数，开始反标准化预测结果");
            std::vector<float> scaled_values;
            for (const auto& label : property_labels_) {
                auto it = results.find(label);
                if (it != results.end()) {
                    scaled_values.push_back(it->second);
                } else {
                    scaled_values.push_back(0.0f);
                }
            }
            
            std::vector<float> original_values = inverseScaleProperties(scaled_values);
            

            // 更新结果
            for (size_t i = 0; i < property_labels_.size() && i < original_values.size(); ++i) {
                results[property_labels_[i]] = original_values[i];
            }

            // 调试：打印反标准化前后前若干维度对照
            if (!property_labels_.empty()) {
                size_t show_n = std::min<size_t>(property_labels_.size(), 3);
                for (size_t i = 0; i < show_n; ++i) {
                    logMessage("[调试] " + property_labels_[i] +
                               " 标准化: " + std::to_string(scaled_values[i]) +
                               ", 反标准化: " + std::to_string(original_values[i]));
                }
            }
        }
        
    } catch (const std::exception& e) {
        std::cerr << "预测过程中出现错误: " << e.what() << std::endl;
    }
    
    return results;
}

std::map<std::string, std::vector<float>> SpectrumPredictor::predictBatch(
    const std::vector<std::vector<float>>& spectra)
{
    std::map<std::string, std::vector<float>> results;
    
    if (!model_loaded_) {
        std::cerr << "模型未加载，无法进行预测" << std::endl;
        return results;
    }
    
    if (spectra.empty()) {
        return results;
    }
    
    try {
        // 初始化结果结构
        for (const auto& label : property_labels_) {
            results[label] = std::vector<float>();
        }
        
        // 逐个预测每个光谱
        for (const auto& spectrum : spectra) {
            if (spectrum.size() != static_cast<size_t>(input_size_)) {
                std::cerr << "光谱数据长度不匹配，跳过该样本" << std::endl;
                continue;
            }
            
            // 应用SNV预处理
            std::vector<float> preprocessed_spectrum = applySNV(spectrum);
            if (!selected_feature_indices_.empty()) {
                size_t max_idx = static_cast<size_t>(*std::max_element(selected_feature_indices_.begin(), selected_feature_indices_.end()));
                if (spectrum.size() <= max_idx) {
                    std::cerr << "输入光谱长度不足以进行特征筛选(批量)，长度: " << spectrum.size() << ", 最大索引: " << max_idx << std::endl;
                    continue;
                }
                preprocessed_spectrum = applyFeatureSelection(preprocessed_spectrum);
            }
            
            // 若加载了PCA，则先做投影
            if (pca_loaded_) {
                const size_t expected_features = pca_mean_.size();
                if (expected_features > 0 && preprocessed_spectrum.size() != expected_features) {
                    if (preprocessed_spectrum.size() > expected_features) {
                        std::vector<float> trimmed(expected_features);
                        std::copy(preprocessed_spectrum.begin(), preprocessed_spectrum.begin() + expected_features, trimmed.begin());
                        preprocessed_spectrum.swap(trimmed);
                    } else {
                        std::cerr << "VIP后特征数(" << preprocessed_spectrum.size() << ") 小于PCA期望特征数(" << expected_features << ")，跳过PCA投影(批量)" << std::endl;
                    }
                }
                if (pca_mean_.size() == preprocessed_spectrum.size() && !pca_components_.empty()) {
                    std::vector<float> centered(preprocessed_spectrum.size());
                    for (size_t i = 0; i < preprocessed_spectrum.size(); ++i) centered[i] = preprocessed_spectrum[i] - pca_mean_[i];
                    const size_t n_components = pca_components_.size();
                    const size_t expected_cols = centered.size();
                    bool row_len_ok = true;
                    for (const auto &row : pca_components_) { if (row.size() != expected_cols) { row_len_ok = false; break; } }
                    if (!row_len_ok) {
                        std::cerr << "PCA主成分列数与特征数不一致(批量)，跳过PCA投影: row_len=" << (pca_components_.empty()?0:pca_components_[0].size()) << ", features=" << expected_cols << std::endl;
                    } else {
                        std::vector<float> projected(n_components, 0.0f);
                        for (size_t r = 0; r < n_components; ++r) {
                            const auto &row = pca_components_[r];
                            float acc = 0.0f;
                            for (size_t c = 0; c < expected_cols; ++c) acc += centered[c] * row[c];
                            projected[r] = acc;
                        }
                        preprocessed_spectrum.swap(projected);
                    }
                } else {
                    std::cerr << "PCA维度不匹配(批量)，跳过PCA投影" << std::endl;
                }
            }
            if (preprocessed_spectrum.size() != static_cast<size_t>(input_size_)) {
                std::cerr << "特征处理后维度与模型输入不一致(批量): " << preprocessed_spectrum.size() << " vs " << input_size_ << std::endl;
                continue;
            }
            auto prediction = libtorch_predictor_->predict(preprocessed_spectrum);
            
            // 反标准化属性数据
            if (preprocessing_loaded_) {
                std::vector<float> scaled_values;
                for (const auto& label : property_labels_) {
                    auto it = prediction.find(label);
                    if (it != prediction.end()) {
                        scaled_values.push_back(it->second);
                    } else {
                        scaled_values.push_back(0.0f);
                    }
                }
                
                std::vector<float> original_values = inverseScaleProperties(scaled_values);

                // 终端打印：批量首样本或少量样本的对照（这里打印每个样本一行）
                std::cerr << "[终端] 批量-标准化输出: [";
                for (size_t i = 0; i < scaled_values.size(); ++i) {
                    std::cerr << scaled_values[i];
                    if (i + 1 < scaled_values.size()) std::cerr << ", ";
                }
                std::cerr << "]" << std::endl;
                std::cerr << "[终端] 批量-反标准化输出: [";
                for (size_t i = 0; i < original_values.size(); ++i) {
                    std::cerr << original_values[i];
                    if (i + 1 < original_values.size()) std::cerr << ", ";
                }
                std::cerr << "]" << std::endl;
                
                // 将反标准化后的结果添加到批量结果中
                for (size_t i = 0; i < property_labels_.size() && i < original_values.size(); ++i) {
                    results[property_labels_[i]].push_back(original_values[i]);
                }
            } else {
                // 调试：未加载到预处理参数
                logMessage("[调试] 预处理参数未加载，批量预测直接返回标准化输出");
                
                // 如果没有预处理参数，直接使用原始预测结果
                for (const auto& label : property_labels_) {
                    auto it = prediction.find(label);
                    if (it != prediction.end()) {
                        results[label].push_back(it->second);
                    } else {
                        results[label].push_back(0.0f);
                    }
                }
            }
        }
        
    } catch (const std::exception& e) {
        std::cerr << "批量预测过程中出现错误: " << e.what() << std::endl;
    }
    
    return results;
}

bool SpectrumPredictor::isModelLoaded() const
{
    return model_loaded_;
}

std::vector<std::string> SpectrumPredictor::getPropertyLabels() const
{
    return property_labels_;
}

std::vector<std::string> SpectrumPredictor::getWavelengthLabels() const
{
    return wavelength_labels_;
}

std::vector<float> SpectrumPredictor::applySNV(const std::vector<float>& spectrum)
{
    std::vector<float> snv_spectrum(spectrum.size());
    
    // 计算均值
    float mean_val = std::accumulate(spectrum.begin(), spectrum.end(), 0.0f) / spectrum.size();
    
    // 计算标准差
    float variance = 0.0f;
    for (float value : spectrum) {
        variance += (value - mean_val) * (value - mean_val);
    }
    float std_val = std::sqrt(variance / spectrum.size());
    
    // 应用SNV公式: (x - mean) / std
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

std::vector<float> SpectrumPredictor::applyFeatureSelection(const std::vector<float>& spectrum) const
{
    if (selected_feature_indices_.empty()) return spectrum;
    std::vector<float> filtered;
    filtered.reserve(selected_feature_indices_.size());
    for (int idx : selected_feature_indices_) {
        if (idx >= 0 && static_cast<size_t>(idx) < spectrum.size()) {
            filtered.push_back(spectrum[static_cast<size_t>(idx)]);
        }
    }
    return filtered;
}

std::vector<float> SpectrumPredictor::inverseScaleProperties(const std::vector<float>& scaled_properties)
{
    std::vector<float> original_properties(scaled_properties.size());
    
    if (!preprocessing_loaded_ || property_scaler_mean_.size() != scaled_properties.size()) {
        logMessage("警告：预处理参数未正确加载，返回原始值");
        return scaled_properties;
    }
    
    // 反标准化公式: original = scaled * scale + mean
    for (size_t i = 0; i < scaled_properties.size(); ++i) {
        original_properties[i] = scaled_properties[i] * property_scaler_scale_[i] + property_scaler_mean_[i];
    }
    
    return original_properties;
}

void SpectrumPredictor::loadPreprocessingParams(const std::string& preprocessing_params_path)
{
    // 优先使用Qt JSON解析，避免手写解析的鲁棒性问题
    try {
        QFile qfile(QString::fromStdString(preprocessing_params_path));
        if (!qfile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            logMessage("无法打开预处理参数文件: " + preprocessing_params_path);
            preprocessing_loaded_ = false;
            return;
        }
        QByteArray data = qfile.readAll();
        qfile.close();
        QJsonParseError perr;
        QJsonDocument jdoc = QJsonDocument::fromJson(data, &perr);
        if (perr.error != QJsonParseError::NoError || !jdoc.isObject()) {
            throw std::runtime_error("Qt JSON解析失败");
        }
        QJsonObject root = jdoc.object();
        // property_scaler
        property_scaler_mean_.clear();
        property_scaler_scale_.clear();
        if (root.contains("property_scaler") && root.value("property_scaler").isObject()) {
            QJsonObject ps = root.value("property_scaler").toObject();
            if (ps.contains("mean") && ps.value("mean").isArray()) {
                QJsonArray meanArr = ps.value("mean").toArray();
                property_scaler_mean_.reserve(meanArr.size());
                for (const auto &v : meanArr) property_scaler_mean_.push_back(static_cast<float>(v.toDouble()));
            }
            if (ps.contains("scale") && ps.value("scale").isArray()) {
                QJsonArray scArr = ps.value("scale").toArray();
                property_scaler_scale_.reserve(scArr.size());
                for (const auto &v : scArr) property_scaler_scale_.push_back(static_cast<float>(v.toDouble()));
            }
        }
        // PCA
        pca_loaded_ = false;
        pca_mean_.clear();
        pca_components_.clear();
        if (root.contains("pca") && root.value("pca").isObject()) {
            QJsonObject pca = root.value("pca").toObject();
            if (pca.contains("mean") && pca.value("mean").isArray()) {
                QJsonArray m = pca.value("mean").toArray();
                pca_mean_.reserve(m.size());
                for (const auto &v : m) pca_mean_.push_back(static_cast<float>(v.toDouble()));
            }
            if (pca.contains("components") && pca.value("components").isArray()) {
                QJsonArray comps = pca.value("components").toArray();
                pca_components_.reserve(comps.size());
                for (const auto &rowV : comps) {
                    if (!rowV.isArray()) continue;
                    QJsonArray rowA = rowV.toArray();
                    std::vector<float> row;
                    row.reserve(rowA.size());
                    for (const auto &e : rowA) row.push_back(static_cast<float>(e.toDouble()));
                    if (!row.empty()) pca_components_.push_back(std::move(row));
                }
            }
            if (!pca_mean_.empty() && !pca_components_.empty()) {
                logMessage("PCA参数加载成功(Qt): n_components=" + std::to_string(pca_components_.size()) + ", n_features=" + std::to_string(pca_mean_.size()));
                pca_loaded_ = true;
            }
        }
        // 完成
        preprocessing_loaded_ = (!property_scaler_mean_.empty() && !property_scaler_scale_.empty());
        if (preprocessing_loaded_) {
            logMessage("预处理参数加载成功");
            logMessage("属性标准化参数数量: " + std::to_string(property_scaler_mean_.size()));
        } else {
            logMessage("预处理参数解析失败（mean或scale为空）。请检查JSON格式与路径。");
        }
        return;
    } catch (...) {
        // 回退到原有的简单字符串解析
    }

    std::ifstream file(preprocessing_params_path);
    if (!file.is_open()) {
        logMessage("无法打开预处理参数文件: " + preprocessing_params_path);
        preprocessing_loaded_ = false;
        return;
    }
    std::string line;
    std::string content;
    while (std::getline(file, line)) {
        content += line;
    }
    file.close();

    try {
        // 记录解析起点与路径
        logMessage("开始解析预处理参数文件: " + preprocessing_params_path);
        property_scaler_mean_.clear();
        property_scaler_scale_.clear();
        
        // 解析属性标准化参数
        size_t start = content.find("\"property_scaler\"");
        if (start != std::string::npos) {
            // 解析均值：允许冒号后有空格
            size_t mean_key = content.find("\"mean\"", start);
            if (mean_key != std::string::npos) {
                size_t mean_bracket = content.find("[", mean_key);
                if (mean_bracket != std::string::npos) {
                    size_t mean_end = content.find("]", mean_bracket);
                    if (mean_end != std::string::npos) {
                        std::string mean_str = content.substr(mean_bracket + 1, mean_end - mean_bracket - 1);
                        std::stringstream ss(mean_str);
                        std::string value;
                        while (std::getline(ss, value, ',')) {
                            // 移除空格和换行符
                            value.erase(std::remove(value.begin(), value.end(), ' '), value.end());
                            value.erase(std::remove(value.begin(), value.end(), '\n'), value.end());
                            value.erase(std::remove(value.begin(), value.end(), '\r'), value.end());
                            if (!value.empty()) {
                                try { property_scaler_mean_.push_back(std::stof(value)); } catch (...) {}
                            }
                        }
                    }
                }
            }
            
            // 解析缩放因子：允许冒号后有空格
            size_t scale_key = content.find("\"scale\"", start);
            if (scale_key != std::string::npos) {
                size_t scale_bracket = content.find("[", scale_key);
                if (scale_bracket != std::string::npos) {
                    size_t scale_end = content.find("]", scale_bracket);
                    if (scale_end != std::string::npos) {
                        std::string scale_str = content.substr(scale_bracket + 1, scale_end - scale_bracket - 1);
                        std::stringstream ss(scale_str);
                        std::string value;
                        while (std::getline(ss, value, ',')) {
                            // 移除空格和换行符
                            value.erase(std::remove(value.begin(), value.end(), ' '), value.end());
                            value.erase(std::remove(value.begin(), value.end(), '\n'), value.end());
                            value.erase(std::remove(value.begin(), value.end(), '\r'), value.end());
                            if (!value.empty()) {
                                try { property_scaler_scale_.push_back(std::stof(value)); } catch (...) {}
                            }
                        }
                    }
                }
            }
        }
        
        // 解析PCA参数（可选）
        size_t pca_pos = content.find("\"pca\"");
        if (pca_pos != std::string::npos) {
            // mean
            size_t mean_key = content.find("\"mean\"", pca_pos);
            if (mean_key != std::string::npos) {
                size_t lb = content.find("[", mean_key);
                size_t rb = (lb != std::string::npos) ? content.find("]", lb) : std::string::npos;
                if (lb != std::string::npos && rb != std::string::npos) {
                    std::string arr = content.substr(lb + 1, rb - lb - 1);
                    std::stringstream ss(arr);
                    std::string token;
                    pca_mean_.clear();
                    while (std::getline(ss, token, ',')) {
                        token.erase(std::remove(token.begin(), token.end(), ' '), token.end());
                        token.erase(std::remove(token.begin(), token.end(), '\n'), token.end());
                        token.erase(std::remove(token.begin(), token.end(), '\r'), token.end());
                        if (!token.empty()) {
                            try { pca_mean_.push_back(std::stof(token)); } catch (...) {}
                        }
                    }
                }
            }
            // components（更稳健的二维数组提取）
            int expected_components = -1;
            size_t nc_key = content.find("\"n_components\"", pca_pos);
            if (nc_key != std::string::npos) {
                size_t colon = content.find(":", nc_key);
                if (colon != std::string::npos) {
                    size_t end = content.find_first_of(",}", colon + 1);
                    if (end != std::string::npos) {
                        std::string num = content.substr(colon + 1, end - (colon + 1));
                        num.erase(std::remove(num.begin(), num.end(), ' '), num.end());
                        try { expected_components = std::stoi(num); } catch (...) {}
                    }
                }
            }
            size_t comp_key = content.find("\"components\"", pca_pos);
            if (comp_key != std::string::npos) {
                size_t lb = content.find("[", comp_key);
                if (lb != std::string::npos) {
                    // 用括号计数找到匹配的右括号
                    int depth = 0;
                    size_t rb = std::string::npos;
                    for (size_t i = lb; i < content.size(); ++i) {
                        if (content[i] == '[') depth++;
                        else if (content[i] == ']') {
                            depth--;
                            if (depth == 0) { rb = i; break; }
                        }
                    }
                    if (rb != std::string::npos) {
                        std::string mat = content.substr(lb + 1, rb - lb - 1); // 去掉最外层[]
                        // 将 "],[" 分割成行
                        // 先去除所有空白
                        mat.erase(std::remove(mat.begin(), mat.end(), '\n'), mat.end());
                        mat.erase(std::remove(mat.begin(), mat.end(), '\r'), mat.end());
                        // 替换成分隔符
                        std::string delim = "],[";
                        size_t pos = 0; 
                        pca_components_.clear();
                        while (true) {
                            size_t next = mat.find(delim, pos);
                            std::string row = mat.substr(pos, (next == std::string::npos ? mat.size() : next) - pos);
                            // 去掉可能的前后方括号
                            if (!row.empty() && row.front() == '[') row.erase(row.begin());
                            if (!row.empty() && row.back() == ']') row.pop_back();
                            std::stringstream rs(row);
                            std::string val;
                            std::vector<float> comp_row;
                            while (std::getline(rs, val, ',')) {
                                val.erase(std::remove(val.begin(), val.end(), ' '), val.end());
                                if (!val.empty()) { try { comp_row.push_back(std::stof(val)); } catch (...) {} }
                            }
                            if (!comp_row.empty()) pca_components_.push_back(std::move(comp_row));
                            if (next == std::string::npos) break;
                            pos = next + delim.size();
                        }
                    }
                }
            }
            if (expected_components > 0 && static_cast<int>(pca_components_.size()) != expected_components) {
                logMessage("PCA主成分行数与n_components不一致: " + std::to_string(pca_components_.size()) + " vs " + std::to_string(expected_components));
            }
            pca_loaded_ = (!pca_mean_.empty() && !pca_components_.empty());
            if (pca_loaded_) {
                logMessage("PCA参数加载成功: n_components=" + std::to_string(pca_components_.size()) + ", n_features=" + std::to_string(pca_mean_.size()));
            } else {
                logMessage("PCA参数存在但解析失败");
            }
        }

        if (!property_scaler_mean_.empty() && !property_scaler_scale_.empty()) {
            preprocessing_loaded_ = true;
            logMessage("预处理参数加载成功");
            logMessage("属性标准化参数数量: " + std::to_string(property_scaler_mean_.size()));
        } else {
            preprocessing_loaded_ = false;
            logMessage("预处理参数解析失败（mean或scale为空）。请检查JSON格式与路径。");
        }
        
    } catch (const std::exception& e) {
        logMessage("预处理参数解析错误: " + std::string(e.what()));
        preprocessing_loaded_ = false;
    }
}

void SpectrumPredictor::loadModelInfo(const std::string& model_info_path)
{
    std::ifstream file(model_info_path);
    if (!file.is_open()) {
        throw std::runtime_error("无法打开模型信息文件: " + model_info_path);
    }
    
    // 简单的JSON解析（这里简化处理，实际项目中建议使用JSON库）
    std::string line;
    std::string content;
    while (std::getline(file, line)) {
        content += line;
    }
    file.close();
    
    // 解析属性标签
    size_t start = content.find("\"property_labels\":[");
    if (start != std::string::npos) {
        start = content.find("[", start);
        size_t end = content.find("]", start);
        if (end != std::string::npos) {
            std::string labels_str = content.substr(start + 1, end - start - 1);
            std::stringstream ss(labels_str);
            std::string label;
            while (std::getline(ss, label, ',')) {
                // 移除引号、空格和换行符
                label.erase(std::remove(label.begin(), label.end(), '"'), label.end());
                label.erase(std::remove(label.begin(), label.end(), ' '), label.end());
                label.erase(std::remove(label.begin(), label.end(), '\n'), label.end());
                label.erase(std::remove(label.begin(), label.end(), '\r'), label.end());
                if (!label.empty()) {
                    property_labels_.push_back(label);
                }
            }
        }
    }
    
    // 解析输入输出大小
    start = content.find("\"input_size\":");
    if (start != std::string::npos) {
        start = content.find(":", start);
        size_t end = content.find(",", start);
        if (end != std::string::npos) {
            input_size_ = std::stoi(content.substr(start + 1, end - start - 1));
        }
    }
    
    start = content.find("\"output_size\":");
    if (start != std::string::npos) {
        start = content.find(":", start);
        size_t end = content.find(",", start);
        if (end != std::string::npos) {
            output_size_ = std::stoi(content.substr(start + 1, end - start - 1));
        }
    }
    
    // 解析selected_feature_indices
    size_t fs = content.find("\"selected_feature_indices\"");
    if (fs != std::string::npos) {
        size_t lb = content.find("[", fs);
        size_t rb = (lb != std::string::npos) ? content.find("]", lb) : std::string::npos;
        if (lb != std::string::npos && rb != std::string::npos) {
            std::string idx_str = content.substr(lb + 1, rb - lb - 1);
            std::stringstream ss(idx_str);
            std::string token;
            while (std::getline(ss, token, ',')) {
                token.erase(std::remove(token.begin(), token.end(), ' '), token.end());
                token.erase(std::remove(token.begin(), token.end(), '\n'), token.end());
                token.erase(std::remove(token.begin(), token.end(), '\r'), token.end());
                if (!token.empty()) {
                    try { selected_feature_indices_.push_back(std::stoi(token)); } catch (...) {}
                }
            }
        }
    }

    // 注意：我们不再需要属性标准化参数，因为Python脚本会处理这些
}

/**
 * @brief 输出日志信息
 * @param message 日志消息
 * @details 如果有日志回调函数则调用，否则不输出
 */
void SpectrumPredictor::logMessage(const std::string& message)
{
    if (log_callback_) {
        log_callback_(message);
    }
}

void SpectrumPredictor::setLogCallback(std::function<void(const std::string&)> log_callback)
{
    log_callback_ = log_callback;
    // 同时设置LibTorch预测器的日志回调
    if (libtorch_predictor_) {
        libtorch_predictor_->setLogCallback(log_callback);
    }
}

