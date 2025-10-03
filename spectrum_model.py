#!/usr/bin/env python3
"""
光谱预测模型训练程序 - 基于PyTorch

训练侧完整流水线（与上位机严格对齐）：
1) SNV：对每个样本做标准正态变量（SNV）标准化，消除强度偏置
2) VIP：使用PLS计算变量重要性（VIP），筛选出最重要的波段索引（selected_feature_indices）
3) PCA：在VIP后的子集上做降维，得到固定维度特征（n_components）
4) DNN：以PCA后的特征作为神经网络输入进行训练

导出文件：
- model_info.json：记录input_size、output_size、property_labels、wavelength_labels、selected_feature_indices
- preprocessing_params.json：记录SNV统计、StandardScaler参数、PCA均值与主成分

注意：若修改VIP的top_k或PCA的n_components，需重新训练并同步上述两个文件到上位机，保证推理一致。
"""

import torch
import torch.nn as nn
import torch.utils.data as Data
import numpy as np
import pandas as pd
from sklearn.metrics import mean_squared_error, r2_score
from sklearn.cross_decomposition import PLSRegression
from sklearn.decomposition import PCA
from sklearn.preprocessing import StandardScaler
import matplotlib.pyplot as plt
import os
import json
from typing import List, Tuple, Dict

class SpectrumPredictor(nn.Module):
    """光谱预测神经网络模型"""
    
    def __init__(self, input_size: int = 400, hidden_sizes: List[int] = [512, 256, 128, 64], output_size: int = 7):
        super(SpectrumPredictor, self).__init__()
        
        # 构建网络层
        layers = []
        prev_size = input_size
        
        for hidden_size in hidden_sizes:
            layers.extend([
                nn.Linear(prev_size, hidden_size),
                nn.BatchNorm1d(hidden_size),
                nn.ReLU(),
                nn.Dropout(0.2)
            ])
            prev_size = hidden_size
        
        # 输出层
        layers.append(nn.Linear(prev_size, output_size))
        
        self.network = nn.Sequential(*layers)
    
    def forward(self, x):
        return self.network(x)

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
    # print(spec_data)
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
                            save_path: str = 'model/preprocessing_params.json',
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

def create_data_loaders(spectra: np.ndarray, properties: np.ndarray, 
                       batch_size: int = 32, train_ratio: float = 0.8) -> Tuple[Data.DataLoader, Data.DataLoader]:
    """
    创建数据加载器
    Args:
        spectra: 光谱数据
        properties: 属性数据
        batch_size: 批次大小
        train_ratio: 训练集比例
    Returns:
        (训练数据加载器, 验证数据加载器)
    """
    # 转换为PyTorch张量
    X = torch.FloatTensor(spectra)
    y = torch.FloatTensor(properties)
    
    # 创建数据集
    dataset = Data.TensorDataset(X, y)
    
    # 分割训练集和验证集
    train_size = int(train_ratio * len(dataset))
    val_size = len(dataset) - train_size
    train_dataset, val_dataset = Data.random_split(dataset, [train_size, val_size])
    
    # 创建数据加载器
    train_loader = Data.DataLoader(train_dataset, batch_size=batch_size, shuffle=True)
    val_loader = Data.DataLoader(val_dataset, batch_size=batch_size, shuffle=False)
    
    print(f"训练集大小: {len(train_dataset)}")
    print(f"验证集大小: {len(val_dataset)}")
    
    return train_loader, val_loader

