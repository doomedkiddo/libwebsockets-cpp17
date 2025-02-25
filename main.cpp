#include <ws_client.h>
#include "binance_order_handler.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <signal.h>

using namespace cexpp::util::wss;

// Add global running flag
static std::atomic<bool> running{true};

int main() {
    // Enable WebSocket logging
    wsLogEnabled = true;
    
    // Set spdlog to flush immediately to see real-time logs
    spdlog::set_level(spdlog::level::debug);
    spdlog::flush_every(std::chrono::seconds(1));

    // Set your API key and private key file path here
    std::string api_key = "YOUR_API_KEY";
    std::string pem_file_path = "Private_key.pem";  // Private key file path

    auto handler = std::make_shared<BinanceOrderHandler>(api_key, pem_file_path);
    
    spdlog::info("Creating WebSocket client for Binance order operations...");
    
    // For order operations, connect to the Binance Futures websocket API
    auto client = std::make_shared<WsClient>(
        handler.get(),
        "ws-fapi.binance.com",     // Binance Futures WebSocket domain
        "/ws-fapi/v1",             // API path
        443,                       // Port
        true                       // Use SSL
    );

    // Set the client reference in handler
    handler->setWsClient(client.get());
    
    spdlog::info("WebSocket client created and initialized");

    // Set up a signal handler for clean shutdown
    signal(SIGINT, [](int) {
        std::cout << "\nShutting down..." << std::endl;
        // Global flag to indicate shutdown
        running = false;
    });

    // Wait a moment for connection to establish
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Example order placement
    try {
        // Place a test order - modify these parameters as needed
        std::string symbol = "BTCUSDT";
        std::string side = "BUY";
        std::string type = "LIMIT";
        double quantity = 0.001;  // Small quantity for testing
        double price = 60000.0;   // Set appropriate price
        
        handler->place_order(symbol, side, type, quantity, price);
        spdlog::info("Order placement request sent");
    } catch (const std::exception& e) {
        spdlog::error("Error placing order: {}", e.what());
    }

    // Main event loop
    while (running) {
        // Process any pending events
        client->processEvents();
        
        // Reduce sleep time for faster response
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    return 0;
}
