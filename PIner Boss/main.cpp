#include <iostream>
#include <csignal>
#include <atomic>
#include <memory>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <thread>

#include "config.h"
#include "mining_coordinator.h"
#include "terminal_ui.h"

std::atomic<bool> g_shutdown(false);
MiningCoordinator* g_coordinator = nullptr;
TerminalUI g_ui;

void signalHandler(int signum) {
    std::cout << "\n\n⚠️  Interrupt signal (" << signum << ") received." << std::endl;
    
    if (g_shutdown.exchange(true)) {
        std::cout << "⚠️  Force exit..." << std::endl;
        _exit(1);
    }
    
    g_ui.cleanup();
    
    if (g_coordinator) {
        std::cout << "🛑 Stopping mining coordinator..." << std::endl;
        g_coordinator->stop();
    }
    
    std::cout << "✅ Cleanup complete, exiting..." << std::endl;
    _exit(0);
}

int main(int argc, char** argv) {
    // ========================================================================
    // DEFAULT POOL CONFIGURATION
    // ========================================================================
    std::string pool_host = DEFAULT_POOL_HOST;
    int pool_port = DEFAULT_POOL_PORT;
    std::string pool_user = DEFAULT_POOL_USER;
    std::string pool_pass = DEFAULT_POOL_PASS;
    
    // ========================================================================
    // COMMAND LINE ARGUMENT PARSING
    // ========================================================================
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--pool-host" && i + 1 < argc) {
            pool_host = argv[++i];
        }
        else if (arg == "--pool-port" && i + 1 < argc) {
            pool_port = std::stoi(argv[++i]);
        }
        else if (arg == "--pool-user" && i + 1 < argc) {
            pool_user = argv[++i];
        }
        else if (arg == "--pool-pass" && i + 1 < argc) {
            pool_pass = argv[++i];
        }
        else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  --pool-host <host>    Stratum pool host" << std::endl;
            std::cout << "  --pool-port <port>    Stratum pool port" << std::endl;
            std::cout << "  --pool-user <user>    Pool user/worker" << std::endl;
            std::cout << "  --pool-pass <pass>    Pool password" << std::endl;
            std::cout << "  --help, -h            Show this help message" << std::endl;
            return 0;
        }
    }
    
    // ========================================================================
    // STARTUP BANNER
    // ========================================================================
    std::cout << "=== Pi Bitcoin Miner (I2C) ===" << std::endl;
    std::cout << "Raspberry Pi Zero 2 W Coordinator" << std::endl;
    std::cout << "Pool: " << pool_host << ":" << pool_port << std::endl;
    std::cout << "User: " << pool_user << std::endl;
    std::cout << "Starting..." << std::endl << std::endl;
    
    // ========================================================================
    // SIGNAL HANDLER SETUP (Ctrl+C)
    // ========================================================================
    struct sigaction sa;
    sa.sa_handler = signalHandler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    
    // ========================================================================
    // INITIALIZE MINING COORDINATOR
    // ========================================================================
    auto coordinator = std::make_unique<MiningCoordinator>(pool_host, pool_port, pool_user, pool_pass);
    g_coordinator = coordinator.get();
    
    if (!coordinator->start()) {
        std::cerr << "Failed to start mining coordinator" << std::endl;
        return 1;
    }
    
    // ========================================================================
    // INITIALIZE TERMINAL UI
    // ========================================================================
    g_ui.init();
    
    // ========================================================================
    // MAIN DISPLAY LOOP
    // ========================================================================
    try {
        while (!g_shutdown.load()) {
            DisplayStats stats;
            coordinator->updateDisplayStats(stats);
            g_ui.update(stats);
            
            // Short sleep for faster signal response
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    } catch (const std::exception& e) {
        g_ui.cleanup();
        std::cerr << "UI error: " << e.what() << std::endl;
    }
    
    // ========================================================================
    // CLEANUP
    // ========================================================================
    g_ui.cleanup();
    coordinator->stop();
    g_coordinator = nullptr;
    
    return 0;
}
