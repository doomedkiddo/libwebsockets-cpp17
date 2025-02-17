#include <ws_client.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <fstream>
#include <simdjson.h>

using namespace cexpp::util::wss;


class BitgetHandler : public IClientHandler {
public:
    void onMessage(const std::string& msg) override {
        simdjson::ondemand::parser parser;
        simdjson::padded_string json_msg(msg);
        
        try {
            auto doc = parser.iterate(json_msg);
            spdlog::info("Received JSON message: {}", msg);
            
            // Handle pong response
            std::string_view event;
            if (doc["event"].get(event) == simdjson::SUCCESS && event == "pong") {
                lastPongTime = std::chrono::steady_clock::now();
                return;
            }

            // Handle error messages
            int64_t code;
            if (doc["code"].get(code) == simdjson::SUCCESS && code != 0) {
                std::string_view message;
                doc["msg"].get(message);
                spdlog::error("Error: {}", message);
                return;
            }
            
            std::ofstream logFile("message_log.txt", std::ios::app);
            if (logFile.is_open()) {
                auto now = std::chrono::system_clock::now();
                auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
                
                // Check if 'data' exists and is an array
                auto data = doc["data"];
                if (doc["data"].error() == simdjson::SUCCESS && data.type() == simdjson::ondemand::json_type::array) {
                    for (auto item : data.get_array()) {
                        std::string_view ts;
                        if (item["ts"].get(ts) == simdjson::SUCCESS) {
                            logFile << timestamp << ": " << ts << std::endl;
                        }
                    }
                }
                logFile.close();
            }
        } 
        catch (const simdjson::simdjson_error& e) {
            spdlog::error("Error processing message: {}", e.what());
        }
    }

    void onUpdate() override {
        spdlog::info("Connection status updated");
        // Start ping timer after connection is established
        if (wsClient) {
            startPingTimer();
        }
    }

    std::string genSubscribePayload(const std::string& name, bool isUnsubscribe) override {
        // Parse the subscription details from the name
        // Expecting format: "instType.channel.instId"
        auto parts = splitString(name, '.');
        if (parts.size() != 3) {
            throw std::runtime_error("Invalid subscription name format");
        }

        std::string payload;
        if (isUnsubscribe) {
            payload = R"({"op":"unsubscribe","args":[{"instType":")" + parts[0] + 
                     R"(","channel":")" + parts[1] + 
                     R"(","instId":")" + parts[2] + R"("}]})";
        } else {
            payload = R"({"op":"subscribe","args":[{"instType":")" + parts[0] + 
                     R"(","channel":")" + parts[1] + 
                     R"(","instId":")" + parts[2] + R"("}]})";
        }
        return payload;
    }

    void setWsClient(WsClient* client) {
        wsClient = client;
        if (wsClient) {
            startPingTimer();
        }
    }

    ~BitgetHandler() {
        stopPingTimer();
    }

private:
    void startPingTimer() {
        stopPingTimer();  // Stop existing timer if any
        
        pingRunning = true;
        pingThread = std::thread([this]() {
            while (pingRunning) {
                if (wsClient) {
                    // Use raw string for ping since simdjson is parse-only
                    wsClient->send(R"({"event":"ping"})");
                }
                std::this_thread::sleep_for(std::chrono::seconds(20));
            }
        });
    }

    void stopPingTimer() {
        pingRunning = false;
        if (pingThread.joinable()) {
            pingThread.join();
        }
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
    std::thread pingThread;
    std::atomic<bool> pingRunning{false};
    std::chrono::steady_clock::time_point lastPongTime;
};

int main() {
    // Enable WebSocket logging

    auto handler = std::make_shared<BitgetHandler>();
    
    // Create WebSocket client for Bitget
    auto client = std::make_shared<WsClient>(
        handler.get(),
        "ws.bitget.com",    // Bitget WebSocket domain
        "/v2/ws/public",    // WebSocket path
        443,               // Port
        true              // Use SSL
    );

    // Set the client reference in handler for ping-pong
    handler->setWsClient(client.get());

    // Subscribe to BTC-USDT futures ticker
    std::string channelName = "USDT-FUTURES.ticker.BTCUSDT";
    client->subscribeDynamic(channelName, "code", true);
    channelName = "USDT-FUTURES.ticker.XRPUSDT";
    client->subscribeDynamic(channelName, "code", true);
    channelName = "USDT-FUTURES.ticker.ETHUSDT";
    client->subscribeDynamic(channelName, "code", true);

    // Keep the program running and handle user input
    std::string command;
    while (true) {
        std::cout << "\nEnter command (subscribe/unsubscribe/quit): ";
        std::getline(std::cin, command);

        if (command == "quit") {
            break;
        }
        else if (command == "subscribe") {
            client->subscribeDynamic(channelName, "code", true);
        }
        else if (command == "unsubscribe") {
            client->unSubscribeDynamic(channelName, "code", true);
        }
    }

    return 0;
}
