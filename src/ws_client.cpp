// ws_client.cpp
#include <ws_client.h>
#include <iostream>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <sched.h>  // 添加头文件

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
                   std::string_view url,
                   std::string_view path,
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
    
    // Start service thread with connect to avoid race conditions
    running_ = true;
    
    // Pre-resolve DNS to speed up connection
    if (wsLogEnabled) {
        logger->info("Pre-resolving DNS for {}", url_);
    }
    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    
    struct addrinfo* result;
    if (getaddrinfo(url_.c_str(), std::to_string(port_).c_str(), &hints, &result) == 0) {
        // Successfully pre-resolved DNS
        freeaddrinfo(result);
    }
    
    // Connect immediately before starting service thread to speed up initialization
    connect();
    
    serviceThread_ = std::thread([this]() {
        // 设置服务线程亲和性
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(-1, &cpuset); // 绑定到核心
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
        
        while (running_) {
            if (context_) {
                lws_service(context_, 1); // Reduce to 1ms for fastest response time
            }
            // Avoid unnecessary sleep that adds latency
            std::this_thread::yield();
        }
    });
    
    // Start subscribe management thread with optimized timing
    subscribeThread_ = std::thread([this]() {
        // 设置订阅线程亲和性
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(-1, &cpuset); // 绑定到核心
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
        
        while (running_) {
            processSubscribeQueue();
            std::this_thread::sleep_for(std::chrono::milliseconds(5)); // Reduce from 50ms to 20ms
        }
    });
}

void WsClient::connect() {
    if (wsLogEnabled) {
        logger->info("Initializing connection to {}:{}{}", url_, port_, path_);
    }
    
    struct lws_context_creation_info info = {};  // Zero-initialize the struct
    
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols_;
    info.gid = -1;
    info.uid = -1;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    
    // Keep-alive settings (available in most versions)
    info.ka_time = 10; // Keep-alive timeout in seconds
    info.ka_interval = 5; // Keep-alive interval
    info.ka_probes = 3; // Number of keep-alive probes
    
    // Remove DNS cache settings for compatibility
    // info.dns_cache_ttl = 300; // Only available in newer versions
    // info.dns_rev_cache_ttl = 300;
    
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
    
    // SSL settings (available in most versions)
    ccinfo.ssl_connection = useSSL_ ? 
        LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | 
        LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK : 0;
    
    if (wsLogEnabled) {
        logger->info("Connecting to {}:{}{}", url_, port_, path_);
    }
    
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

void WsClient::reconnect(std::string_view reason) {
    if (wsLogEnabled) {
        logger->info("Reconnecting due to: {}", reason);
    }
    
    // Add a small delay before reconnecting to avoid rapid reconnection attempts
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    disconnect();
    
    // Try to connect up to 3 times with increasing backoff
    for (int attempt = 0; attempt < 3; attempt++) {
        try {
            logger->info("Connection attempt {} of 3", attempt + 1);
            connect();
            logger->info("Connection successful");
            
            // Successfully connected, no need to try more
            break;
        } catch (const std::exception& e) {
            if (wsLogEnabled) {
                logger->error("Reconnect attempt {} failed: {}", attempt + 1, e.what());
            }
            
            // Last attempt failed, but we've run out of retries
            if (attempt == 2) {
                logger->error("All reconnection attempts failed");
                return;
            }
            
            // Exponential backoff for next attempt
            std::this_thread::sleep_for(std::chrono::milliseconds(500 * (1 << attempt)));
        }
    }
    
    // For Binance direct streams, we don't need to resubscribe
    if (path_.find("/ws/") == 0) {
        logger->info("Using direct stream URL - no resubscription needed");
        return;
    }
    
    // For non-direct streams, resubscribe to active subscriptions
    std::lock_guard<std::mutex> lock(subMutex_);
    logger->info("Resubscribing to active streams");
    
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

void WsClient::send(std::string_view payload) const {
    std::lock_guard<std::mutex> lock(sendMutex_);
    // Pre-allocate buffer with LWS_PRE padding
    static thread_local std::vector<unsigned char> buffer(LWS_PRE + 4096);
    if (buffer.size() < LWS_PRE + payload.length()) {
        buffer.resize(LWS_PRE + payload.length());
    }
    memcpy(buffer.data() + LWS_PRE, payload.data(), payload.length());
    
    sendQueue_.push(std::string(payload)); // Still need to queue for async sending
    sendCv_.notify_one();
    
    if (connection_) {
        lws_callback_on_writable(connection_);
    }
}

void WsClient::subscribe(std::string_view name,
                        std::string_view payload,
                        std::string_view successKey,
                        bool waitOk) {
    std::lock_guard<std::mutex> lock(subMutex_);
    SubscribeRequest req{
        std::string(name),      // Need to copy for storage
        std::string(payload),   // Need to copy for storage
        std::string(successKey),// Need to copy for storage
        waitOk,
        false,
        std::chrono::steady_clock::now()
    };
    subscribeQueue_.push(std::move(req));  // Use move to avoid extra copy
    subCv_.notify_one();
}

void WsClient::unSubscribe(std::string_view name,
                          std::string_view payload,
                          std::string_view successKey,
                          bool waitOk) {
    std::lock_guard<std::mutex> lock(subMutex_);
    SubscribeRequest req{
        std::string(name),
        std::string(payload),
        std::string(successKey),
        waitOk,
        true,
        std::chrono::steady_clock::now()
    };
    subscribeQueue_.push(req);
    subCv_.notify_one();
}

void WsClient::subscribeDynamic(std::string_view name,
                              std::string_view successKey,
                              bool waitOk) {
    subscribe(name, handler->genSubscribePayload(std::string(name), false), successKey, waitOk);
}

void WsClient::unSubscribeDynamic(std::string_view name,
                                 std::string_view successKey,
                                 bool waitOk) {
    unSubscribe(name, handler->genSubscribePayload(std::string(name), true), successKey, waitOk);
}

bool WsClient::isSubscribeOk(std::string_view name) {
    std::lock_guard<std::mutex> lock(subMutex_);
    return subscribeStatus_[std::string(name)];  // Need string for map lookup
}

bool WsClient::isUnsubscribeOk(std::string_view name) {
    std::lock_guard<std::mutex> lock(subMutex_);
    return unsubscribeStatus_[std::string(name)];  // Need string for map lookup
}

void WsClient::processMessage(const std::string& msg) {
    // Fast path: first try to handle subscribe response without JSON parsing
    if (!subscribeQueue_.empty()) {
        handleSubscribeResponse(msg);
    }
    
    try {
        auto json = nlohmann::json::parse(msg);
        handler->onMessage(json);
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
                
                // Add to pending callbacks
                pendingCallbacks_.emplace_back(req.name, true);
            } else {
                subscribeStatus_[req.name] = true;
                unsubscribeStatus_[req.name] = false;
                
                // Add to pending callbacks
                pendingCallbacks_.emplace_back(req.name, false);
            }
            subscribeQueue_.pop();
            subCv_.notify_one();
        }
    }
}

