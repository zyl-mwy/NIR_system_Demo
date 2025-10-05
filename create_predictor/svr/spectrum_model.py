#!/usr/bin/env python3
"""
光谱预测模型训练程序 - 基于SVR (Support Vector Regression)

训练侧完整流水线（与上位机严格对齐）：
1) SNV：对每个样本做标准正态变量（SNV）标准化，消除强度偏置
2) VIP：使用PLS计算变量重要性（VIP），筛选出最重要的波段索引（selected_feature_indices）
3) PCA：在VIP后的子集上做降维，得到固定维度特征（n_components）
4) SVR：以PCA后的特征作为SVR输入进行训练

导出文件：
- model_info.json：记录input_size、output_size、property_labels、wavelength_labels、selected_feature_indices
- preprocessing_params.json：记录SNV统计、StandardScaler参数、PCA均值与主成分
- feature_scaler.pkl：SVR特征标准化器
- Property2_model.pkl：训练好的SVR模型

注意：若修改VIP的top_k或PCA的n_components，需重新训练并同步上述文件到上位机，保证推理一致。
"""

import numpy as np
import pandas as pd
from sklearn.svm import SVR
from sklearn.metrics import mean_squared_error, r2_score
from sklearn.cross_decomposition import PLSRegression
from sklearn.decomposition import PCA
from sklearn.preprocessing import StandardScaler
from sklearn.model_selection import GridSearchCV
import matplotlib.pyplot as plt
import os
import json
import pickle
from typing import List, Tuple, Dict

# 以仓库根目录为基准进行路径解析，保证脚本移动后输出位置不变
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, '..', '..'))

# 控制是否生成训练曲线图。默认禁用，可通过环境变量覆盖：
# DISABLE_TRAINING_HISTORY=0 以启用
DISABLE_TRAINING_HISTORY = os.environ.get("DISABLE_TRAINING_HISTORY", "1").lower() in ("1", "true", "yes")

def load_data(spectrum_file: str, property_file: str) -> Tuple[np.ndarray, np.ndarray, List[str], List[str]]:
    """
    加载光谱和属性数据
    Args:
        spectrum_file: 光谱数据文件路径
        property_file: 属性数据文件路径
    Returns:
        (光谱数据, 属性数据, 波长标签, 属性标签)
    """
    print("正在加载数据...")
    
    # 加载光谱数据
    spec_df = pd.read_csv(spectrum_file, header=None)
    spec_data = spec_df.iloc[9:].values
    
    # 提取波长标签（第10行）
    wavelength_labels = spec_df.iloc[9, 2:].dropna().astype(str).tolist()
    
    # 提取光谱数据（从第11行开始，跳过前2列，移除最后一列NaN）
    spectra = spec_data[1:, 2:].astype(float)
    spectra = spectra[:, ~np.isnan(spectra).any(axis=0)]  # 移除包含NaN的列
    
    # 加载属性数据
    prop_df = pd.read_csv(property_file, header=None)
    prop_data = prop_df.iloc[9:].values
    
    # 提取属性标签（第9行，真正的标签行）
    property_labels = prop_df.iloc[8, 2:].dropna().astype(str).tolist()
    
    # 提取属性数据（从第11行开始，只提取有标签的列）
    properties = prop_data[1:, 2:2+len(property_labels)].astype(float)
    
    # 确保光谱数据和属性数据的行数一致
    min_rows = min(spectra.shape[0], properties.shape[0])
    spectra = spectra[:min_rows]
    properties = properties[:min_rows]
    
    # 只移除光谱数据中包含NaN的行
    valid_indices = ~np.isnan(spectra).any(axis=1)
    spectra = spectra[valid_indices]
    properties = properties[valid_indices]
    
    # 对于属性数据，用0填充NaN值
    properties = np.nan_to_num(properties, nan=0.0)
    
    print(f"光谱数据形状: {spectra.shape}")
    print(f"属性数据形状: {properties.shape}")
    print(f"波长数量: {len(wavelength_labels)}")
    print(f"属性数量: {len(property_labels)}")
    
    return spectra, properties, wavelength_labels, property_labels

def apply_snv(spectra: np.ndarray) -> np.ndarray:
    """
    对光谱数据应用SNV (Standard Normal Variate) 标准化
    Args:
        spectra: 原始光谱数据 (样本数 x 波长数)
    Returns:
        SNV标准化后的光谱数据
    """
    print("正在应用SNV标准化...")
    
    # SNV标准化：对每个样本进行标准化
    # 公式: (x - mean(x)) / std(x)
    snv_spectra = np.zeros_like(spectra)
    
    for i in range(spectra.shape[0]):
        sample = spectra[i, :]
        # 计算均值和标准差
        mean_val = np.mean(sample)
        std_val = np.std(sample)
        
        # 避免除零错误
        if std_val > 1e-8:
            snv_spectra[i, :] = (sample - mean_val) / std_val
        else:
            snv_spectra[i, :] = sample - mean_val
    
    print(f"SNV标准化完成，数据形状: {snv_spectra.shape}")
    return snv_spectra

