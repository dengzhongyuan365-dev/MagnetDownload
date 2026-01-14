// MagnetDownload - Magnet Link Downloader
// Command Line Interface

#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>
#include <iomanip>

#ifdef _WIN32
#include <windows.h>
#endif

#include <asio.hpp>
#include "magnet/application/download_controller.h"
#include "magnet/utils/logger.h"
#include "magnet/version.h"

using namespace magnet;

// Windows console UTF-8 setup
void setupConsole() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    // Enable ANSI escape sequences
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
}

// Global variables
std::atomic<bool> g_running{true};
std::shared_ptr<application::DownloadController> g_controller;

// Signal handler
void signalHandler(int signal) {
    (void)signal;
    std::cout << "\n\n[!] Interrupt received, stopping download..." << std::endl;
    g_running = false;
    if (g_controller) {
        g_controller->stop();
    }
}

// Print help
void printHelp(const char* program) {
    std::cout << R"(
+--------------------------------------------------------------+
|              MagnetDownload - Magnet Link Downloader         |
|                    )" << magnet::Version::getVersionFull() << R"(                     |
+--------------------------------------------------------------+
|  Usage:                                                      |
|    magnetdownload <magnet_uri> [options]                     |
|                                                              |
|  Options:                                                    |
|    -o, --output <path>    Save path (default: current dir)   |
|    -c, --connections <n>  Max connections (default: 200)     |
|    -v, --verbose          Verbose output                     |
|    -h, --help             Show help                          |
|    --version              Show version information           |
|                                                              |
|  Example:                                                    |
|    magnetdownload "magnet:?xt=urn:btih:..." -o ./downloads   |
|                                                              |
|  Press Ctrl+C to stop download                               |
+--------------------------------------------------------------+
)" << std::endl;
}

// Print version information
void printVersion() {
    std::cout << magnet::Version::getCompleteInfo() << std::endl;
}

// 格式化文件大小
std::string formatSize(size_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit = 0;
    double size = static_cast<double>(bytes);
    
    while (size >= 1024 && unit < 4) {
        size /= 1024;
        unit++;
    }
    
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << size << " " << units[unit];
    return oss.str();
}

// 格式化时间
std::string formatTime(std::chrono::seconds secs) {
    if (secs.count() <= 0) return "--:--:--";
    if (secs.count() > 86400 * 7) return ">7 days";
    
    int hours = static_cast<int>(secs.count() / 3600);
    int mins = static_cast<int>((secs.count() % 3600) / 60);
    int s = static_cast<int>(secs.count() % 60);
    
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << hours << ":"
        << std::setfill('0') << std::setw(2) << mins << ":"
        << std::setfill('0') << std::setw(2) << s;
    return oss.str();
}

// Print progress bar
void printProgress(const application::DownloadProgress& progress) {
    const int bar_width = 40;
    double percent = progress.progressPercent();
    int filled = static_cast<int>(percent * bar_width / 100.0);
    
    std::cout << "\r";
    
    // Progress bar (ASCII)
    std::cout << "[";
    for (int i = 0; i < bar_width; ++i) {
        if (i < filled) std::cout << "=";
        else if (i == filled) std::cout << ">";
        else std::cout << " ";
    }
    std::cout << "] ";
    
    // Percentage
    std::cout << std::fixed << std::setprecision(1) << std::setw(5) << percent << "% ";
    
    // Speed
    std::cout << formatSize(static_cast<size_t>(progress.download_speed)) << "/s ";
    
    // Downloaded/Total
    std::cout << formatSize(progress.downloaded_size) << "/" << formatSize(progress.total_size) << " ";
    
    // ETA
    std::cout << "ETA: " << formatTime(progress.eta()) << " ";
    
    // Peers
    std::cout << "Peers: " << progress.connected_peers << "/" << progress.total_peers;
    
    std::cout << std::flush;
}

// Print state
void printState(application::DownloadState state) {
    std::cout << "\n[*] Status: ";
    switch (state) {
        case application::DownloadState::Idle:
            std::cout << "Idle";
            break;
        case application::DownloadState::ResolvingMetadata:
            std::cout << "Searching for peers...";
            break;
        case application::DownloadState::Downloading:
            std::cout << "Downloading";
            break;
        case application::DownloadState::Paused:
            std::cout << "Paused";
            break;
        case application::DownloadState::Verifying:
            std::cout << "Verifying";
            break;
        case application::DownloadState::Completed:
            std::cout << "Download completed!";
            break;
        case application::DownloadState::Failed:
            std::cout << "Download failed";
            break;
        case application::DownloadState::Stopped:
            std::cout << "Stopped";
            break;
    }
    std::cout << std::endl;
}

