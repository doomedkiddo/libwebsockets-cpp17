// ws_client.h

#pragma once

#include "websocket_client_base.h"
#include <libwebsockets.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <queue>
#include <unordered_map>
#include <condition_variable>
#include <chrono>

namespace cexpp::util::wss {

// Forward declaration of callback
int wsCallback(struct lws* wsi,
               enum lws_callback_reasons reason,
               void* user,
               void* in,
               size_t len);

struct SubscribeRequest {
    std::string name;
    std::string payload;
    std::string successKey;
    bool waitOk;
    bool isUnsubscribe;
    std::chrono::steady_clock::time_point lastTryTime;
    int retryCount{0};
};

class WsClient : public ClientBase, public std::enable_shared_from_this<WsClient> {
public:
    WsClient(IClientHandler* handler, 
             std::string_view url,
             std::string_view path,
             uint16_t port = 443,
             bool useSSL = true);
    ~WsClient();

    void reconnect(std::string_view reason) override;
    void send(std::string_view payload) const override;
    
    void subscribe(std::string_view name,
                  std::string_view payload,
                  std::string_view successKey,
                  bool waitOk) override;
                  
    void unSubscribe(std::string_view name,
                    std::string_view payload,
                    std::string_view successKey,
                    bool waitOk) override;
                     
    void subscribeDynamic(std::string_view name,
                         std::string_view successKey,
                         bool waitOk) override;
                         
    void unSubscribeDynamic(std::string_view name,
                           std::string_view successKey,
                           bool waitOk) override;
                           
    bool isSubscribeOk(std::string_view name) override;
    bool isUnsubscribeOk(std::string_view name) override;

    // Make these public for the callback
    void processMessage(const std::string& msg);

protected:
    using ClientBase::handler;  // Make handler accessible

private:
    void connect();
    void disconnect();
    void processSubscribeQueue();
    void handleSubscribeResponse(const std::string& msg);

    // Connection related
    std::string url_;
    std::string path_;
    uint16_t port_;
    bool useSSL_;
    
    // libwebsockets context
    struct lws_context* context_{nullptr};
    struct lws* connection_{nullptr};
    struct lws_protocols protocols_[2];  // One for ws, one for null termination
    
    // Threading
    std::atomic<bool> running_{false};
    std::thread serviceThread_;
    std::thread subscribeThread_;
    
    // Message queue
    mutable std::mutex sendMutex_;
    mutable std::queue<std::string> sendQueue_;
    mutable std::condition_variable sendCv_;
    
    // Subscribe management
    std::mutex subMutex_;
    std::queue<SubscribeRequest> subscribeQueue_;
    std::condition_variable subCv_;
    
    // Subscribe status tracking
    std::unordered_map<std::string, bool> subscribeStatus_;
    std::unordered_map<std::string, bool> unsubscribeStatus_;
    
    // Retry configuration
    static constexpr int MAX_RETRY_COUNT = 3;
    static constexpr auto RETRY_INTERVAL = std::chrono::seconds(5);

    friend int wsCallback(struct lws* wsi,
                         enum lws_callback_reasons reason,
                         void* user,
                         void* in,
                         size_t len);
};

} // namespace cexpp::util::wss
