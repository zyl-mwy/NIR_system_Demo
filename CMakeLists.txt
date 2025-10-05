# =============================================================================
# 光谱预测系统 - CMake构建配置
# =============================================================================
# 项目: 基于TCP通信的智能光谱预测系统
# 功能: 下位机(TCP服务器) + 上位机(TCP客户端) + LibTorch深度学习预测
# 作者: 系统设计项目组
# 日期: 2024
# =============================================================================

# 最低CMake版本要求
cmake_minimum_required(VERSION 3.16)

# 项目名称和版本
project(SpectrumPredictionSystem VERSION 1.0.0 LANGUAGES CXX)

# =============================================================================
# 编译器和C++标准配置
# =============================================================================
set(CMAKE_CXX_STANDARD 17)                    # 使用C++17标准
set(CMAKE_CXX_STANDARD_REQUIRED ON)           # 强制要求C++17
set(CMAKE_CXX_EXTENSIONS OFF)                 # 禁用编译器扩展

# 静态链接libstdc++以避免Anaconda环境中的版本冲突
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libstdc++")

# =============================================================================
# 依赖库配置
# =============================================================================

# 强制使用系统Qt5，避免选择到Conda/Anaconda Qt
set(Qt5_DIR "/usr/lib/x86_64-linux-gnu/cmake/Qt5" CACHE PATH "")

# 查找Qt5组件 (GUI界面和网络通信)
find_package(Qt5 REQUIRED COMPONENTS 
    Core      # Qt核心功能
    Network   # TCP网络通信
    Widgets   # GUI控件
    Charts    # 图表显示
    Sql       # 数据库
)

# 注意：加密功能使用Qt内置的加密算法，无需额外依赖

# 设置LibTorch路径 (深度学习预测)
set(LIBTORCH_PATH "/media/linxi-ice/other/科研/系统设计/c_libtorch_test/libtorch")
set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} ${LIBTORCH_PATH})
find_package(Torch REQUIRED)

# 设置LibTorch包含目录
include_directories(${TORCH_INCLUDE_DIRS})

# 启用Qt5的MOC自动处理 (Qt元对象编译器)
set(CMAKE_AUTOMOC ON)

# =============================================================================
# 下位机可执行文件 (TCP服务器)
# =============================================================================
add_executable(lower_computer
    lower_computer/main.cpp            # 下位机主程序
    lower_computer/Server.cpp          # TCP服务器实现
    lower_computer/Server.h            # 服务器头文件
    lower_computer/CryptoUtils.cpp     # 加密工具实现
    lower_computer/CryptoUtils.h       # 加密工具头文件
)

# 下位机链接库 (只需要基础Qt功能)
target_link_libraries(lower_computer
    Qt5::Core      # Qt核心功能
    Qt5::Network   # TCP网络通信
    Qt5::Widgets   # GUI界面
)

# =============================================================================
# 上位机可执行文件 (TCP客户端 + 光谱预测)
# =============================================================================
# 自动收集 upper_computer 下所有源文件
file(GLOB_RECURSE UPPER_COMP_SOURCES
    "upper_computer/*.cpp"
    "upper_computer/*.c"
    "upper_computer/*.cc"
    "upper_computer/*.cxx"
    "upper_computer/*.h"
    "upper_computer/*.hpp"
)

add_executable(upper_computer
    ${UPPER_COMP_SOURCES}
)

# 为上位机目标添加包含目录，适配新的目录结构
target_include_directories(upper_computer PRIVATE
    ${CMAKE_SOURCE_DIR}/upper_computer
    ${CMAKE_SOURCE_DIR}/upper_computer/basic
    ${CMAKE_SOURCE_DIR}/upper_computer/predictor/final_predictor/example
    ${CMAKE_SOURCE_DIR}/upper_computer/predictor/final_predictor/svr
    ${CMAKE_SOURCE_DIR}/upper_computer/predictor/basic
)

# 上位机链接库 (包含所有功能)
target_link_libraries(upper_computer
    Qt5::Core        # Qt核心功能
    Qt5::Network     # TCP网络通信
    Qt5::Widgets     # GUI界面
    Qt5::Charts      # 图表显示 (光谱曲线图、预测历史图)
    Qt5::Sql         # SQLite数据库
    ${TORCH_LIBRARIES} # LibTorch深度学习库
)

# =============================================================================
# 输出目录配置
# =============================================================================
# 设置可执行文件输出到项目根目录下的bin目录
set_target_properties(lower_computer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)

set_target_properties(upper_computer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)

# =============================================================================
# 安装规则 (可选)
# =============================================================================
install(TARGETS lower_computer upper_computer
    RUNTIME DESTINATION bin
)

# =============================================================================
# 构建信息输出
# =============================================================================
message(STATUS "==========================================")
message(STATUS "光谱预测系统构建配置")
message(STATUS "==========================================")
message(STATUS "项目: ${PROJECT_NAME} v${PROJECT_VERSION}")
message(STATUS "C++标准: ${CMAKE_CXX_STANDARD}")
message(STATUS "Qt5版本: ${Qt5_VERSION}")
message(STATUS "LibTorch路径: ${LIBTORCH_PATH}")
message(STATUS "输出目录: ${CMAKE_SOURCE_DIR}/bin")
message(STATUS "==========================================")
