#include "binance_order_handler.h"
#include "crypto_utils.hpp"
#include <vector>
#include <spdlog/spdlog.h>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <fstream>

BinanceOrderHandler::BinanceOrderHandler(const std::string& api_key, const std::string& private_key_path)
    : api_key_(api_key), private_key_path_(private_key_path) {
    spdlog::info("BinanceOrderHandler initialized with API key: {}", api_key);
    
    // Check if private key file exists
    std::ifstream keyFile(private_key_path);
    if (!keyFile.good()) {
        spdlog::error("Private key file not found at: {}", private_key_path);
        throw std::runtime_error("Private key file not found");
    }
    spdlog::info("Private key file found at: {}", private_key_path);
}

BinanceOrderHandler::~BinanceOrderHandler() {
    // Cleanup
}

void BinanceOrderHandler::onMessage(const nlohmann::json& json) {
    try {
        spdlog::info("Received order response: {}", json.dump(2));
        
        // Check for order response
        if (json.contains("id") && json.contains("result")) {
            // This is an order response
            if (json.contains("error")) {
                spdlog::error("Order error: {}", json.dump());
            } else {
                spdlog::info("Order successfully placed: {}", json.dump());
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("Error processing message: {}", e.what());
    }
}

void BinanceOrderHandler::onMessage(const std::string& msg) {
    try {
        spdlog::info("Received raw message: {}", msg);
        auto json = nlohmann::json::parse(msg);
        onMessage(json);
    } catch (const std::exception& e) {
        spdlog::error("Error parsing message: {}", e.what());
    }
}

void BinanceOrderHandler::onUpdate() {
    spdlog::info("Connection status updated - Ready for order placement");
}

std::string BinanceOrderHandler::genSubscribePayload(const std::string& name, bool isUnsubscribe) {
    // Not needed for order operations
    return "{}";
}

void BinanceOrderHandler::setWsClient(cexpp::util::wss::WsClient* client) {
    ws_client_ = client;
}

std::string BinanceOrderHandler::place_order(const std::string& symbol, 
                                            const std::string& side, 
                                            const std::string& type, 
                                            double quantity, 
                                            double price) {
    spdlog::info("Placing order: {} {} {} {} @ {}", symbol, side, type, quantity, price);
    
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    // Calculate take profit and stop loss prices (0.06% and 0.04%)
    double tp_price, sl_price;
    if (side == "BUY") {
        tp_price = price * 1.0006;  // Take profit 0.06% above entry
        sl_price = price * 0.9996;  // Stop loss 0.04% below entry
    } else {  // SELL
        tp_price = price * 0.9994;  // Take profit 0.06% below entry
        sl_price = price * 1.0004;  // Stop loss 0.04% above entry
    }

    // Format prices to correct number of decimal places
    std::stringstream tp_ss, sl_ss;
    tp_ss << std::fixed << std::setprecision(2) << tp_price;
    sl_ss << std::fixed << std::setprecision(2) << sl_price;

    // Create params map and sort it
    std::map<std::string, std::string> params;
    params["apiKey"] = api_key_;
    params["price"] = std::to_string(price);
    params["quantity"] = std::to_string(quantity);
    params["side"] = side;
    params["symbol"] = symbol;
    params["timeInForce"] = "GTC";
    params["timestamp"] = std::to_string(timestamp);
    params["type"] = type;
    params["takeProfitPrice"] = tp_ss.str();
    params["stopLossPrice"] = sl_ss.str();
    params["workingType"] = "MARK_PRICE";  // Use mark price for TP/SL triggers

    // Build payload string in sorted order
    std::stringstream payload;
    bool first = true;
    for (const auto& param : params) {
        if (!first) payload << "&";
        payload << param.first << "=" << param.second;
        first = false;
    }

    // Use CryptoUtils to generate RSA signature from PEM file
    std::string signature = CryptoUtils::generate_signature(private_key_path_, payload.str());
    signature.erase(std::remove(signature.begin(), signature.end(), '\n'), signature.end());
    
    std::string request = (boost::format(R"({"id": "order_%s", "method": "order.place", "params": %s})")
        % std::to_string(timestamp)
        % json_encode_params(params, signature)).str();

    spdlog::info("Sending order request: {}", request);
    
    if (ws_client_) {
        ws_client_->send(request);
    } else {
        spdlog::error("WebSocket client not set");
    }
    
    return request;
}

std::string BinanceOrderHandler::json_encode_params(const std::map<std::string, std::string>& params, const std::string& signature) {
    std::stringstream ss;
    ss << "{";
    bool first = true;
    for (const auto& param : params) {
        if (!first) ss << ",";
        ss << "\"" << param.first << "\":\"" << param.second << "\"";
        first = false;
    }
    ss << ",\"signature\":\"" << signature << "\"}";
    return ss.str();
} 