void WsClient::setSubscriptionCallback(std::function<void(const std::string&, bool)> callback) {
    subscriptionCallback_ = std::move(callback);
}

void WsClient::setUnsubscriptionCallback(std::function<void(const std::string&, bool)> callback) {
    unsubscriptionCallback_ = std::move(callback);
}

void WsClient::processEvents() {
    // Process any pending events in the main thread
    // This can be called regularly from the main loop
    
    // Check for subscription status changes
    std::lock_guard<std::mutex> lock(subMutex_);
    for (auto it = pendingCallbacks_.begin(); it != pendingCallbacks_.end();) {
        const auto& [name, isUnsubscribe] = *it;
        
        if (isUnsubscribe) {
            if (unsubscribeStatus_[name]) {
                if (unsubscriptionCallback_) {
                    unsubscriptionCallback_(name, true);
                }
                it = pendingCallbacks_.erase(it);
                continue;
            }
        } else {
            if (subscribeStatus_[name]) {
                if (subscriptionCallback_) {
                    subscriptionCallback_(name, true);
                }
                it = pendingCallbacks_.erase(it);
                continue;
            }
        }
        
        ++it;
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
        
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
            const char* error_msg = in ? static_cast<const char*>(in) : "Unknown error";
            if (wsLogEnabled) {
                logger->error("Connection error: {}", error_msg);
            }
            client->reconnect("Connection error");
            break;
        }
        
        case LWS_CALLBACK_CLIENT_CLOSED: {
            if (wsLogEnabled) {
                logger->warn("Connection closed");
            }
            client->reconnect("Connection closed");
            break;
        }
        
        default:
            break;
    }
    
    return 0;
}

} // namespace cexpp::util::wss
