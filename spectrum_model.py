#!/usr/bin/env python3
"""
光谱预测模型训练程序 - 基于PyTorch
不进行预处理，直接使用原始光谱数据进行训练
"""

import torch
import torch.nn as nn
import torch.utils.data as Data
import numpy as np
import pandas as pd
from sklearn.metrics import mean_squared_error, r2_score
import matplotlib.pyplot as plt
import os
import json
from typing import List, Tuple

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
    
    # 提取波长标签（第10行）
    wavelength_labels = spec_df.iloc[9, 2:].dropna().astype(str).tolist()
    
    # 提取光谱数据（从第11行开始，跳过前3列，移除最后一列NaN）
    spectra = spec_data[1:, 3:].astype(float)
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

def evaluate_model(model: nn.Module, val_loader: Data.DataLoader, property_labels: List[str]) -> None:
    """
    评估模型性能
    Args:
        model: 训练好的模型
        val_loader: 验证数据加载器
        property_labels: 属性标签列表
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
    
    # 计算评估指标
    print("\n模型性能评估:")
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
                   wavelength_labels: List[str], save_path: str = 'model/model_info.json'):
    """保存模型信息"""
    model_info = {
        'input_size': input_size,
        'output_size': output_size,
        'property_labels': property_labels,
        'wavelength_labels': wavelength_labels
    }
    
    with open(save_path, 'w') as f:
        json.dump(model_info, f, indent=2)
    
    print(f"模型信息已保存到: {save_path}")

def main():
    """主函数"""
    print("=== 光谱预测模型训练程序 ===")
    
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
    
    # 创建数据加载器
    train_loader, val_loader = create_data_loaders(spectra, properties, batch_size=16)
    
    # 创建模型
    input_size = spectra.shape[1]
    output_size = properties.shape[1]
    hidden_sizes = [512, 256, 128, 64]
    
    model = SpectrumPredictor(input_size, hidden_sizes, output_size)
    print(f"模型参数数量: {sum(p.numel() for p in model.parameters()):,}")
    
    # 训练模型
    train_losses, val_losses = train_model(model, train_loader, val_loader, epochs=200, learning_rate=0.001)
    
    # 加载最佳模型
    model.load_state_dict(torch.load('model/spectrum_best.pth'))
    
    # 评估模型
    evaluate_model(model, val_loader, property_labels)
    
    # 绘制训练历史
    plot_training_history(train_losses, val_losses)
    
    # 保存模型权重
    torch.save(model.state_dict(), 'model/spectrum_model.pth')
    print("PyTorch模型权重已保存为: model/spectrum_model.pth")
    
    # 转换为TorchScript格式
    convert_to_torchscript(model, input_size, 'model/spectrum_model.jit')
    
    # 保存模型信息
    save_model_info(input_size, output_size, property_labels, wavelength_labels)
    
    print("\n训练完成！")
    print("生成的文件:")
    print("- model/spectrum_model.pth (PyTorch模型权重)")
    print("- model/spectrum_model.jit (TorchScript模型)")
    print("- model/spectrum_best.pth (最佳模型权重)")
    print("- model/model_info.json (模型信息)")
    print("- training_history.png (训练历史图表)")

if __name__ == "__main__":
    main()
