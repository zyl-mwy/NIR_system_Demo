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

SpectrumPredictor::SpectrumPredictor(const std::string& model_path, 
                                     const std::string& model_info_path, 
                                     const std::string& device)
    : device_(device), model_loaded_(false), input_size_(0), output_size_(0), log_callback_(nullptr)
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
            
            logMessage("光谱预测模型加载成功（使用LibTorch）");
            logMessage("设备: " + device_);
            logMessage("输入特征数: " + std::to_string(input_size_));
            logMessage("输出属性数: " + std::to_string(output_size_));
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
    
    if (spectrum.size() != static_cast<size_t>(input_size_)) {
        std::cerr << "光谱数据长度不匹配，期望: " << input_size_ 
                  << ", 实际: " << spectrum.size() << std::endl;
        return results;
    }
    
    try {
        // 使用LibTorch预测器进行预测
        results = libtorch_predictor_->predict(spectrum);
        
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
            
            auto prediction = libtorch_predictor_->predict(spectrum);
            
            // 将预测结果添加到批量结果中
            for (const auto& label : property_labels_) {
                auto it = prediction.find(label);
                if (it != prediction.end()) {
                    results[label].push_back(it->second);
                } else {
                    results[label].push_back(0.0f);
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

