#include <ws_client.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

using namespace cexpp::util::wss;


class BitgetHandler : public IClientHandler {
public:
    void onMessage(const nlohmann::json& json) override {
        try {
            spdlog::info("Received JSON message: {}", json.dump(2));
            
            // Handle pong response
            if (json.contains("event") && json["event"] == "pong") {
                lastPongTime = std::chrono::steady_clock::now();
                return;
            }

            // Handle error messages
            if (json.contains("code") && json["code"] != 0) {
                spdlog::error("Error: {}", json["msg"].get<std::string>());
                return;
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

        nlohmann::json payload;
        payload["op"] = isUnsubscribe ? "unsubscribe" : "subscribe";
        payload["args"] = nlohmann::json::array();
        
        nlohmann::json arg;
        arg["instType"] = parts[0];
        arg["channel"] = parts[1];
        arg["instId"] = parts[2];
        payload["args"].push_back(arg);

        return payload.dump();
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
                    // Bitget's ping format
                    nlohmann::json pingMsg;
                    pingMsg["event"] = "ping";
                    wsClient->send(pingMsg.dump());
                }
                std::this_thread::sleep_for(std::chrono::seconds(20));  // Send ping every 20 seconds
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
