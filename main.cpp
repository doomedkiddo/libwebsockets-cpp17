// ws_client_test.cpp
#include <ws_client.h>
#include <iostream>
#include <chrono>
#include <thread>

using namespace cexpp::util::wss;

// 自定义处理器，实现IClientHandler接口
class TestHandler : public IClientHandler {
public:
    void onMessage(const nlohmann::json& json) override {
        std::cout << "Received JSON message: " << json.dump() << std::endl;
    }

    void onMessage(const std::string& msg) override {
        std::cout << "Received raw message: " << msg << std::endl;
    }

    void onUpdate() override {
        std::cout << "Connection status updated" << std::endl;
    }

    std::string genSubscribePayload(const std::string& name, bool isUnsubscribe) override {
        // 生成订阅/取消订阅的payload示例
        if (isUnsubscribe) {
            return R"({"unsub":")" + name + R"("})";
        }
        return R"({"sub":")" + name + R"("})";
    }
};

int main() {
    TestHandler handler;
    WsClient client(
        &handler,
        "echo.websocket.org",  // WebSocket测试服务器地址
        "/",                   // 路径
        443,                   // 端口
        true                   // 使用SSL
    );

    // 测试基本消息发送
    client.send(R"({"test":"Hello WebSocket"})");
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // 测试动态订阅
    client.subscribeDynamic("market.btcusdt.ticker", "success_key", true);
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // 测试取消订阅
    client.unSubscribeDynamic("market.btcusdt.ticker", "success_key", true);
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // 测试重连
    std::cout << "Testing reconnect..." << std::endl;
    client.reconnect("manual test");
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // 保持运行以观察输出
    std::cout << "Press Enter to exit..." << std::endl;
    std::cin.get();

    return 0;
}
