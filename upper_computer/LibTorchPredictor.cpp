/**
 * @file LibTorchPredictor.cpp
 * @brief LibTorch深度学习预测器实现文件
 * @details 实现基于LibTorch的深度学习模型预测器，提供光谱数据属性预测功能
 * @author 系统设计项目组
 * @date 2024
 */

#include "LibTorchPredictor.h"

// LibTorch头文件
#include <torch/torch.h>      // LibTorch核心库
#include <torch/script.h>     // TorchScript模型加载

// 标准库头文件
#include <fstream>            // 文件流
#include <iostream>           // 输入输出流
#include <sstream>            // 字符串流
#include <algorithm>          // 算法库
#include <numeric>            // 数值算法

/**
 * @class LibTorchPredictor::Impl
 * @brief LibTorch预测器实现类
 * @details 使用PIMPL设计模式，隐藏LibTorch实现细节
 *          包含模型加载、预测执行、数据预处理等核心功能
 */
class LibTorchPredictor::Impl
{
public:
    // === 模型相关成员变量 ===
    torch::jit::script::Module model_;        // TorchScript模型
    torch::Device torch_device_;              // 计算设备（CPU/CUDA）
    bool model_loaded_;                       // 模型是否已加载
    int input_size_;                          // 输入特征数量
    int output_size_;                         // 输出属性数量
    std::vector<std::string> property_labels_;    // 属性标签列表
    std::vector<std::string> wavelength_labels_;  // 波长标签列表
    std::function<void(const std::string&)> log_callback_;  // 日志回调函数

    /**
     * @brief 实现类构造函数
     * @param model_path 模型文件路径（.jit文件）
     * @param model_info_path 模型信息文件路径（.json文件）
     * @param device 计算设备（"cpu"或"cuda"）
     * @details 初始化预测器，加载模型和模型信息
     */
    Impl(const std::string& model_path,
         const std::string& model_info_path,
         const std::string& device)
        : torch_device_(torch::kCPU), model_loaded_(false), input_size_(0), output_size_(0), log_callback_(nullptr)
    {
        try {
            // === 设备设置 ===
            if (device == "cuda" && torch::cuda::is_available()) {
                torch_device_ = torch::kCUDA;
            }

            // === 加载模型信息 ===
            loadModelInfo(model_info_path);

            // === 加载LibTorch模型 ===
            model_ = torch::jit::load(model_path, torch_device_);
            model_.eval();  // 设置为评估模式
            model_loaded_ = true;

            // === 输出加载信息 ===
            logMessage("LibTorch模型加载成功");
            logMessage("设备: " + std::string(torch_device_.is_cuda() ? "cuda" : "cpu"));
            logMessage("输入特征数: " + std::to_string(input_size_));
            logMessage("输出属性数: " + std::to_string(output_size_));

        } catch (const std::exception& e) {
            logMessage("加载LibTorch模型失败: " + std::string(e.what()));
            model_loaded_ = false;
        }
    }

    /**
     * @brief 输出日志信息
     * @param message 日志消息
     * @details 如果有日志回调函数则调用，否则不输出
     */
    void logMessage(const std::string& message)
    {
        if (log_callback_) {
            log_callback_(message);
        }
    }

