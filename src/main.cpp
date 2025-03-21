#include <iostream>
#include <iomanip>
#include <string>
#include <chrono>
#include <thread>
#include <csignal>
#include <atomic>
#include <filesystem>

#include "magnetparser/magnetparser.h"
#include "magnetdownloader/magnetdownloader.h"

// 全局变量用于信号处理
std::atomic<bool> g_running = true;

// 信号处理函数
void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", exiting..." << std::endl;
    g_running = false;
}

// 格式化文件大小为人类可读格式
std::string format_size(int64_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit_index = 0;
    double size = static_cast<double>(bytes);
    
    while (size >= 1024.0 && unit_index < 4) {
        size /= 1024.0;
        unit_index++;
    }
    
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << size << " " << units[unit_index];
    return ss.str();
}

// 格式化速度为人类可读格式
std::string format_speed(float bytes_per_sec) {
    return format_size(static_cast<int64_t>(bytes_per_sec)) + "/s";
}

// 创建进度条
std::string create_progress_bar(float progress, int width = 50) {
    int pos = static_cast<int>(width * progress);
    std::string bar = "[";
    
    for (int i = 0; i < width; ++i) {
        if (i < pos) bar += "=";
        else if (i == pos) bar += ">";
        else bar += " ";
    }
    
    bar += "] " + std::to_string(static_cast<int>(progress * 100.0)) + "%";
    return bar;
}

// 下载进度回调函数
void progress_callback(const DownloadProgress& progress) {
    // 清除当前行
    std::cout << "\r\033[K";
    
    // 根据状态显示不同信息
    switch (progress.status) {
        case DownloadStatus::CONNECTING:
            std::cout << "Connecting to peers... ";
            break;
            
        case DownloadStatus::METADATA:
            std::cout << "Downloading metadata... ";
            break;
            
        case DownloadStatus::DOWNLOADING:
            // 显示下载进度
            std::cout << progress.filename << " | " 
                      << create_progress_bar(progress.progress) << " | "
                      << format_size(progress.downloaded) << " of " 
                      << format_size(progress.total_size) << " | "
                      << format_speed(progress.download_speed) << " | "
                      << "Seeds: " << progress.seeds 
                      << " Peers: " << progress.peers;
            break;
            
        case DownloadStatus::SEEDING:
            std::cout << "Seeding | Upload: " 
                      << format_speed(progress.upload_speed) 
                      << " | Seeds: " << progress.seeds 
                      << " Peers: " << progress.peers;
            break;
            
        case DownloadStatus::PAUSED:
            std::cout << "Paused | " 
                      << format_size(progress.downloaded) << " of " 
                      << format_size(progress.total_size) 
                      << " (" << static_cast<int>(progress.progress * 100.0) << "%)";
            break;
            
        case DownloadStatus::COMPLETED:
            std::cout << "Download completed | " 
                      << format_size(progress.total_size) 
                      << " | Seeding at " 
                      << format_speed(progress.upload_speed);
            break;
            
        case DownloadStatus::ERROR:
            std::cout << "Error: " << progress.error_message;
            break;
    }
    
    // 刷新输出
    std::cout.flush();
}

// 主函数
int main(int argc, char* argv[]) {
    // 设置信号处理
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    // 检查命令行参数
    if (argc < 2) {
        std::cout << "使用方法: " << argv[0] << " <磁力链接> [保存路径]" << std::endl;
        std::cout << "示例: " << argv[0] << " \"magnet:?xt=urn:btih:...\" ./downloads" << std::endl;
        return 1;
    }
    
    // 获取磁力链接
    std::string magnet_uri = argv[1];
    
    // 获取保存路径（如有）
    std::filesystem::path save_path = "./downloads";
    if (argc > 2) {
        save_path = argv[2];
    }
    
    // 确保保存路径存在
    std::filesystem::create_directories(save_path);
    
    try {
        // 打印欢迎信息
        std::cout << "MagnetDownloader - 简单的磁力链接下载工具" << std::endl;
        std::cout << "----------------------------------------" << std::endl;
        std::cout << "磁力链接: " << magnet_uri << std::endl;
        std::cout << "保存路径: " << save_path << std::endl;
        std::cout << "按Ctrl+C停止下载" << std::endl;
        std::cout << "----------------------------------------" << std::endl;
        
        // 解析磁力链接
        MagnetParserStateMachine parser;
        if (!parser.parse(magnet_uri)) {
            std::cerr << "错误: 无法解析磁力链接" << std::endl;
            return 1;
        }
        
        // 打印解析信息
        std::cout << "Info Hash: " << parser.get_info_hash() << std::endl;
        std::cout << "名称: " << parser.get_display_name() << std::endl;
        std::cout << "Tracker数量: " << parser.get_trackers().size() << std::endl;
        std::cout << "----------------------------------------" << std::endl;
        
        // 创建下载管理器
        MagnetDownloader downloader(save_path, progress_callback);
        
        // 开始下载
        if (!downloader.start_download(parser)) {
            std::cerr << "错误: 无法开始下载" << std::endl;
            return 1;
        }
        
        // 等待下载完成或用户中断
        while (g_running) {
            auto status = downloader.get_status();
            
            // 如果下载出错或完成，退出循环
            if (status == DownloadStatus::ERROR) {
                std::cout << std::endl;
                std::cerr << "下载失败: " << downloader.get_progress().error_message << std::endl;
                break;
            }
            
            // 休眠一段时间
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        // 如果是用户中断，显示消息
        if (!g_running) {
            std::cout << std::endl;
            std::cout << "下载已中断。" << std::endl;
            
            // 如果下载进行中，询问是否保留已下载文件
            auto status = downloader.get_status();
            if (status == DownloadStatus::DOWNLOADING || 
                status == DownloadStatus::METADATA ||
                status == DownloadStatus::CONNECTING) {
                
                std::cout << "是否删除已下载的文件？(y/n): ";
                char response;
                std::cin >> response;
                
                if (response == 'y' || response == 'Y') {
                    downloader.cancel(true);
                    std::cout << "已删除下载的文件。" << std::endl;
                } else {
                    downloader.cancel(false);
                    std::cout << "已保留下载的文件。" << std::endl;
                }
            }
        }
        
        std::cout << "程序退出" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "发生错误: " << e.what() << std::endl;
        return 1;
    }
}