int main(int argc, char* argv[]) {
    // Setup console for UTF-8 on Windows
    setupConsole();
    
    // Setup signal handlers
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    // Parse command line arguments
    if (argc < 2) {
        printHelp(argv[0]);
        return 1;
    }
    
    std::string magnet_uri;
    std::string output_path = ".";
    size_t max_connections = 100;
    bool verbose = false;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            printHelp(argv[0]);
            return 0;
        } else if (arg == "--version") {
            printVersion();
            return 0;
        } else if (arg == "-o" || arg == "--output") {
            if (i + 1 < argc) {
                output_path = argv[++i];
            }
        } else if (arg == "-c" || arg == "--connections") {
            if (i + 1 < argc) {
                max_connections = std::stoul(argv[++i]);
            }
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else if (arg[0] != '-' && magnet_uri.empty()) {
            // 只有当magnet_uri还没有设置时，才设置第一个非选项参数为magnet_uri
            magnet_uri = arg;
        }
    }
    
    if (magnet_uri.empty()) {
        std::cerr << "[ERROR] Please provide a magnet link" << std::endl;
        printHelp(argv[0]);
        return 1;
    }
    
    // Setup logging
    if (verbose) {
        utils::Logger::instance().set_level(utils::LogLevel::Debug);
    } else {
        utils::Logger::instance().set_level(utils::LogLevel::Info);
    }
    utils::Logger::instance().set_console_output(verbose);
    
    std::cout << R"(
+--------------------------------------------------------------+
|              MagnetDownload - Magnet Link Downloader         |
|                    )" << magnet::Version::getVersionFull() << R"(                     |
+--------------------------------------------------------------+
)" << std::endl;
    
    std::cout << "[>] Magnet: " << magnet_uri.substr(0, 60) << "..." << std::endl;
    std::cout << "[>] Output: " << output_path << std::endl;
    std::cout << "[>] Max connections: " << max_connections << std::endl;
    std::cout << std::endl;
    
    try {
        // Create io_context
        asio::io_context io_context;
        
        // Create work guard to prevent io_context from exiting with no tasks
        auto work_guard = asio::make_work_guard(io_context);
        
        // Create DownloadController
        g_controller = std::make_shared<application::DownloadController>(io_context);
        
        // Setup callbacks
        g_controller->setStateCallback([](application::DownloadState state) {
            printState(state);
        });
        
        g_controller->setProgressCallback([](const application::DownloadProgress& progress) {
            printProgress(progress);
        });
        
        g_controller->setMetadataCallback([](const application::TorrentMetadata& metadata) {
            std::cout << "\n[+] Metadata received!" << std::endl;
            std::cout << "    Name: " << metadata.name << std::endl;
            std::cout << "    Size: " << formatSize(metadata.total_size) << std::endl;
            std::cout << "    Pieces: " << metadata.piece_count << std::endl;
            std::cout << std::endl;
        });
        
        g_controller->setCompletedCallback([&work_guard](bool success, const std::string& error) {
            std::cout << std::endl;
            if (success) {
                std::cout << "\n[+] Download completed!" << std::endl;
            } else {
                std::cout << "\n[-] Download failed: " << error << std::endl;
            }
            work_guard.reset();
        });
        
        // Configure download
        application::DownloadConfig config;
        config.magnet_uri = magnet_uri;
        config.save_path = output_path;
        config.max_connections = max_connections;
        config.metadata_timeout = std::chrono::seconds(180);
        
        // Start download
        std::cout << "[*] Starting download..." << std::endl;
        if (!g_controller->start(config)) {
            std::cerr << "[-] Failed to start download" << std::endl;
            return 1;
        }
        
        // Run io_context in separate thread
        std::thread io_thread([&io_context]() {
            try {
                io_context.run();
            } catch (const std::exception& e) {
                std::cerr << "IO Error: " << e.what() << std::endl;
            }
        });
        
        // Main loop - wait for completion or interrupt
        while (g_running) {
            auto state = g_controller->state();
            if (state == application::DownloadState::Completed ||
                state == application::DownloadState::Failed ||
                state == application::DownloadState::Stopped) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // Cleanup
        work_guard.reset();
        io_context.stop();
        
        if (io_thread.joinable()) {
            io_thread.join();
        }
        
        // Show final statistics
        auto progress = g_controller->progress();
        std::cout << "\n[*] Statistics:" << std::endl;
        std::cout << "    Downloaded: " << formatSize(progress.downloaded_size) << std::endl;
        std::cout << "    Uploaded: " << formatSize(progress.uploaded_size) << std::endl;
        std::cout << "    Pieces: " << progress.completed_pieces << "/" << progress.total_pieces << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "[-] Error: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "\n[*] Goodbye!" << std::endl;
    return 0;
}
