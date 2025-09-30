# 光谱预测系统 - 下位机与上位机

这是一个基于C++、Qt5和LibTorch的智能光谱预测系统，包含下位机（TCP服务器）和上位机（TCP客户端），支持实时光谱数据采集、处理和属性预测。

## 项目结构

```
c_system/
├── lower_computer/          # 下位机（TCP服务器）
│   ├── main.cpp            # 下位机主程序
│   ├── Server.cpp          # TCP服务器实现
│   ├── Server.h            # 服务器头文件
│   └── CMakeLists.txt      # 下位机构建配置
├── upper_computer/         # 上位机（TCP客户端）
│   ├── main.cpp            # 上位机主程序
│   ├── Client.cpp          # TCP客户端实现
│   ├── Client.h            # 客户端头文件
│   ├── SpectrumPredictor.cpp # 光谱预测器实现
│   ├── SpectrumPredictor.h   # 光谱预测器头文件
│   ├── LibTorchPredictor.cpp # LibTorch预测器实现
│   ├── LibTorchPredictor.h   # LibTorch预测器头文件
│   ├── PredictionWorker.cpp  # 预测工作线程实现
│   ├── PredictionWorker.h    # 预测工作线程头文件
│   └── CMakeLists.txt      # 上位机构建配置
├── model/                  # 模型文件目录
│   ├── spectrum_model.jit  # TorchScript模型文件
│   ├── spectrum_model.pth  # PyTorch模型权重
│   ├── spectrum_best.pth   # 最佳模型权重
│   ├── model_info.json     # 模型信息文件
│   ├── property_scaler.pkl # 属性标准化器
│   └── spectrum_scaler.pkl # 光谱标准化器
├── data/                   # 数据文件目录
│   ├── spectrum/           # 光谱数据文件
│   ├── calibration/        # 校准数据文件
│   ├── diesel_prop.csv     # 柴油属性数据
│   └── diesel_spec.csv     # 柴油光谱数据
├── bin/                    # 可执行文件目录
│   ├── lower_computer      # 下位机可执行文件
│   └── upper_computer      # 上位机可执行文件
├── spectrum_model.py       # Python模型训练脚本
├── CMakeLists.txt          # 主构建配置
└── README.md              # 说明文档
```

> 说明：构建系统已改为自动收集 `upper_computer/` 目录下的全部源文件（使用 CMake `GLOB_RECURSE`）。新增/删除上位机文件无需修改 `CMakeLists.txt`。

## 架构总览

```
┌────────────┐        TCP(JSON/命令)        ┌──────────────┐
│ 下位机(服务器) │ <──────────────────────────> │ 上位机(客户端) │
└────┬───────┘                                 └───────┬──────┘
     │ 采集/读取数据                                   │ GUI与计算
     │                                                  │
     ▼                                                  ▼
  传感器/CSV → 流式光谱/状态 → 网络 → 解析 → 预处理 → 图表/预测 → 历史/告警/存储
```

- **下位机（lower_computer）**：提供 TCP 服务，按指令发送设备状态与光谱数据，支持整帧与流式。
- **上位机（upper_computer）**：Qt GUI，负责接收解析、图表展示、实时预测（LibTorch）、质量监控、历史、阈值告警等。
- **模型（model/）**：部署用 TorchScript 模型与信息文件。

### 关键模块（上位机）
- `Client.cpp/.h`：核心 UI、网络、数据流与图表逻辑。
- `SpectrumPredictor.* / LibTorchPredictor.*`：推理适配与性能优化。
- `PredictionWorker.*`：异步预测线程，避免 UI 阻塞。
- `Database.*`：结果与历史持久化（可选）。
- `ZoomableChartView.h`：图表交互（缩放/平移/双击弹窗）。

### 数据流与信号
- TCP 收包 → JSON 解析 → `updateSensorData` → 分发到：
  - 光谱整帧：`updateSpectrumChart`
  - 光谱流点：`updateSpectrumDataPoint`
  - 状态/心跳：状态面板与日志
- 预测：新光谱数据触发 `performAutoPrediction`，后台线程产出结果回主线程渲染。

## 实时图表与弹窗刷新策略

- 主图表（光谱曲线、实时预测柱状、历史趋势）实时更新。
- 图表“可双击”弹出独立非模态窗口，便于放大观察。
- 为保证弹窗与主图同步：
  - 监听源图表系列变化（`QXYSeries::pointsReplaced/pointAdded/pointRemoved` 等）并触发同步。
  - 光谱弹窗额外启用兜底定时器，默认 50ms（`Qt::PreciseTimer`）触发一次同步，避免系列重建或信号偶发丢失导致的短暂不同步。
