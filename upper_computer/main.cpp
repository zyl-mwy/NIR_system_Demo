/**
 * @file main.cpp
 * @brief 上位机客户端主程序入口
 * @details 创建Qt应用程序实例，初始化上位机客户端界面并启动事件循环
 * @author 系统设计项目组
 * @date 2024
 */

#include <QApplication>  // Qt应用程序类
#include <QMetaType>     // Qt元类型系统
#include <QLoggingCategory> // Qt日志分类系统
#include "Client.h"      // 上位机客户端类

/**
 * @brief 上位机客户端主程序入口
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * @return 程序退出码
 * @details 创建Qt应用程序实例，初始化上位机客户端界面并启动事件循环
 */
int main(int argc, char *argv[])
{
    // 禁用Qt调试输出，避免终端显示调试信息
    QLoggingCategory::setFilterRules("*.debug=false");
    
    // 创建Qt应用程序实例
    QApplication app(argc, argv);
    
    // 注册std::vector<float>类型到Qt元对象系统
    qRegisterMetaType<std::vector<float>>("std::vector<float>");
    
    // 注册QMap<QString,float>类型到Qt元对象系统
    qRegisterMetaType<QMap<QString,float>>("QMap<QString,float>");
    
    // 创建上位机客户端实例
    UpperComputerClient client;
    
    // 显示客户端窗口
    client.show();
    
    // 启动Qt事件循环，程序将在此处阻塞直到退出
    return app.exec();
}
