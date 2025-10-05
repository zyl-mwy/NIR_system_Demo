/**
 * @file ExampleSpectrumPredictor.h
 * @brief 光谱预测类头文件 - 基于LibTorch
 * @details 使用LibTorch进行光谱数据预测，支持SNV标准化和模型推理
 * @author 系统设计项目组
 * @date 2024
 */

#pragma once

// 暂时使用Python脚本进行预测，避免LibTorch集成问题
// #include "/media/linxi-ice/other/科研/系统设计/c_libtorch_test/libtorch/include/torch/csrc/api/include/torch/torch.h"
// #include "/media/linxi-ice/other/科研/系统设计/c_libtorch_test/libtorch/include/torch/script.h"
#include <vector>
#include <string>
#include <map>
#include <memory>
#include <functional>
#include "ExampleLibTorchPredictor.h"

/**
 * @class ExampleSpectrumPredictor
 * @brief 光谱预测类
 * @details 使用LibTorch加载训练好的PyTorch模型，进行光谱数据预测
 */
class ExampleSpectrumPredictor
{
public:
    /**
     * @brief 构造函数
     * @param model_path 模型文件路径
     * @param model_info_path 模型信息文件路径
     * @param preprocessing_params_path 预处理参数文件路径
     * @param device 计算设备 ("cpu" 或 "cuda")
     */
    ExampleSpectrumPredictor(const std::string& model_path, 
                     const std::string& model_info_path,
                     const std::string& preprocessing_params_path,
                     const std::string& device = "cpu");
    
    /**
     * @brief 析构函数
     */
    ~ExampleSpectrumPredictor();
    
    /**
     * @brief 预测单个光谱样本
     * @param spectrum 光谱数据向量
     * @return 预测结果映射 (属性名 -> 预测值)
     */
    std::map<std::string, float> predict(const std::vector<float>& spectrum);
    
    /**
     * @brief 预测多个光谱样本
     * @param spectra 光谱数据矩阵 (样本数 x 特征数)
     * @return 预测结果映射 (属性名 -> 预测值向量)
     */
    std::map<std::string, std::vector<float>> predictBatch(const std::vector<std::vector<float>>& spectra);
    
    /**
     * @brief 检查模型是否已加载
     * @return 模型是否可用
     */
    bool isModelLoaded() const;
    
    /**
     * @brief 获取属性标签列表
     * @return 属性标签向量
     */
    std::vector<std::string> getPropertyLabels() const;
    
    /**
     * @brief 获取波长标签列表
     * @return 波长标签向量
     */
    std::vector<std::string> getWavelengthLabels() const;

    // === 日志功能 ===
    /**
     * @brief 设置日志回调函数
     * @param log_callback 日志回调函数指针
     * @details 设置用于输出日志信息的回调函数
     */
    void setLogCallback(std::function<void(const std::string&)> log_callback);

private:
    /**
     * @brief 输出日志信息
     * @param message 日志消息
     * @details 如果有日志回调函数则调用，否则不输出
     */
    
    /**
     * @brief 加载模型信息
     * @param model_info_path 模型信息文件路径
     */
    void loadModelInfo(const std::string& model_info_path);
    
    /**
     * @brief 加载预处理参数
     * @param preprocessing_params_path 预处理参数文件路径
     */
    void loadPreprocessingParams(const std::string& preprocessing_params_path);
    
    // LibTorch预测器
    std::unique_ptr<ExampleLibTorchPredictor> libtorch_predictor_;

private:
    std::string device_;                         // 计算设备
    bool model_loaded_;                          // 模型是否已加载
    int input_size_;                             // 输入特征数量
    int output_size_;                            // 输出属性数量
    std::vector<std::string> property_labels_;   // 属性标签
    std::vector<std::string> wavelength_labels_; // 波长标签
    std::vector<int> selected_feature_indices_;  // VIP选择的特征索引（用于特征子集）
    // PCA 参数（在VIP后）
    bool pca_loaded_ = false;
    std::vector<float> pca_mean_;
    std::vector<std::vector<float>> pca_components_; // n_components x n_features
    std::function<void(const std::string&)> log_callback_;  // 日志回调函数
    
    // 预处理参数
    std::vector<float> property_scaler_mean_;    // 属性标准化均值
    std::vector<float> property_scaler_scale_;   // 属性标准化缩放因子
    bool preprocessing_loaded_;                  // 预处理参数是否已加载
};