def apply_property_scaling(properties: np.ndarray) -> Tuple[np.ndarray, StandardScaler]:
    """
    对属性数据进行标准化
    Args:
        properties: 原始属性数据 (样本数 x 属性数)
    Returns:
        (标准化后的属性数据, 标准化器对象)
    """
    print("正在对属性数据进行标准化...")
    
    scaler = StandardScaler()
    scaled_properties = scaler.fit_transform(properties)
    
    print(f"属性数据标准化完成，数据形状: {scaled_properties.shape}")
    return scaled_properties, scaler

def save_preprocessing_params(snv_spectra: np.ndarray, property_scaler: StandardScaler, 
                            save_path: str = 'model/svr/preprocessing_params.json',
                            pca: PCA = None):
    """
    保存预处理参数供上位机使用
    - 目的：保证上位机复现训练时的同一处理链
    - 写入：SNV统计（mean/std）、属性StandardScaler（mean/scale）、可选PCA（mean/components/n_components）
    Args:
        snv_spectra: SNV标准化后的光谱数据（用于计算统计信息）
        property_scaler: 属性数据标准化器
        save_path: 保存路径
        pca: 若启用PCA，则传入以便序列化其参数
    """
    print("正在保存预处理参数...")
    
    # 计算光谱数据的统计信息（用于验证）
    spectrum_mean = np.mean(snv_spectra, axis=0)
    spectrum_std = np.std(snv_spectra, axis=0)
    
    preprocessing_params = {
        'spectrum_stats': {
            'mean': spectrum_mean.tolist(),
            'std': spectrum_std.tolist()
        },
        'property_scaler': {
            'mean': property_scaler.mean_.tolist(),
            'scale': property_scaler.scale_.tolist()
        },
        'preprocessing_type': 'SNV',
        'description': 'SNV标准化用于光谱数据，StandardScaler用于属性数据'
    }
    if pca is not None:
        # components_形状：(n_components, n_features_after_VIP)
        # 上位机将以mean_长度作为期望特征数，先对VIP后的特征对齐，再执行投影
        preprocessing_params['pca'] = {
            'mean': pca.mean_.tolist(),
            'components': pca.components_.tolist(),
            'n_components': int(pca.n_components_)
        }
    
    with open(save_path, 'w') as f:
        json.dump(preprocessing_params, f, indent=2)
    
    print(f"预处理参数已保存到: {save_path}")

def compute_vip_scores(X: np.ndarray, Y: np.ndarray, n_components: int = 10) -> np.ndarray:
    """
    使用PLS回归计算VIP分数（多输出Y）
    Args:
        X: 预处理后的光谱矩阵 (n_samples, n_features)
        Y: 标准化后的属性矩阵 (n_samples, n_targets)
        n_components: PLS成分数
    Returns:
        vip: 每个特征的VIP分数 (n_features,)
    """
    n_components = max(1, min(n_components, min(X.shape[0]-1, X.shape[1])))
    pls = PLSRegression(n_components=n_components)
    pls.fit(X, Y)

    T = pls.x_scores_               # (n_samples, n_components)
    W = pls.x_weights_              # (n_features, n_components)
    Q = pls.y_loadings_             # (n_targets, n_components)

    # 每个成分的解释方差贡献（对Y）
    # 使用T的方差与Q的平方和来度量
    ssy = np.sum((T ** 2), axis=0) * np.sum((Q ** 2), axis=0)  # (n_components,)
    ssy_total = np.sum(ssy)
    ssy = ssy / (ssy_total + 1e-12)

    # 计算VIP
    W_norm = W / (np.linalg.norm(W, axis=0, keepdims=True) + 1e-12)
    vip = np.sqrt(X.shape[1] * np.sum((W_norm ** 2) * ssy[np.newaxis, :], axis=1))
    return vip

def select_features_vip(X: np.ndarray, Y: np.ndarray, top_k: int = 100, n_components: int = 10) -> np.ndarray:
    """
    计算VIP分数并选择前top_k特征的索引
    """
    top_k = min(top_k, X.shape[1])
    vip = compute_vip_scores(X, Y, n_components=n_components)
    selected_idx = np.argsort(vip)[::-1][:top_k]
    selected_idx = np.sort(selected_idx)
    return selected_idx