def train_model(model: nn.Module, train_loader: Data.DataLoader, val_loader: Data.DataLoader, 
                epochs: int = 100, learning_rate: float = 0.001) -> Tuple[List[float], List[float]]:
    """
    训练模型
    Args:
        model: 神经网络模型
        train_loader: 训练数据加载器
        val_loader: 验证数据加载器
        epochs: 训练轮数
        learning_rate: 学习率
    Returns:
        (训练损失列表, 验证损失列表)
    """
    device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
    model = model.to(device)
    
    optimizer = torch.optim.Adam(model.parameters(), lr=learning_rate, weight_decay=1e-5)
    criterion = nn.MSELoss()
    scheduler = torch.optim.lr_scheduler.ReduceLROnPlateau(optimizer, 'min', patience=10, factor=0.5)
    
    train_losses = []
    val_losses = []
    best_val_loss = float('inf')
    patience_counter = 0
    patience = 20
    
    print(f"使用设备: {device}")
    print("开始训练...")
    
    for epoch in range(epochs):
        # 训练阶段
        model.train()
        train_loss = 0.0
        for batch_x, batch_y in train_loader:
            batch_x, batch_y = batch_x.to(device), batch_y.to(device)
            
            optimizer.zero_grad()
            outputs = model(batch_x)
            loss = criterion(outputs, batch_y)
            loss.backward()
            optimizer.step()
            
            train_loss += loss.item()
        
        train_loss /= len(train_loader)
        
        # 验证阶段
        model.eval()
        val_loss = 0.0
        with torch.no_grad():
            for batch_x, batch_y in val_loader:
                batch_x, batch_y = batch_x.to(device), batch_y.to(device)
                outputs = model(batch_x)
                loss = criterion(outputs, batch_y)
                val_loss += loss.item()
        
        val_loss /= len(val_loader)
        
        train_losses.append(train_loss)
        val_losses.append(val_loss)
        
        scheduler.step(val_loss)
        
        # 早停机制
        if val_loss < best_val_loss:
            best_val_loss = val_loss
            patience_counter = 0
            # 保存最佳模型
            torch.save(model.state_dict(), 'model/spectrum_best.pth')
        else:
            patience_counter += 1
        
        if patience_counter >= patience:
            print(f"Early stopping at epoch {epoch+1}")
            break
        
        if (epoch + 1) % 10 == 0:
            print(f'Epoch [{epoch+1}/{epochs}], Train Loss: {train_loss:.6f}, Val Loss: {val_loss:.6f}')
    
    return train_losses, val_losses

def evaluate_model(model: nn.Module, val_loader: Data.DataLoader, property_labels: List[str], 
                   property_scaler: StandardScaler = None) -> None:
    """
    评估模型性能
    Args:
        model: 训练好的模型
        val_loader: 验证数据加载器
        property_labels: 属性标签列表
        property_scaler: 属性数据标准化器（用于反标准化）
    """
    device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
    model = model.to(device)
    model.eval()
    
    all_predictions = []
    all_targets = []
    
    with torch.no_grad():
        for batch_x, batch_y in val_loader:
            batch_x, batch_y = batch_x.to(device), batch_y.to(device)
            predictions = model(batch_x)
            
            all_predictions.append(predictions.cpu().numpy())
            all_targets.append(batch_y.cpu().numpy())
    
    # 合并所有预测结果
    predictions = np.vstack(all_predictions)
    targets = np.vstack(all_targets)
    
    # 反标准化到原始尺度
    if property_scaler is not None:
        predictions_original = property_scaler.inverse_transform(predictions)
        targets_original = property_scaler.inverse_transform(targets)
        
        print("\n=== 标准化后的预测结果（模型输出）===")
        print("预测值（标准化）:", predictions[:5])  # 显示前5个样本
        print("真实值（标准化）:", targets[:5])
        
        print("\n=== 反标准化后的预测结果（实际值）===")
        print("预测值（实际）:", predictions_original[:5])
        print("真实值（实际）:", targets_original[:5])
        
        # 使用反标准化后的数据计算评估指标
        print("\n模型性能评估（基于实际值）:")
        for i, label in enumerate(property_labels):
            mse = mean_squared_error(targets_original[:, i], predictions_original[:, i])
            r2 = r2_score(targets_original[:, i], predictions_original[:, i])
            print(f"{label}: MSE = {mse:.4f}, R² = {r2:.4f}")
    else:
        print("\n=== 预测结果（标准化数据）===")
        print("预测值:", predictions[:5])
        print("真实值:", targets[:5])
        
        # 使用标准化后的数据计算评估指标
        print("\n模型性能评估（基于标准化数据）:")
        for i, label in enumerate(property_labels):
            mse = mean_squared_error(targets[:, i], predictions[:, i])
            r2 = r2_score(targets[:, i], predictions[:, i])
            print(f"{label}: MSE = {mse:.4f}, R² = {r2:.4f}")

def plot_training_history(train_losses: List[float], val_losses: List[float], save_path: str = 'training_history.png'):
    """绘制训练历史"""
    plt.figure(figsize=(12, 4))
    
    plt.subplot(1, 2, 1)
    plt.plot(train_losses, label='Training Loss')
    plt.plot(val_losses, label='Validation Loss')
    plt.xlabel('Epoch')
    plt.ylabel('Loss')
    plt.title('Training and Validation Loss')
    plt.legend()
    plt.grid(True)
    
    plt.subplot(1, 2, 2)
    plt.plot(val_losses, label='Validation Loss', color='red')
    plt.xlabel('Epoch')
    plt.ylabel('Validation Loss')
    plt.title('Validation Loss (Zoomed)')
    plt.legend()
    plt.grid(True)
    
    plt.tight_layout()
    plt.savefig(save_path, dpi=300, bbox_inches='tight')
    plt.show()