    std::map<std::string, float> predict(const std::vector<float>& spectrum)
    {
        std::map<std::string, float> results;
        
        if (!model_loaded_) {
            logMessage("❌ LibTorch模型未加载，无法进行预测");
            return results;
        }

        try {
            logMessage("🔧 开始LibTorch预测处理...");
            logMessage("  - 输入光谱数据点数: " + std::to_string(spectrum.size()));
            
            // 直接使用原始光谱数据（移除SNV标准化）
            logMessage("  - 跳过SNV标准化，直接使用原始光谱数据");

            // 转换为torch::Tensor
            logMessage("  - 转换为torch::Tensor...");
            torch::Tensor input_tensor = torch::from_blob(
                const_cast<float*>(spectrum.data()),
                {1, static_cast<long>(spectrum.size())},
                torch::kFloat
            ).clone().to(torch_device_);
            logMessage("  - Tensor形状: [" + std::to_string(input_tensor.size(0)) + ", " + std::to_string(input_tensor.size(1)) + "]");

            // 进行预测
            logMessage("  - 执行模型推理...");
            std::vector<torch::jit::IValue> inputs;
            inputs.push_back(input_tensor);
            
            torch::Tensor output = model_.forward(inputs).toTensor();
            output = output.to(torch::kCPU);
            logMessage("  - 模型推理完成，输出形状: [" + std::to_string(output.size(0)) + ", " + std::to_string(output.size(1)) + "]");

            // 提取结果
            logMessage("  - 提取预测结果...");
            auto output_accessor = output.accessor<float, 2>();
            
            // 打印所有原始数值
            logMessage("  - 模型输出原始数值 [1, " + std::to_string(output.size(1)) + "]:");
            for (int i = 0; i < output.size(1); ++i) {
                logMessage("    [" + std::to_string(i) + "] = " + std::to_string(output_accessor[0][i]));
            }

            // 映射到属性标签
            logMessage("  - 映射到属性标签:");
            logMessage("  - 属性标签数量: " + std::to_string(property_labels_.size()));
            for (size_t i = 0; i < property_labels_.size(); ++i) {
                logMessage("    标签[" + std::to_string(i) + "]: " + property_labels_[i]);
            }
            for (size_t i = 0; i < property_labels_.size() && i < static_cast<size_t>(output.size(1)); ++i) {
                results[property_labels_[i]] = output_accessor[0][i];
                logMessage("    " + property_labels_[i] + ": " + std::to_string(output_accessor[0][i]));
            }
            logMessage("✅ LibTorch预测处理完成，共" + std::to_string(results.size()) + "个属性");

        } catch (const std::exception& e) {
            logMessage("❌ LibTorch预测过程中出现错误: " + std::string(e.what()));
        }

        return results;
    }

    std::map<std::string, std::vector<float>> predictBatch(const std::vector<std::vector<float>>& spectra)
    {
        std::map<std::string, std::vector<float>> results;
        
        if (!model_loaded_) {
            logMessage("模型未加载，无法进行批量预测");
            return results;
        }

        // 初始化结果容器
        for (const auto& label : property_labels_) {
            results[label] = std::vector<float>(spectra.size(), 0.0f);
        }

        // 逐个预测
        for (size_t i = 0; i < spectra.size(); ++i) {
            auto prediction = predict(spectra[i]);
            for (const auto& label : property_labels_) {
                auto it = prediction.find(label);
                if (it != prediction.end()) {
                    results[label][i] = it->second;
                }
            }
        }

        return results;
    }

    bool isModelLoaded() const
    {
        return model_loaded_;
    }

    int getInputSize() const
    {
        return input_size_;
    }

    int getOutputSize() const
    {
        return output_size_;
    }

    std::vector<std::string> getPropertyLabels() const
    {
        return property_labels_;
    }

    std::vector<std::string> getWavelengthLabels() const
    {
        return wavelength_labels_;
    }

private:
    std::vector<float> applySNV(const std::vector<float>& spectrum)
    {
        std::vector<float> normalized(spectrum);
        
        // 计算均值
        float mean = std::accumulate(normalized.begin(), normalized.end(), 0.0f) / normalized.size();
        
        // 计算标准差
        float variance = 0.0f;
        for (float value : normalized) {
            variance += (value - mean) * (value - mean);
        }
        float std_dev = std::sqrt(variance / normalized.size());
        
        // 应用SNV标准化
        if (std_dev > 0) {
            for (float& value : normalized) {
                value = (value - mean) / std_dev;
            }
        }
        
        return normalized;
    }