- 预测柱状图弹窗：
  - 采用“自绘”数值标签，强制黑色，支持居中显示在柱体内部；
  - 主题固定为浅色，并适度提亮柱填充，提高可读性。

> 如需改动弹窗刷新策略（完全靠信号或不同刷新间隔），可在 `Client.cpp::openChartInWindow` 中调整。

### 实时统计窗口（物质统计弹窗）增强

- 点击顶部任一物质按钮可打开该物质的“实时统计”弹窗。
- 新增显示项：
  - “该物质异常次数”：边沿触发计数（正常→异常时 +1）。
  - “该物质检测总次数”：每次预测完成 +1。
  - “光谱质量异常累计次数”：全局性的质量不过关累计值（与具体物质无关）。
- 新增操作按钮：
  - “清除历史异常”：仅清空当前物质的历史异常计数（橙色历史状态）。
  - “清除光谱异常计数”：清空全局“光谱质量异常累计次数”。

### 顶部状态栏与按钮状态说明

- “已连接”指示：连接状态显示。
- “检测状态”标签：仅用于状态显示（检测/不在检测），不控制逻辑。
- 物质按钮颜色（统一样式，与“已连接”一致的扁平风格）：
  - 绿色：当前正常
  - 红色：当前异常
  - 橙色：曾经异常但当前已恢复（历史异常）
  - 蓝色：暂停/未在检测
  - 灰色：未连接
  - 紫色：光谱质量不达标（全局质量失败提示色，不与上述颜色冲突）

> 注：橙色由“历史异常计数”决定；点击统计弹窗中的“清除该物质异常统计”可清空该物质历史异常计数从而清除橙色状态（若当前也不异常则回到绿色）。紫色由“光谱质量不达标”触发，清空“光谱质量异常计数”或质量恢复后会根据当前状态回退到其它颜色。

### 质量阈值处理与弹窗策略（防多弹）

- 每次预测前会预检查全局“光谱质量异常累计次数”，若已达 `anomalyLimit`：
  1) 先发送 `STOP_SPECTRUM_STREAM` 给下位机，停止流；
  2) 再弹出一次告警窗口；
  3) 本次预测终止。
- 在单次质量失败导致累计达到阈值时，同样遵循“先停止流再弹窗”的顺序。
- 为避免多次弹窗，系统使用单次告警标志，仅在达到阈值的首次触发时显示；当重新开始流或清除全局质量异常计数时，该标志会被重置，后续再次达到阈值会重新弹一次。

## 快速开始（构建与运行）

### 1) 构建
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

### 2) 运行
```bash
# 终端1：下位机
./bin/lower_computer

# 终端2：上位机（GUI）
./bin/upper_computer
```

> 首次运行若遇到库路径问题，参见文末“GLIBCXX版本冲突解决方案”。

## 配置与路径

- 模型：`model/` 目录，使用 `model_info.json` 与 `spectrum_model.jit`。
- 数据：`data/` 目录（`spectrum/` 光谱、`calibration/` 校准、`diesel_spec.csv` 等）。
- 阈值配置：`config/thresholds.json`（属性上下限与告警阈值）。
- 日志输出：`bin/logs/` 与项目根 `logs/`。

### 阈值配置（thresholds.json）

支持两类配置：

- `thresholds`：各物质预测值的正常范围 `{min, max}`（用于顶部物质按钮的绿/红/橙状态）
- `quality_limits`：光谱质量监控阈值（决定是否对当前光谱执行预测）

字段说明：

- `quality_limits.snrMin`：信噪比下限，当前实现为 `(max − min) / 标准差`，需 ≥ snrMin
- `quality_limits.baselineMax`：基线指标上限，为 `|末端均值 − 起始均值| / 标准差`，需 ≤ baselineMax
- `quality_limits.integrityMin`：数据完整性下限（0~1），为 `有效点数 / 总点数`，需 ≥ integrityMin
- `quality_limits.anomalyLimit`：光谱质量“不达标”累计次数上限；达到/超过后将自动停止流

示例：

