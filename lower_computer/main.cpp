/**
 * @file main.cpp
 * @brief 下位机服务器主程序入口
 * @details 创建Qt应用程序实例，初始化下位机服务器界面并启动事件循环
 * @author 系统设计项目组
 * @date 2024
 */

#include <QApplication>  // Qt应用程序类
#include "Server.h"      // 下位机服务器类

/**
 * @brief 下位机服务器主程序入口
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * @return 程序退出码
 * @details 创建Qt应用程序实例，初始化下位机服务器界面并启动事件循环
 */
int main(int argc, char *argv[])
{
    // 创建Qt应用程序实例
    QApplication app(argc, argv);
    
    // 创建下位机服务器实例
    LowerComputerServer server;
    
    // 显示服务器窗口
    server.show();
    
    // 启动Qt事件循环，程序将在此处阻塞直到退出
    return app.exec();
}