    void loadModelInfo(const std::string& model_info_path)
    {
        std::ifstream file(model_info_path);
        if (!file.is_open()) {
            logMessage("无法打开模型信息文件: " + model_info_path);
            return;
        }

        std::string content((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
        file.close();

        // 解析JSON（简单解析）
        // 查找输入特征数
        size_t pos = content.find("\"input_size\":");
        if (pos != std::string::npos) {
            pos += 13; // "input_size":的长度
            size_t end = content.find_first_of(",}", pos);
            if (end != std::string::npos) {
                input_size_ = std::stoi(content.substr(pos, end - pos));
            }
        }

        // 查找输出属性数
        pos = content.find("\"output_size\":");
        if (pos != std::string::npos) {
            pos += 14; // "output_size":的长度
            size_t end = content.find_first_of(",}", pos);
            if (end != std::string::npos) {
                output_size_ = std::stoi(content.substr(pos, end - pos));
            }
        }

        // 查找属性标签
        pos = content.find("property_labels");
        if (pos != std::string::npos) {
            logMessage("找到property_labels字段");
            // 查找冒号后的方括号
            pos = content.find("[", pos);
            if (pos != std::string::npos) {
                logMessage("找到方括号");
                pos++; // 跳过方括号
                size_t end = content.find("]", pos);
                if (end != std::string::npos) {
                    std::string labels_str = content.substr(pos, end - pos);
                    logMessage("属性标签字符串: " + labels_str);
                    std::istringstream iss(labels_str);
                    std::string label;
                    while (std::getline(iss, label, ',')) {
                        // 移除引号、空格和换行符
                        label.erase(std::remove(label.begin(), label.end(), '"'), label.end());
                        label.erase(std::remove(label.begin(), label.end(), ' '), label.end());
                        label.erase(std::remove(label.begin(), label.end(), '\n'), label.end());
                        label.erase(std::remove(label.begin(), label.end(), '\r'), label.end());
                        if (!label.empty()) {
                            property_labels_.push_back(label);
                            logMessage("添加标签: " + label);
                        }
                    }
                    logMessage("总共加载了 " + std::to_string(property_labels_.size()) + " 个属性标签");
                } else {
                    logMessage("未找到属性标签的结束位置");
                }
            } else {
                logMessage("未找到方括号");
            }
        } else {
            logMessage("未找到property_labels字段");
        }

        // 查找波长标签
        pos = content.find("\"wavelength_labels\":[");
        if (pos != std::string::npos) {
            pos += 21; // "wavelength_labels":[的长度
            size_t end = content.find("]", pos);
            if (end != std::string::npos) {
                std::string labels_str = content.substr(pos, end - pos);
                std::istringstream iss(labels_str);
                std::string label;
                while (std::getline(iss, label, ',')) {
                    // 移除引号和空格
                    label.erase(std::remove(label.begin(), label.end(), '"'), label.end());
                    label.erase(std::remove(label.begin(), label.end(), ' '), label.end());
                    if (!label.empty()) {
                        wavelength_labels_.push_back(label);
                    }
                }
            }
        }
    }
};

// LibTorchPredictor实现
LibTorchPredictor::LibTorchPredictor(const std::string& model_path,
                                     const std::string& model_info_path,
                                     const std::string& device)
    : pimpl_(std::make_unique<Impl>(model_path, model_info_path, device))
{
}

LibTorchPredictor::~LibTorchPredictor() = default;

std::map<std::string, float> LibTorchPredictor::predict(const std::vector<float>& spectrum)
{
    return pimpl_->predict(spectrum);
}

std::map<std::string, std::vector<float>> LibTorchPredictor::predictBatch(const std::vector<std::vector<float>>& spectra)
{
    return pimpl_->predictBatch(spectra);
}

bool LibTorchPredictor::isModelLoaded() const
{
    return pimpl_->isModelLoaded();
}

int LibTorchPredictor::getInputSize() const
{
    return pimpl_->getInputSize();
}

int LibTorchPredictor::getOutputSize() const
{
    return pimpl_->getOutputSize();
}

std::vector<std::string> LibTorchPredictor::getPropertyLabels() const
{
    return pimpl_->getPropertyLabels();
}

std::vector<std::string> LibTorchPredictor::getWavelengthLabels() const
{
    return pimpl_->getWavelengthLabels();
}

void LibTorchPredictor::setLogCallback(std::function<void(const std::string&)> log_callback)
{
    pimpl_->log_callback_ = log_callback;
}