```json
{
  "thresholds": {
    "BP50": { "min": 500, "max": 600 },
    "CN": { "min": 40, "max": 70 },
    "D4052": { "min": 0.78, "max": 0.88 },
    "FLASH": { "min": 40, "max": 100 },
    "FREEZE": { "min": -60, "max": 5 },
    "TOTAL": { "min": 10, "max": 50 },
    "VISC": { "min": 1.0, "max": 5.0 }
  },
  "quality_limits": {
    "snrMin": 5.0,
    "baselineMax": 10.0,
    "integrityMin": 0.95,
    "anomalyLimit": 100
  }
}
```

行为说明：

- 当任一质量指标未达标（snr/baseline/integrity 任一失败）时：
  - 跳过本次预测（不触发推理）
  - 顶部物质按钮统一显示为“紫色”以提示质量异常
  - 全局计数 `光谱质量异常累计次数` +1
- 当累计次数 ≥ `quality_limits.anomalyLimit` 时：
  - 弹出提示框告知已达阈值
  - 自动向下位机发送 `STOP_SPECTRUM_STREAM`，停止流与检测

## 常见问题（补充）

1) 双击弹窗不刷新/延迟刷新？
   - 已启用 50ms 兜底定时器与系列信号双路径同步；若需更高刷新率，可改为 20ms（增加 CPU 占用）。

2) 柱状图数字颜色/位置不可见？
   - 已强制黑色文本并浅色主题，且自绘定位在柱体中心；仍不可见时检查主题与对比度是否被外部样式覆盖。

3) 新增上位机源码后构建失败？
   - 现已自动收集 `upper_computer/` 源文件；确认新文件扩展名与路径符合 GLOB 规则。

4) 光谱流式数据未绘制全？
   - `updateSpectrumDataPoint` 会累积数据点并刷新标题/坐标轴；若切换文件会自动重置累积。

5) 预测线程阻塞界面？
   - 预测在 `PredictionWorker` 中异步执行；若 UI 卡顿，检查额外重负的同步操作或磁盘写入。

## 功能特性

### 下位机（TCP服务器）
- **图形界面**: 基于Qt5的现代化GUI界面
- **端口监听**: 可配置端口，支持多客户端连接
- **传感器数据模拟**: 可配置间隔的自动数据发送
- **光谱文件读取**: 自动读取data文件夹中的光谱文件
- **自动加载**: 启动时自动加载diesel_spec.csv文件
- **指令驱动**: 默认不自动发送数据，等待上位机指令
- **光谱数据解析**: 支持CSV和TXT格式，按指定格式解析光谱数据
- **流式发送**: 支持逐行发送光谱数据，每50ms发送一行
- **命令处理**: 支持多种客户端命令
- **JSON数据格式**: 使用JSON格式传输结构化数据
- **连接管理**: 实时显示客户端列表和连接状态
- **数据监控**: 实时显示传感器数据和通信日志
- **服务器控制**: 图形化启动/停止服务器

### 上位机（TCP客户端）
- **图形界面**: 基于Qt5的现代化GUI界面
- **实时数据显示**: 表格形式显示传感器数据和光谱数据
- **光谱数据接收**: 支持接收和显示光谱文件数据
- **光谱数据实时显示**: 专门的光谱数据显示区域
  - 光谱信息显示：文件名称、数据点数、接收时间
  - 光谱数据表格：前50个数据点的波长-光谱值对应表
  - 光谱图表区域：显示波长范围、光谱值范围、数据点数统计
  - 实时更新：接收到新数据时自动更新所有显示
- **流式数据接收**: 支持逐行接收和显示光谱数据
  - 实时显示当前数据点信息
  - 流式接收状态指示
  - 循环发送支持
- **智能光谱预测**: 基于LibTorch的深度学习预测功能
  - **实时预测**: 自动对接收的光谱数据进行属性预测
  - **多属性预测**: 支持多个物理化学属性的同时预测
  - **预测历史**: 实时显示预测结果的历史趋势图表
  - **预测结果表格**: 以表格形式显示当前预测结果
  - **预测状态监控**: 实时显示预测状态和进度
- **光谱质量监控**: 光谱数据质量评估和监控
  - **质量指标**: 信噪比、基线稳定性、峰形质量等
  - **质量图表**: 实时显示质量指标变化趋势
  - **质量报警**: 质量指标异常时自动报警
- **光谱校准功能**: 光谱数据校准和预处理
  - **暗电流校准**: 自动应用暗电流校准
  - **白参考校准**: 自动应用白参考校准
  - **预处理选项**: 平滑、标准化、基线校正、导数等