def train_svr_models(X_train: np.ndarray, y_train: np.ndarray, 
                    X_val: np.ndarray, y_val: np.ndarray,
                    property_labels: List[str]) -> List[SVR]:
    """
    训练SVR模型（每个属性一个模型）
    Args:
        X_train: 训练特征数据
        y_train: 训练标签数据
        X_val: 验证特征数据
        y_val: 验证标签数据
        property_labels: 属性标签列表
    Returns:
        训练好的SVR模型列表
    """
    print("正在训练SVR模型...")
    
    # 为每个属性训练一个SVR模型
    svr_models = []
    
    for i, label in enumerate(property_labels):
        print(f"训练属性 {i+1}/{len(property_labels)}: {label}")
        
        # 提取当前属性的训练和验证数据
        y_train_prop = y_train[:, i]
        y_val_prop = y_val[:, i]
        
        # 定义SVR参数网格
        param_grid = {
            'C': [0.1, 1, 10, 100],
            'gamma': ['scale', 'auto', 0.001, 0.01, 0.1, 1],
            'epsilon': [0.01, 0.1, 0.2, 0.5]
        }
        
        # 创建SVR模型
        svr = SVR(kernel='rbf')
        
        # 使用网格搜索进行超参数优化
        grid_search = GridSearchCV(
            svr, 
            param_grid, 
            cv=3, 
            scoring='neg_mean_squared_error',
            n_jobs=-1,
            verbose=0
        )
        
        # 训练模型
        grid_search.fit(X_train, y_train_prop)
        
        # 获取最佳模型
        best_svr = grid_search.best_estimator_
        svr_models.append(best_svr)
        
        # 评估模型
        y_pred = best_svr.predict(X_val)
        mse = mean_squared_error(y_val_prop, y_pred)
        r2 = r2_score(y_val_prop, y_pred)
        
        print(f"  {label}: MSE = {mse:.4f}, R² = {r2:.4f}")
        print(f"  最佳参数: {grid_search.best_params_}")
    
    return svr_models

def evaluate_svr_models(svr_models: List[SVR], X_val: np.ndarray, y_val: np.ndarray, 
                       property_labels: List[str], property_scaler: StandardScaler = None) -> None:
    """
    评估SVR模型性能
    Args:
        svr_models: 训练好的SVR模型列表
        X_val: 验证特征数据
        y_val: 验证标签数据
        property_labels: 属性标签列表
        property_scaler: 属性数据标准化器（用于反标准化）
    """
    print("\n=== SVR模型性能评估 ===")
    
    all_predictions = []
    all_targets = []
    
    for i, (svr_model, label) in enumerate(zip(svr_models, property_labels)):
        # 预测
        y_pred = svr_model.predict(X_val)
        all_predictions.append(y_pred)
        all_targets.append(y_val[:, i])
        
        # 计算评估指标
        mse = mean_squared_error(y_val[:, i], y_pred)
        r2 = r2_score(y_val[:, i], y_pred)
        
        print(f"{label}: MSE = {mse:.4f}, R² = {r2:.4f}")
    
    # 反标准化到原始尺度
    if property_scaler is not None:
        predictions_original = property_scaler.inverse_transform(np.array(all_predictions).T)
        targets_original = property_scaler.inverse_transform(y_val)
        
        print("\n=== 反标准化后的预测结果（实际值）===")
        print("预测值（实际）:", predictions_original[:5])
        print("真实值（实际）:", targets_original[:5])
        
        # 使用反标准化后的数据计算评估指标
        print("\n模型性能评估（基于实际值）:")
        for i, label in enumerate(property_labels):
            mse = mean_squared_error(targets_original[:, i], predictions_original[:, i])
            r2 = r2_score(targets_original[:, i], predictions_original[:, i])
            print(f"{label}: MSE = {mse:.4f}, R² = {r2:.4f}")

def save_svr_models(svr_models: List[SVR], feature_scaler: StandardScaler, 
                   save_dir: str = 'model/svr'):
    """
    保存SVR模型和特征标准化器
    Args:
        svr_models: 训练好的SVR模型列表
        feature_scaler: 特征标准化器
        save_dir: 保存目录
    """
    print("正在保存SVR模型...")
    
    # 创建保存目录
    if not os.path.exists(save_dir):
        os.makedirs(save_dir)
    
    # 保存SVR模型
    model_path = os.path.join(save_dir, 'Property2_model.pkl')
    with open(model_path, 'wb') as f:
        pickle.dump(svr_models, f)
    print(f"SVR模型已保存到: {model_path}")
    
    # 保存特征标准化器
    scaler_path = os.path.join(save_dir, 'feature_scaler.pkl')
    with open(scaler_path, 'wb') as f:
        pickle.dump(feature_scaler, f)
    print(f"特征标准化器已保存到: {scaler_path}")

