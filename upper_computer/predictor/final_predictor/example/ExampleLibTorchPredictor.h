/**
 * @file ExampleLibTorchPredictor.h
 * @brief LibTorch深度学习预测器头文件
 * @details 基于LibTorch的深度学习模型预测器，用于光谱数据属性预测
 *          使用PIMPL设计模式隐藏LibTorch实现细节
 * @author 系统设计项目组
 * @date 2024
 */

#pragma once

// 标准库头文件
#include <vector>    // 向量容器
#include <string>    // 字符串
#include <map>       // 映射容器
#include <memory>    // 智能指针
#include <functional> // 函数对象

/**
 * @class ExampleLibTorchPredictor
 * @brief LibTorch深度学习预测器类
 * @details 封装LibTorch深度学习模型，提供光谱数据属性预测功能
 *          使用PIMPL设计模式隐藏LibTorch实现细节，避免头文件依赖
 */
class ExampleLibTorchPredictor
{
public:
    /**
     * @brief 构造函数
     * @param model_path 模型文件路径（.jit文件）
     * @param model_info_path 模型信息文件路径（.json文件）
     * @param device 计算设备（"cpu"或"cuda"）
     * @details 加载TorchScript模型和模型信息，初始化预测器
     */
    ExampleLibTorchPredictor(const std::string& model_path,
                      const std::string& model_info_path,
                      const std::string& device = "cpu");
    
    /**
     * @brief 析构函数
     * @details 清理资源，释放模型内存
     */
    ~ExampleLibTorchPredictor();

    // === 预测接口 ===
    /**
     * @brief 单条光谱数据预测
     * @param spectrum 输入光谱数据向量
     * @return 预测结果映射（属性名->预测值）
     * @details 对单条光谱数据进行属性预测
     */
    std::map<std::string, float> predict(const std::vector<float>& spectrum);
    
    /**
     * @brief 批量光谱数据预测
     * @param spectra 输入光谱数据向量列表
     * @return 批量预测结果映射（属性名->预测值列表）
     * @details 对多条光谱数据进行批量属性预测，提高效率
     */
    std::map<std::string, std::vector<float>> predictBatch(const std::vector<std::vector<float>>& spectra);

    // === 模型信息查询接口 ===
    /**
     * @brief 检查模型是否已加载
     * @return true如果模型已加载，false否则
     * @details 检查TorchScript模型是否成功加载
     */
    bool isModelLoaded() const;
    
    /**
     * @brief 获取模型输入尺寸
     * @return 输入特征数量
     * @details 返回模型期望的输入光谱数据点数量
     */
    int getInputSize() const;
    
    /**
     * @brief 获取模型输出尺寸
     * @return 输出属性数量
     * @details 返回模型预测的属性数量
     */
    int getOutputSize() const;
    
    /**
     * @brief 获取属性标签列表
     * @return 属性名称列表
     * @details 返回模型预测的所有属性名称
     */
    std::vector<std::string> getPropertyLabels() const;
    
    /**
     * @brief 获取波长标签列表
     * @return 波长标签列表
     * @details 返回模型使用的波长标签（如果可用）
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
     * @brief 前向声明实现类
     * @details 使用PIMPL设计模式，隐藏LibTorch实现细节
     *          避免在头文件中包含LibTorch头文件
     */
    class Impl;
    
    /**
     * @brief 实现类智能指针
     * @details 使用unique_ptr管理实现类实例，确保资源正确释放
     */
    std::unique_ptr<Impl> pimpl_;
};

