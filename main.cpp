#include <ws_client.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <signal.h>

using namespace cexpp::util::wss;

// Add global running flag
static std::atomic<bool> running{true};

class BinanceHandler : public IClientHandler {
public:
    void onMessage(const nlohmann::json& json) override {
        try {
            // For a direct stream connection, we'll receive data directly
            // The format is different from subscription-based connections
            
            // Direct stream data doesn't have a stream field, the whole JSON is the data
            spdlog::info("Received market data: {}", json.dump(2));
            
            // Process ticker data specifically
            if (json.contains("e") && json["e"] == "24hrTicker") {
                // Example of extracting some key ticker data
                std::string symbol = json["s"];
                double price = std::stod(json["c"].get<std::string>());
                double priceChange = std::stod(json["p"].get<std::string>());
                double priceChangePercent = std::stod(json["P"].get<std::string>());
                
                spdlog::info("Ticker: {} Price: {} Change: {}%", 
                             symbol, price, priceChangePercent);
            }
            
            // Other message types can be processed here
            
            // Error handling still applies
            if (json.contains("error")) {
                spdlog::error("Error: {}", json.dump());
            }
        } catch (const std::exception& e) {
            spdlog::error("Error processing message: {}", e.what());
        }
    }

    void onMessage(const std::string& msg) override {
        spdlog::info("Received raw message: {}", msg);
    }

    void onUpdate() override {
        spdlog::info("Connection status updated");
        // For direct stream connections, we don't need ping-pong
        // Binance will keep the connection alive
    }

    std::string genSubscribePayload(const std::string& name, bool isUnsubscribe) override {
        // Binance expects: {"method":"SUBSCRIBE","params":["btcusdt@ticker"],"id":1}
        static int requestId = 1;
        
        nlohmann::json payload;
        payload["method"] = isUnsubscribe ? "UNSUBSCRIBE" : "SUBSCRIBE";
        payload["params"] = nlohmann::json::array();
        payload["params"].push_back(name);
        payload["id"] = requestId++;  // Use incrementing ID for each request

        return payload.dump();
    }

    void setWsClient(WsClient* client) {
        wsClient = client;
        // Don't start ping timer for direct streams
    }

    ~BinanceHandler() {
        stopPingTimer();
    }

private:
    // Keep ping timer methods but don't use them
    void startPingTimer() {
        // For direct streams, we don't need to send pings
        spdlog::info("Ping timer not required for direct stream connection");
    }

    void stopPingTimer() {
        // No need to implement stopPingTimer for direct streams
    }

    std::vector<std::string> splitString(const std::string& str, char delimiter) {
        std::vector<std::string> tokens;
        std::string token;
        std::istringstream tokenStream(str);
        while (std::getline(tokenStream, token, delimiter)) {
            tokens.push_back(token);
        }
        return tokens;
    }

    WsClient* wsClient = nullptr;
    std::chrono::steady_clock::time_point lastPongTime;
};

int main() {
    // Enable WebSocket logging
    wsLogEnabled = true;
    
    // Set spdlog to flush immediately to see real-time logs
    spdlog::set_level(spdlog::level::debug);
    spdlog::flush_every(std::chrono::seconds(1));

    auto handler = std::make_shared<BinanceHandler>();
    
    // For a single stream, use the direct stream URL format
    std::string channelName = "btcusdt@ticker";
    
    spdlog::info("Creating WebSocket client for Binance...");
    
    // Create WebSocket client with the direct stream endpoint
    // IMPORTANT: For a direct single stream, we connect directly to the stream endpoint
    auto client = std::make_shared<WsClient>(
        handler.get(),
        "stream.binance.com",      // Binance WebSocket domain
        "/ws/" + channelName,      // Direct stream path - use this format for single streams
        443,                       // Port
        true                       // Use SSL
    );

    // Immediately set the client reference in handler to avoid race conditions
    handler->setWsClient(client.get());
    
    spdlog::info("WebSocket client created and initialized");

    // Set up a signal handler for clean shutdown
    signal(SIGINT, [](int) {
        std::cout << "\nShutting down..." << std::endl;
        // Global flag to indicate shutdown
        running = false;
    });

    // Main event loop
    while (running) {
        // Process any pending events
        client->processEvents();
        
        // Reduce sleep time for faster response
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    return 0;
}
