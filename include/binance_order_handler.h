#pragma once

#include <ws_client.h>
#include <string>
#include <map>
#include <chrono>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>
#include <spdlog/spdlog.h>
#include <sstream>
#include <iomanip>

// 前向声明
namespace cexpp::util {
    class CryptoUtils;
}

class BinanceOrderHandler : public cexpp::util::wss::IClientHandler {
public:
    BinanceOrderHandler(const std::string& api_key, const std::string& private_key_path);
    ~BinanceOrderHandler();

    void onMessage(const nlohmann::json& json) override;
    void onMessage(const std::string& msg) override;
    void onUpdate() override;
    std::string genSubscribePayload(const std::string& name, bool isUnsubscribe) override;

    // Order placement methods
    std::string place_order(const std::string& symbol, 
                           const std::string& side, 
                           const std::string& type, 
                           double quantity, 
                           double price);
    
    void setWsClient(cexpp::util::wss::WsClient* client);

private:
    std::string api_key_;
    std::string private_key_path_;
    cexpp::util::wss::WsClient* ws_client_ = nullptr;
    std::string json_encode_params(const std::map<std::string, std::string>& params, const std::string& signature);
}; 