- **可调整界面**: 支持鼠标拖拽调整所有组件大小
- **指令驱动**: 按需请求光谱数据和传感器数据
- **命令发送**: 支持向服务器发送各种命令
- **连接管理**: 可视化连接状态和日志
- **数据历史**: 记录通信历史和命令历史

## 系统要求

- **操作系统**: Linux (推荐Ubuntu 18.04+)
- **编译器**: GCC 7.0+ 或 Clang 5.0+
- **Qt版本**: Qt5.12+
- **CMake**: 3.16+
- **LibTorch**: 1.12+ (用于深度学习预测)
- **Python**: 3.8+ (用于模型训练)
- **PyTorch**: 1.12+ (用于模型训练)
- **依赖库**: Qt5 Core, Network, Widgets, Charts, LibTorch

## 安装依赖

### Ubuntu/Debian
```bash
# 基础开发工具
sudo apt update
sudo apt install build-essential cmake qt5-default qtbase5-dev

# Python和PyTorch (用于模型训练)
sudo apt install python3 python3-pip
pip3 install torch torchvision torchaudio --index-url https://download.pytorch.org/whl/cpu
pip3 install numpy pandas scikit-learn matplotlib

# LibTorch (用于C++预测)
# 下载LibTorch CPU版本
wget https://download.pytorch.org/libtorch/cpu/libtorch-cxx11-abi-shared-with-deps-2.0.1%2Bcpu.zip
unzip libtorch-cxx11-abi-shared-with-deps-2.0.1+cpu.zip
# 将libtorch目录移动到项目目录下
```

### CentOS/RHEL
```bash
# 基础开发工具
sudo yum install gcc-c++ cmake3 qt5-qtbase-devel

# Python和PyTorch
sudo yum install python3 python3-pip
pip3 install torch torchvision torchaudio --index-url https://download.pytorch.org/whl/cpu
pip3 install numpy pandas scikit-learn matplotlib
```

## 模型训练

### 1. 训练光谱预测模型
在运行系统之前，需要先训练深度学习模型：

```bash
# 训练模型（生成.jit文件供C++使用）
python3 spectrum_model.py
```

**训练完成后将生成以下文件**:
- `model/spectrum_model.jit` - TorchScript模型文件（C++使用）
- `model/spectrum_model.pth` - PyTorch模型权重
- `model/spectrum_best.pth` - 最佳模型权重
- `model/model_info.json` - 模型信息文件
- `model/property_scaler.pkl` - 属性标准化器
- `model/spectrum_scaler.pkl` - 光谱标准化器

**注意**: 确保 `data/` 目录下有训练数据文件：
- `diesel_prop.csv` - 柴油属性数据
- `diesel_spec.csv` - 柴油光谱数据

### 2. 模型架构和技术细节

**神经网络架构**:
```python
class SpectrumPredictor(nn.Module):
    def __init__(self, input_size=400, hidden_sizes=[512, 256, 128, 64], output_size=7):
        # 输入层: 400个光谱特征
        # 隐藏层: 512 -> 256 -> 128 -> 64 (每层包含BatchNorm + ReLU + Dropout)
        # 输出层: 7个属性预测值
```

**训练配置**:
- **批次大小**: 16
- **学习率**: 0.001
- **训练轮数**: 200（早停机制）
- **优化器**: Adam
- **损失函数**: MSE
- **数据量**: 783个样本

**数据格式**:
- **光谱数据**: 400个波长点的强度值（750-1550nm）
- **属性数据**: 多个化学属性（密度、粘度、硫含量等）

## 构建和运行

### 1. 构建项目
```bash
# 使用构建脚本（推荐）
./build.sh

# 或手动构建
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### 2. 运行系统

**步骤1: 启动下位机**
```bash
# 推荐使用启动脚本（自动解决库版本冲突）
./run_lower.sh

# 或直接运行（可能需要设置库路径）
./bin/lower_computer
```
下位机将启动TCP服务器，监听8888端口。

**步骤2: 启动上位机**
```bash
# 推荐使用启动脚本（自动解决库版本冲突）
./run_upper.sh

