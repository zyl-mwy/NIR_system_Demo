/**
 * @file ExampleSpectrumPredictor.cpp
 * @brief 光谱预测类实现文件 - 基于LibTorch
 * @details 实现光谱数据预测功能，包括SNV标准化和模型推理
 * @author 系统设计项目组
 * @date 2024
 */

#include "ExampleSpectrumPredictor.h"
#include "pre_processing.h"
#include "feature_selection.h"
#include "feature_reduction.h"
#include "log.h"
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

ExampleSpectrumPredictor::ExampleSpectrumPredictor(const std::string& model_path, 
                                     const std::string& model_info_path,
                                     const std::string& preprocessing_params_path,
                                     const std::string& device)
    : device_(device), model_loaded_(false), input_size_(0), output_size_(0), 
      preprocessing_loaded_(false), log_callback_(nullptr)
{
    try {
        // 创建LibTorch预测器
        libtorch_predictor_ = std::make_unique<ExampleLibTorchPredictor>(model_path, model_info_path, device);
        
        // 设置LibTorch预测器的日志回调
        if (libtorch_predictor_) {
            libtorch_predictor_->setLogCallback([this](const std::string& message) {
                upper_computer::basic::LogManager::info(message);
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
                upper_computer::basic::LogManager::error(std::string("加载模型信息失败: ") + e.what());
            }

            // 加载预处理参数
            loadPreprocessingParams(preprocessing_params_path);
            
            upper_computer::basic::LogManager::info(std::string("光谱预测模型加载成功（使用LibTorch）"));
            upper_computer::basic::LogManager::info(std::string("设备: ") + device_);
            upper_computer::basic::LogManager::info(std::string("输入特征数: ") + std::to_string(input_size_));
            upper_computer::basic::LogManager::info(std::string("输出属性数: ") + std::to_string(output_size_));
            upper_computer::basic::LogManager::info(std::string("预处理参数加载: ") + (preprocessing_loaded_ ? "成功" : "失败"));
        } else {
            model_loaded_ = false;
            upper_computer::basic::LogManager::error(std::string("LibTorch预测器加载失败"));
        }
        
    } catch (const std::exception& e) {
        upper_computer::basic::LogManager::error(std::string("模型加载失败: ") + std::string(e.what()));
        model_loaded_ = false;
    }
}

ExampleSpectrumPredictor::~ExampleSpectrumPredictor()
{
    // 析构函数，LibTorch会自动清理资源
}

std::map<std::string, float> ExampleSpectrumPredictor::predict(const std::vector<float>& spectrum)
{
    std::map<std::string, float> results;
    
    if (!model_loaded_) {
        std::cerr << "模型未加载，无法进行预测" << std::endl;
        return results;
    }
    
    // 尺寸检查延后到预处理/特征选择之后
    
    try {
        // 应用SNV预处理（基础模块）
        std::vector<float> preprocessed_spectrum = predictor::basic::applySNV(spectrum);
        // 应用VIP特征筛选（若有索引）
        if (!selected_feature_indices_.empty()) {
            size_t max_idx = static_cast<size_t>(*std::max_element(selected_feature_indices_.begin(), selected_feature_indices_.end()));
            if (spectrum.size() <= max_idx) {
                std::cerr << "输入光谱长度不足以进行特征筛选，长度: " << spectrum.size() << ", 最大索引: " << max_idx << std::endl;
                return results;
            }
            preprocessed_spectrum = predictor::basic::applyVipSelection(preprocessed_spectrum, selected_feature_indices_);
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
                preprocessed_spectrum = predictor::basic::applyPcaProject(preprocessed_spectrum, pca_mean_, pca_components_);
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
            upper_computer::basic::LogManager::debug(std::string("[调试] 已加载预处理参数，开始反标准化预测结果"));
            std::vector<float> scaled_values;
            for (const auto& label : property_labels_) {
                auto it = results.find(label);
                if (it != results.end()) {
                    scaled_values.push_back(it->second);
                } else {
                    scaled_values.push_back(0.0f);
                }
            }
            
            std::vector<float> original_values = predictor::basic::inverseTransformPredictions(scaled_values, property_scaler_mean_, property_scaler_scale_);
            

            // 更新结果
            for (size_t i = 0; i < property_labels_.size() && i < original_values.size(); ++i) {
                results[property_labels_[i]] = original_values[i];
            }

            // 调试：打印反标准化前后前若干维度对照
            if (!property_labels_.empty()) {
                size_t show_n = std::min<size_t>(property_labels_.size(), 3);
                for (size_t i = 0; i < show_n; ++i) {
                    upper_computer::basic::LogManager::debug(std::string("[调试] ") + property_labels_[i] +
                               std::string(" 标准化: ") + std::to_string(scaled_values[i]) +
                               std::string(", 反标准化: ") + std::to_string(original_values[i]));
                }
            }
        }
        
    } catch (const std::exception& e) {
        std::cerr << "预测过程中出现错误: " << e.what() << std::endl;
    }
    
    return results;
}

std::map<std::string, std::vector<float>> ExampleSpectrumPredictor::predictBatch(
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
            std::vector<float> preprocessed_spectrum = predictor::basic::applySNV(spectrum);
            if (!selected_feature_indices_.empty()) {
                size_t max_idx = static_cast<size_t>(*std::max_element(selected_feature_indices_.begin(), selected_feature_indices_.end()));
                if (spectrum.size() <= max_idx) {
                    std::cerr << "输入光谱长度不足以进行特征筛选(批量)，长度: " << spectrum.size() << ", 最大索引: " << max_idx << std::endl;
                    continue;
                }
                preprocessed_spectrum = predictor::basic::applyVipSelection(preprocessed_spectrum, selected_feature_indices_);
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
                    preprocessed_spectrum = predictor::basic::applyPcaProject(preprocessed_spectrum, pca_mean_, pca_components_);
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
                
                std::vector<float> original_values = predictor::basic::inverseTransformPredictions(scaled_values, property_scaler_mean_, property_scaler_scale_);

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
                upper_computer::basic::LogManager::debug(std::string("[调试] 预处理参数未加载，批量预测直接返回标准化输出"));
                
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

bool ExampleSpectrumPredictor::isModelLoaded() const
{
    return model_loaded_;
}

std::vector<std::string> ExampleSpectrumPredictor::getPropertyLabels() const
{
    return property_labels_;
}

std::vector<std::string> ExampleSpectrumPredictor::getWavelengthLabels() const
{
    return wavelength_labels_;
}



void ExampleSpectrumPredictor::loadPreprocessingParams(const std::string& preprocessing_params_path)
{
    // 优先使用Qt JSON解析，避免手写解析的鲁棒性问题
    try {
        QFile qfile(QString::fromStdString(preprocessing_params_path));
        if (!qfile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            upper_computer::basic::LogManager::error(std::string("无法打开预处理参数文件: ") + preprocessing_params_path);
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
                upper_computer::basic::LogManager::info(std::string("PCA参数加载成功(Qt): n_components=") + std::to_string(pca_components_.size()) + std::string(", n_features=") + std::to_string(pca_mean_.size()));
                pca_loaded_ = true;
            }
        }
        // 完成
        preprocessing_loaded_ = (!property_scaler_mean_.empty() && !property_scaler_scale_.empty());
        if (preprocessing_loaded_) {
            upper_computer::basic::LogManager::info(std::string("预处理参数加载成功"));
            upper_computer::basic::LogManager::info(std::string("属性标准化参数数量: ") + std::to_string(property_scaler_mean_.size()));
        } else {
            upper_computer::basic::LogManager::error(std::string("预处理参数解析失败（mean或scale为空）。请检查JSON格式与路径。"));
        }
        return;
    } catch (...) {
        // 回退到原有的简单字符串解析
    }

    std::ifstream file(preprocessing_params_path);
    if (!file.is_open()) {
        upper_computer::basic::LogManager::error(std::string("无法打开预处理参数文件: ") + preprocessing_params_path);
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
        upper_computer::basic::LogManager::info(std::string("开始解析预处理参数文件: ") + preprocessing_params_path);
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
                upper_computer::basic::LogManager::error(std::string("PCA主成分行数与n_components不一致: ") + std::to_string(pca_components_.size()) + std::string(" vs ") + std::to_string(expected_components));
            }
            pca_loaded_ = (!pca_mean_.empty() && !pca_components_.empty());
            if (pca_loaded_) {
                upper_computer::basic::LogManager::info(std::string("PCA参数加载成功: n_components=") + std::to_string(pca_components_.size()) + std::string(", n_features=") + std::to_string(pca_mean_.size()));
            } else {
                upper_computer::basic::LogManager::warning(std::string("PCA参数存在但解析失败"));
            }
        }

        if (!property_scaler_mean_.empty() && !property_scaler_scale_.empty()) {
            preprocessing_loaded_ = true;
            upper_computer::basic::LogManager::info(std::string("预处理参数加载成功"));
            upper_computer::basic::LogManager::info(std::string("属性标准化参数数量: ") + std::to_string(property_scaler_mean_.size()));
        } else {
            preprocessing_loaded_ = false;
            upper_computer::basic::LogManager::error(std::string("预处理参数解析失败（mean或scale为空）。请检查JSON格式与路径。"));
        }
        
    } catch (const std::exception& e) {
        upper_computer::basic::LogManager::error(std::string("预处理参数解析错误: ") + std::string(e.what()));
        preprocessing_loaded_ = false;
    }
}

void ExampleSpectrumPredictor::loadModelInfo(const std::string& model_info_path)
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

void ExampleSpectrumPredictor::setLogCallback(std::function<void(const std::string&)> log_callback)
{
    log_callback_ = log_callback;
    // 同时设置LibTorch预测器的日志回调
    if (libtorch_predictor_) {
        libtorch_predictor_->setLogCallback(log_callback);
    }
}