def save_model_info(input_size: int, output_size: int, property_labels: List[str], 
                   wavelength_labels: List[str], selected_feature_indices: List[int],
                   save_path: str = None):
    """保存模型信息"""
    model_info = {
        'input_size': input_size,
        'output_size': output_size,
        'property_labels': property_labels,
        'wavelength_labels': wavelength_labels,
        'selected_feature_indices': selected_feature_indices,
        'model_type': 'SVR',
        'description': '基于支持向量回归的光谱预测模型'
    }
    
    if save_path is None:
        save_path = os.path.join(ROOT_DIR, 'model', 'svr', 'model_info.json')
    with open(save_path, 'w') as f:
        json.dump(model_info, f, indent=2)
    
    print(f"模型信息已保存到: {save_path}")

def main():
    """主函数"""
    print("=== 光谱预测模型训练程序（基于SVR）===")
    
    # 设置随机种子
    np.random.seed(42)
    
    # 创建模型目录
    model_dir = os.path.join(ROOT_DIR, 'model', 'svr')
    if not os.path.exists(model_dir):
        os.makedirs(model_dir)
    
    # 加载数据
    spectrum_file = os.path.join(ROOT_DIR, 'data', 'diesel_spec.csv')
    property_file = os.path.join(ROOT_DIR, 'data', 'diesel_prop.csv')
    
    spectra, properties, wavelength_labels, property_labels = load_data(spectrum_file, property_file)
    
    # 应用SNV预处理
    snv_spectra = apply_snv(spectra)
    
    # 对属性数据进行标准化
    scaled_properties, property_scaler = apply_property_scaling(properties)
    
    # === VIP特征选择 ===
    top_k = min(100, snv_spectra.shape[1])
    selected_idx = select_features_vip(snv_spectra, scaled_properties, top_k=top_k, n_components=10)
    print(f"选中特征数: {len(selected_idx)}，示例索引: {selected_idx[:10]}")

    # 根据选择的索引筛选训练数据
    snv_feat = snv_spectra[:, selected_idx]

    # === PCA降维（在VIP之后）===
    pca_components = min(32, snv_feat.shape[1])
    pca = PCA(n_components=pca_components)
    snv_feat_pca = pca.fit_transform(snv_feat)

    # 保存预处理参数（包含PCA）
    save_preprocessing_params(snv_spectra, property_scaler, 
                             save_path=os.path.join(ROOT_DIR, 'model', 'svr', 'preprocessing_params.json'),
                             pca=pca)
    
    # 分割训练集和验证集
    train_ratio = 0.8
    n_samples = snv_feat_pca.shape[0]
    n_train = int(train_ratio * n_samples)
    
    # 随机打乱索引
    indices = np.random.permutation(n_samples)
    train_indices = indices[:n_train]
    val_indices = indices[n_train:]
    
    X_train = snv_feat_pca[train_indices]
    y_train = scaled_properties[train_indices]
    X_val = snv_feat_pca[val_indices]
    y_val = scaled_properties[val_indices]
    
    print(f"训练集大小: {X_train.shape[0]}")
    print(f"验证集大小: {X_val.shape[0]}")
    
    # 对特征数据进行标准化（SVR需要）
    feature_scaler = StandardScaler()
    X_train_scaled = feature_scaler.fit_transform(X_train)
    X_val_scaled = feature_scaler.transform(X_val)
    
    # 训练SVR模型
    svr_models = train_svr_models(X_train_scaled, y_train, X_val_scaled, y_val, property_labels)
    
    # 评估模型
    evaluate_svr_models(svr_models, X_val_scaled, y_val, property_labels, property_scaler)
    
    # 保存模型
    save_svr_models(svr_models, feature_scaler, model_dir)
    
    # 保存模型信息（包含选中特征索引）
    input_size = snv_feat_pca.shape[1]
    output_size = scaled_properties.shape[1]
    save_model_info(input_size, output_size, property_labels, wavelength_labels, selected_idx.tolist(),
                    save_path=os.path.join(ROOT_DIR, 'model', 'svr', 'model_info.json'))
    
    print("\n训练完成！")
    print("生成的文件:")
    print("- model/svr/Property2_model.pkl (SVR模型)")
    print("- model/svr/feature_scaler.pkl (特征标准化器)")
    print("- model/svr/model_info.json (模型信息)")
    print("- model/svr/preprocessing_params.json (预处理参数)")
    print("\n注意：模型使用SNV预处理的光谱数据和标准化后的属性数据进行训练")
    print("上位机需要使用相同的预处理参数进行预测")

if __name__ == "__main__":
    main()
