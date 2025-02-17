#include "binance_ws_client.h"
#include <zmq_manager.hpp>
#include <iostream>
#include <thread>
#include <libwebsockets.h>

using namespace cexpp::util::wss;

// 实现客户端处理程序
class OrderHandler : public IClientHandler {
public:
    // 添加ZMQ相关成员
    std::unique_ptr<ZMQManager> zmq_manager;
    BinanceWsClient* client;

    void onMessage(const std::string& payload) override {
        std::cout << "Received order update:\n" << payload << "\n";
        
        // 这里可以添加更复杂的订单状态解析逻辑
        if (payload.find("\"s\":\"FILLED\"") != std::string::npos) {
            std::cout << "Order filled completely!\n";
        } else if (payload.find("\"s\":\"PARTIALLY_FILLED\"") != std::string::npos) {
            std::cout << "Order partially filled!\n";
        }
    }

    void onUpdate() override {
        std::cout << "Connection established/updated\n";
    }

    std::string genSubscribePayload(const std::string& name, bool isUnsubscribe) override {
        // 本例不需要订阅频道，返回空字符串
        return "";
    }

    // 添加ZMQ信号处理逻辑
    void setup_zmq_listener(struct lws_context* lws_ctx) {
        zmq_manager = std::make_unique<ZMQManager>(lws_ctx, "tcp://localhost:5556");
        zmq_manager->set_signal_handler([this](const TradeSignal& signal) {
            auto trim_str = [](const char* data, size_t max) {
                return std::string(data, strnlen(data, max));
            };

            try {
                client->place_order(
                    trim_str(signal.symbol, 32).c_str(),
                    trim_str(signal.side, 8).c_str(),
                    trim_str(signal.type, 16).c_str(),
                    signal.quantity,
                    signal.price
                );
            } catch (const std::exception& e) {
                std::cerr << "Order failed: " << e.what() << "\n";
            }
        });
    }
};

int main() {
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;
    struct lws_context* lws_ctx = lws_create_context(&info);

    try {
        OrderHandler handler;
        BinanceWsClient client(
            &handler,
            "8W4HJU3kXIGp5gYmGIYlATxIKeH1Psac1Q1e06Oiis1B9DF6CRcMZPNTD7VDhLq3",
            "../Private_key.pem",
            true
        );
        handler.client = &client;
        handler.setup_zmq_listener(lws_ctx);

        std::cout << "Waiting for ZMQ signals...\n";
        while (true) {
            lws_service(lws_ctx, 0);
            usleep(1000); // 1ms sleep to prevent 100% CPU
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        lws_context_destroy(lws_ctx);
        return 1;
    }

    lws_context_destroy(lws_ctx);
    return 0;
}
