// ws_client.cpp
#include <ws_client.h>
#include <iostream>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace cexpp::util::wss {

static std::shared_ptr<spdlog::logger> logger = spdlog::stdout_color_mt("websocket");
// Define the global variable for logging
bool wsLogEnabled = false;

int wsCallback(struct lws* wsi,
               enum lws_callback_reasons reason,
               void* user,
               void* in,
               size_t len);

WsClient::~WsClient() {
    running_ = false;
    
    if (serviceThread_.joinable()) {
        serviceThread_.join();
    }
    if (subscribeThread_.joinable()) {
        subscribeThread_.join();
    }
    
    disconnect();
}

WsClient::WsClient(IClientHandler* handler, 
                   const std::string& url,
                   const std::string& path,
                   uint16_t port,
                   bool useSSL)
    : ClientBase(handler)
    , url_(url)
    , path_(path)
    , port_(port)
    , useSSL_(useSSL) {

    logger->set_level(spdlog::level::info);
    
    // Initialize the first protocol (ws protocol)
    protocols_[0].name = "ws-protocol";
    protocols_[0].callback = wsCallback;
    protocols_[0].per_session_data_size = 0;
    protocols_[0].rx_buffer_size = 4096;
    protocols_[0].id = 0;
    protocols_[0].user = this;
    protocols_[0].tx_packet_size = 0;

    // Initialize the second protocol (null termination)
    protocols_[1].name = nullptr;
    protocols_[1].callback = nullptr;
    protocols_[1].per_session_data_size = 0;
    protocols_[1].rx_buffer_size = 0;
    protocols_[1].id = 0;
    protocols_[1].user = nullptr;
    protocols_[1].tx_packet_size = 0;
    
    connect();
    
    // Start service thread
    running_ = true;
    serviceThread_ = std::thread([this]() {
        while (running_) {
            if (context_) {
                lws_service(context_, 50);  // 50ms timeout
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    
    // Start subscribe management thread
    subscribeThread_ = std::thread([this]() {
        while (running_) {
            processSubscribeQueue();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
}

void WsClient::connect() {
    struct lws_context_creation_info info = {};  // Zero-initialize the struct
    
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols_;
    info.gid = -1;
    info.uid = -1;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    
    context_ = lws_create_context(&info);
    if (!context_) {
        throw std::runtime_error("Failed to create lws context");
    }
    
    struct lws_client_connect_info ccinfo = {};  // Zero-initialize the struct
    
    ccinfo.context = context_;
    ccinfo.address = url_.c_str();
    ccinfo.port = port_;
    ccinfo.path = path_.c_str();
    ccinfo.host = url_.c_str();
    ccinfo.origin = url_.c_str();
    ccinfo.protocol = protocols_[0].name;
    ccinfo.ssl_connection = useSSL_ ? LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED : 0;
    
    connection_ = lws_client_connect_via_info(&ccinfo);
    if (!connection_) {
        throw std::runtime_error("Failed to connect to server");
    }
}

void WsClient::disconnect() {
    if (connection_) {
        lws_callback_on_writable(connection_);
        connection_ = nullptr;
    }
    
    if (context_) {
        lws_context_destroy(context_);
        context_ = nullptr;
    }
}

void WsClient::reconnect(const std::string reason) {
    if (wsLogEnabled) {
        logger->info("Reconnecting due to: {}" , reason);
    }
    
    disconnect();
    connect();
    
    // Resubscribe to all active subscriptions
    std::lock_guard<std::mutex> lock(subMutex_);
    for (const auto& [name, status] : subscribeStatus_) {
        if (status) {
            SubscribeRequest req{
                name,
                handler->genSubscribePayload(name, false),
                "",
                true,
                false,
                std::chrono::steady_clock::now()
            };
            subscribeQueue_.push(req);
        }
    }
    subCv_.notify_one();
}

void WsClient::send(const std::string& payload) const {
    std::lock_guard<std::mutex> lock(sendMutex_);
    sendQueue_.push(payload);
    sendCv_.notify_one();
    
    if (connection_) {
        lws_callback_on_writable(connection_);
    }
}

void WsClient::subscribe(const std::string& name,
                        const std::string& payload,
                        const std::string successKey,
                        bool waitOk) {
    std::lock_guard<std::mutex> lock(subMutex_);
    SubscribeRequest req{
        name,
        payload,
        successKey,
        waitOk,
        false,
        std::chrono::steady_clock::now()
    };
    subscribeQueue_.push(req);
    subCv_.notify_one();
}

void WsClient::unSubscribe(const std::string& name,
                          const std::string& payload,
                          const std::string successKey,
                          bool waitOk) {
    std::lock_guard<std::mutex> lock(subMutex_);
    SubscribeRequest req{
        name,
        payload,
        successKey,
        waitOk,
        true,
        std::chrono::steady_clock::now()
    };
    subscribeQueue_.push(req);
    subCv_.notify_one();
}

void WsClient::subscribeDynamic(const std::string& name,
                              const std::string successKey,
                              bool waitOk) {
    subscribe(name, handler->genSubscribePayload(name, false), successKey, waitOk);
}

void WsClient::unSubscribeDynamic(const std::string& name,
                                 const std::string successKey,
                                 bool waitOk) {
    unSubscribe(name, handler->genSubscribePayload(name, true), successKey, waitOk);
}

bool WsClient::isSubscribeOk(const std::string& name) {
    std::lock_guard<std::mutex> lock(subMutex_);
    return subscribeStatus_[name];
}

bool WsClient::isUnsubscribeOk(const std::string& name) {
    std::lock_guard<std::mutex> lock(subMutex_);
    return unsubscribeStatus_[name];
}

void WsClient::processMessage(const std::string& msg) {
    try {
        auto json = nlohmann::json::parse(msg);
        handler->onMessage(json);
        handleSubscribeResponse(msg);
    } catch (const std::exception& e) {
        handler->onMessage(msg);
    }
}

void WsClient::processSubscribeQueue() {
    std::unique_lock<std::mutex> lock(subMutex_);
    
    while (!subscribeQueue_.empty()) {
        auto& req = subscribeQueue_.front();
        auto now = std::chrono::steady_clock::now();
        
        if (now - req.lastTryTime >= RETRY_INTERVAL) {
            if (req.retryCount < MAX_RETRY_COUNT) {
                send(req.payload);
                req.lastTryTime = now;
                req.retryCount++;
            } else {
                if (wsLogEnabled) {
                    logger->error("Subscribe request failed after max retries: {}", req.name);
                }
                subscribeQueue_.pop();
            }
        }
        
        if (req.waitOk) {
            subCv_.wait_for(lock, RETRY_INTERVAL);
        } else {
            subscribeQueue_.pop();
        }
    }
}

void WsClient::handleSubscribeResponse(const std::string& msg) {
    std::lock_guard<std::mutex> lock(subMutex_);
    
    if (!subscribeQueue_.empty()) {
        auto& req = subscribeQueue_.front();
        if (!req.successKey.empty() && msg.find(req.successKey) != std::string::npos) {
            if (req.isUnsubscribe) {
                unsubscribeStatus_[req.name] = true;
                subscribeStatus_[req.name] = false;
            } else {
                subscribeStatus_[req.name] = true;
                unsubscribeStatus_[req.name] = false;
            }
            subscribeQueue_.pop();
            subCv_.notify_one();
        }
    }
}

int wsCallback(struct lws* wsi,
               enum lws_callback_reasons reason,
               void* user,
               void* in,
               size_t len) {
    // Early return for SSL certificate loading callback
    if (reason == LWS_CALLBACK_OPENSSL_LOAD_EXTRA_CLIENT_VERIFY_CERTS) {
        return 0;
    }

    // Get protocol and validate it exists
    struct lws_protocols const* protocol = lws_get_protocol(wsi);
    if (!protocol || !protocol->user) {
        return 0;
    }
    
    auto* client = static_cast<WsClient*>(protocol->user);
    
    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED: {
            if (wsLogEnabled) {
                logger->info("Connection established");
            }
            client->handler->onUpdate();
            break;
        }
        
        case LWS_CALLBACK_CLIENT_RECEIVE: {
            const auto* payload = static_cast<unsigned char*>(in);
            std::string msg(reinterpret_cast<char*>(const_cast<unsigned char*>(payload)), len);
            client->processMessage(msg);
            break;
        }
        
        case LWS_CALLBACK_CLIENT_WRITEABLE: {
            std::lock_guard<std::mutex> lock(client->sendMutex_);
            if (!client->sendQueue_.empty()) {
                std::string& msg = client->sendQueue_.front();
                
                unsigned char buf[LWS_PRE + msg.length()];
                memcpy(&buf[LWS_PRE], msg.data(), msg.length());
                
                int written = lws_write(wsi, &buf[LWS_PRE], msg.length(), LWS_WRITE_TEXT);
                if (written < 0) {
                    return -1;
                }
                
                client->sendQueue_.pop();
                if (!client->sendQueue_.empty()) {
                    lws_callback_on_writable(wsi);
                }
            }
            break;
        }
        
        case LWS_CALLBACK_CLIENT_CLOSED:
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
            if (wsLogEnabled) {
                logger->warn("Connection closed or error");
            }
            client->reconnect("Connection closed or error");
            break;
        }
        
        default:
            break;
    }
    
    return 0;
}

} // namespace cexpp::util::wss