# 或直接运行（可能需要设置库路径）
./bin/upper_computer
```
上位机将显示图形界面，点击"连接"按钮连接到下位机。

## 使用说明

### 下位机功能
- **服务器控制**: 图形化启动/停止TCP服务器
- **端口配置**: 可自定义监听端口
- **自动数据发送**: 可配置发送间隔（1-60秒）
- **客户端管理**: 实时显示连接的客户端列表
- **数据监控**: 实时显示传感器数据表格
- **通信日志**: 显示所有通信记录
- **命令处理**: 处理客户端命令并响应

### 上位机功能
- **连接设置**: 配置服务器地址和端口
- **实时监控**: 查看传感器数据（温度、湿度、气压等）
- **光谱数据显示**: 实时显示光谱曲线图和光谱数据表格
- **智能预测**: 自动对光谱数据进行属性预测
  - 实时预测结果表格显示
  - 预测历史趋势图表
  - 预测状态监控
- **光谱质量监控**: 监控光谱数据质量指标
- **光谱校准**: 应用暗电流和白参考校准
- **预处理选项**: 光谱数据预处理（平滑、标准化等）
- **界面调整**: 鼠标拖拽调整各组件大小
- **命令发送**: 发送控制命令给下位机
- **日志查看**: 查看通信日志和命令历史

### 支持的命令
- `GET_STATUS`: 获取下位机状态
- `GET_VERSION`: 获取下位机版本
- `RESTART`: 重启下位机
- `START_DATA`: 开始数据发送
- `STOP_DATA`: 停止数据发送
- `GET_SPECTRUM`: 获取光谱数据（401个波长点）
- `GET_SPECTRUM_STREAM`: 开始流式发送光谱数据（逐行发送）
- `STOP_SPECTRUM_STREAM`: 停止流式发送光谱数据
- `GET_SENSOR_DATA`: 获取传感器数据（温度、湿度、气压）

## 数据格式

### 传感器数据（JSON格式）
```json
{
    "timestamp": "2024-01-01 12:00:00",
    "type": "sensor_data",
    "temperature": 25.5,
    "humidity": 65.2,
    "pressure": 1013.8,
    "status": "normal"
}
```

### 光谱数据（JSON格式）
```json
{
    "timestamp": "2024-01-01 12:00:00",
    "type": "spectrum_data",
    "file_name": "spectrum_sample.txt",
    "data_points": 61,
    "wavelengths": [400, 410, 420, ...],
    "spectrum_values": [0.85, 0.87, 0.89, ...]
}
```

### 光谱数据点（流式发送）
```json
{
    "timestamp": "2024-01-01 12:00:00",
    "type": "spectrum_data_point",
    "file_name": "diesel_spec.csv",
    "index": 0,
    "total_points": 401,
    "wavelength": 750.0,
    "spectrum_value": 0.85
}
```

### 预测结果数据格式（JSON格式）
```json
{
    "timestamp": "2024-01-01 12:00:00",
    "type": "prediction_result",
    "spectrum_file": "diesel_spec.csv",
    "predictions": {
        "density": 0.85,
        "viscosity": 4.2,
        "sulfur_content": 0.05,
        "flash_point": 65.0,
        "cetane_number": 52.3
    },
    "confidence": 0.92,
    "processing_time": 0.15
}
```

### 光谱文件格式
光谱文件应放置在`data/`目录下，支持`.txt`、`.dat`、`.csv`格式：

**TXT格式：**
```
# 光谱数据文件
# 第10行第3列开始：波段信息
# 第11行第3列开始：光谱数据
# 
# 波长(nm)	反射率(%)	备注
400	0.85	紫外波段
410	0.87	
420	0.89	
...
```

**CSV格式（如diesel_spec.csv）：**
```
# 第10行：波长信息（从第3列开始）
# 第11行开始：光谱数据（每行第3列开始）
# 自动选择第一行光谱数据发送
```

**自动加载功能：**
- 下位机启动时自动加载`diesel_spec.csv`文件
- 自动解析401个波长点（750-1550nm）
- 自动发送光谱数据到上位机

### 命令响应
- 成功响应: 返回相应的状态信息
- 错误响应: 返回错误描述

## 网络配置

- **默认端口**: 8888
- **协议**: TCP/IP
- **数据格式**: JSON + 文本命令
- **编码**: UTF-8

## 故障排除

### 常见问题

1. **编译错误**: 确保安装了Qt5开发包和LibTorch
2. **连接失败**: 检查端口是否被占用，防火墙设置
3. **数据不显示**: 确保下位机正在运行且已连接
4. **模型加载失败**: 确保模型文件存在且路径正确
   - 检查 `model/spectrum_model.jit` 文件是否存在
   - 检查 `model/model_info.json` 文件是否存在
   - 确保LibTorch版本兼容
5. **预测功能不工作**: 检查模型文件完整性
   - 重新训练模型: `python3 spectrum_model.py`
   - 检查数据文件: 确保 `data/` 目录下有训练数据
6. **预测精度问题**: 模型性能参考
   - 当前模型在验证集上的表现（仅供参考）:
     - 属性230: MSE = 17146.68, R² = -0.027
     - 属性55.1: MSE = 598.35, R² = 0.006
     - 属性1.98: MSE = 0.18, R² = -0.026
   - 如需提高精度，可尝试调整网络架构或增加训练数据
6. **GLIBCXX版本冲突**: 如果遇到 `GLIBCXX_3.4.32 not found` 错误，请使用以下解决方案：

#### GLIBCXX版本冲突解决方案

**问题描述**: 运行程序时出现 `GLIBCXX_3.4.32 not found` 错误，这通常是由于 Anaconda 环境中的 libstdc++ 版本较旧导致的。

**解决方案1: 使用启动脚本（推荐）**
```bash
# 使用提供的启动脚本
./run_lower.sh    # 启动下位机
./run_upper.sh    # 启动上位机
```

**解决方案2: 手动指定库路径**
```bash
# 下位机
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu ./bin/lower_computer