def convert_to_torchscript(model: nn.Module, input_size: int, save_path: str = 'model/spectrum_model.jit'):
    """
    将PyTorch模型转换为TorchScript格式
    Args:
        model: 训练好的PyTorch模型
        input_size: 输入特征数量
        save_path: 保存路径
    """
    print("正在转换为TorchScript格式...")
    
    # 确保模型在CPU上
    device = torch.device('cpu')
    model = model.to(device)
    model.eval()
    
    # 创建示例输入（在CPU上）
    sample = torch.randn(1, input_size, device=device)
    
    # 使用torch.jit.trace进行转换
    try:
        traced_model = torch.jit.trace(model, sample)
        traced_model.save(save_path)
        print(f"TorchScript模型已保存到: {save_path}")
        
        # 验证转换后的模型
        loaded_model = torch.jit.load(save_path, map_location='cpu')
        test_output = loaded_model(sample)
        print(f"模型验证成功，输出形状: {test_output.shape}")
        
    except Exception as e:
        print(f"TorchScript转换失败: {e}")
        print("尝试使用torch.jit.script...")
        
        # 如果tracing失败，尝试使用scripting
        try:
            scripted_model = torch.jit.script(model)
            scripted_model.save(save_path)
            print(f"TorchScript模型已保存到: {save_path}")
            
            # 验证转换后的模型
            loaded_model = torch.jit.load(save_path, map_location='cpu')
            test_output = loaded_model(sample)
            print(f"模型验证成功，输出形状: {test_output.shape}")
            
        except Exception as e2:
            print(f"TorchScript scripting也失败: {e2}")

def save_model_info(input_size: int, output_size: int, property_labels: List[str], 
                   wavelength_labels: List[str], selected_feature_indices: List[int],
                   save_path: str = 'model/model_info.json'):
    """保存模型信息"""
    model_info = {
        'input_size': input_size,
        'output_size': output_size,
        'property_labels': property_labels,
        'wavelength_labels': wavelength_labels,
        'selected_feature_indices': selected_feature_indices
    }
    
    with open(save_path, 'w') as f:
        json.dump(model_info, f, indent=2)
    
    print(f"模型信息已保存到: {save_path}")

def main():
    """主函数"""
    print("=== 光谱预测模型训练程序（含SNV预处理）===")
    
    # 设置随机种子
    torch.manual_seed(42)
    np.random.seed(42)
    
    # 创建模型目录
    if not os.path.exists("model"):
        os.makedirs("model")
    
    # 加载数据
    spectrum_file = 'data/diesel_spec.csv'
    property_file = 'data/diesel_prop.csv'
    
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
    save_preprocessing_params(snv_spectra, property_scaler, pca=pca)
    
    # 创建数据加载器（使用PCA后的数据）
    train_loader, val_loader = create_data_loaders(snv_feat_pca, scaled_properties, batch_size=16)
    
    # 创建模型（使用PCA后的数据维度）
    input_size = snv_feat_pca.shape[1]
    output_size = scaled_properties.shape[1]
    hidden_sizes = [512, 256, 128, 64]
    
    model = SpectrumPredictor(input_size, hidden_sizes, output_size)
    print(f"模型参数数量: {sum(p.numel() for p in model.parameters()):,}")
    
    # 训练模型
    train_losses, val_losses = train_model(model, train_loader, val_loader, epochs=200, learning_rate=0.001)
    
    # 加载最佳模型
    model.load_state_dict(torch.load('model/spectrum_best.pth'))
    
    # 评估模型（传递标准化器以进行反标准化）
    evaluate_model(model, val_loader, property_labels, property_scaler)
    
    # 绘制训练历史
    plot_training_history(train_losses, val_losses)
    
    # 保存模型权重
    torch.save(model.state_dict(), 'model/spectrum_model.pth')
    print("PyTorch模型权重已保存为: model/spectrum_model.pth")
    
    # 转换为TorchScript格式
    convert_to_torchscript(model, input_size, 'model/spectrum_model.jit')
    
    # 保存模型信息（包含选中特征索引）
    save_model_info(input_size, output_size, property_labels, wavelength_labels, selected_idx.tolist())
    
    print("\n训练完成！")
    print("生成的文件:")
    print("- model/spectrum_model.pth (PyTorch模型权重)")
    print("- model/spectrum_model.jit (TorchScript模型)")
    print("- model/spectrum_best.pth (最佳模型权重)")
    print("- model/model_info.json (模型信息)")
    print("- model/preprocessing_params.json (预处理参数)")
    print("- training_history.png (训练历史图表)")
    print("\n注意：模型使用SNV预处理的光谱数据和标准化后的属性数据进行训练")
    print("上位机需要使用相同的预处理参数进行预测")

if __name__ == "__main__":
    main()