# 上位机
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu ./bin/upper_computer
```

**解决方案3: 重新编译（已自动配置）**
项目已配置为静态链接 libstdc++，重新编译后应该可以避免版本冲突：
```bash
./build.sh
```

**解决方案4: 环境变量设置**
```bash
# 临时设置
export LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH

# 永久设置（添加到 ~/.bashrc）
echo 'export LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH' >> ~/.bashrc
source ~/.bashrc
```

### 调试模式
```bash
# 启用调试输出
export QT_LOGGING_RULES="*=true"
./build/bin/lower_computer
./build/bin/upper_computer
```

## 测试脚本

项目包含多个测试脚本，用于验证不同功能：

- `test_connection.sh` - 基本连接测试
- `test_gui.sh` - GUI功能测试
- `test_system.sh` - 系统综合测试
- `test_spectrum.sh` - 光谱数据传输测试
- `test_diesel_auto.sh` - 自动加载diesel_spec.csv测试
- `test_command_driven.sh` - 指令驱动模式测试
- `test_spectrum_display.sh` - 光谱数据实时显示测试
- `test_spectrum_stream.sh` - 光谱数据逐行发送测试

### 使用方法
```bash
# 运行特定测试
./test_spectrum_display.sh

# 运行所有测试
for script in test_*.sh; do
    echo "运行 $script"
    ./$script
done
```

## 扩展功能

### 已实现的高级功能
- **深度学习预测**: 基于LibTorch的实时光谱属性预测
- **多属性预测**: 同时预测多个物理化学属性
- **预测历史追踪**: 实时显示预测结果的历史趋势
- **光谱质量监控**: 自动评估光谱数据质量
- **光谱校准**: 暗电流和白参考校准
- **数据预处理**: 多种光谱数据预处理选项
- **可调整界面**: 支持鼠标拖拽调整所有组件
- **实时图表**: 光谱曲线图和预测历史图表

### 可以添加的功能
- **数据存储**: 将预测结果存储到数据库
- **报警系统**: 预测结果异常时自动报警
- **配置文件**: 支持配置文件自定义参数
- **多语言支持**: 界面多语言切换
- **数据加密**: 网络传输数据加密
- **模型更新**: 在线模型更新功能
- **批量预测**: 支持批量光谱数据预测
- **预测报告**: 自动生成预测报告
- **数据导出**: 支持预测结果导出

### 技术改进建议
- **模型优化**: 尝试不同的网络架构和训练策略
- **预处理增强**: 添加SNV标准化、基线校正等预处理方法
- **特征工程**: 提取更多光谱特征（导数、积分等）
- **数据增强**: 增加训练数据量和数据质量
- **实时性能**: 优化C++预测代码的性能
- **模型集成**: 使用集成学习方法提高预测精度

### 自定义功能
- **传感器数据**: 修改下位机中的`sensorData`对象来添加更多传感器类型
- **预测属性**: 修改训练数据来添加更多预测属性
- **预处理方法**: 在`spectrum_model.py`中添加更多预处理方法
- **界面布局**: 通过修改Qt布局代码自定义界面

## 许可证

本项目仅供学习和研究使用。

## 联系方式

如有问题或建议，请通过以下方式联系：
- 项目地址: [GitHub链接]
- 邮箱: [您的邮箱]

---

**注意**: 这是一个教学示例项目，在生产环境中使用前请进行充分的安全性和稳定性测试。
