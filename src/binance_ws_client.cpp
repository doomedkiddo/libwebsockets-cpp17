#include "binance_ws_client.h"
#include <openssl/pem.h>
#include <boost/format.hpp>
#include <iomanip>
#include <sstream>

namespace cexpp::util::wss {

using namespace std::chrono;

EVP_PKEY* BinanceWsClient::load_private_key() {
    FILE* file = fopen(private_key_path_.c_str(), "r");
    if (!file) {
        spdlog::error("Failed to open private key file: {}", private_key_path_);
        return nullptr;
    }

    EVP_PKEY* private_key = PEM_read_PrivateKey(file, nullptr, nullptr, nullptr);
    fclose(file);

    if (!private_key) {
        spdlog::error("Failed to load Ed25519 private key");
        return nullptr;
    }

    if (EVP_PKEY_id(private_key) != EVP_PKEY_ED25519) {
        spdlog::error("Invalid key type, expected Ed25519");
        EVP_PKEY_free(private_key);
        return nullptr;
    }

    return private_key;
}

std::string BinanceWsClient::generate_signature(const std::string& data) {
    EVP_PKEY* private_key = load_private_key();
    if (!private_key) {
        throw std::runtime_error("Failed to load private key");
    }

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        EVP_PKEY_free(private_key);
        throw std::runtime_error("Failed to create EVP context");
    }

    if (EVP_DigestSignInit(ctx, nullptr, nullptr, nullptr, private_key) <= 0) {
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(private_key);
        throw std::runtime_error("Signing initialization failed");
    }

    size_t sig_len;
    if (EVP_DigestSign(ctx, nullptr, &sig_len, 
                      reinterpret_cast<const unsigned char*>(data.c_str()), 
                      data.size()) <= 0) {
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(private_key);
        throw std::runtime_error("Failed to get signature length");
    }

    std::vector<unsigned char> signature(sig_len);
    if (EVP_DigestSign(ctx, signature.data(), &sig_len,
                     reinterpret_cast<const unsigned char*>(data.c_str()),
                     data.size()) <= 0) {
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(private_key);
        throw std::runtime_error("Signing failed");
    }

    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(private_key);

    // Base64编码
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* mem = BIO_new(BIO_s_mem());
    BIO_push(b64, mem);
    BIO_write(b64, signature.data(), sig_len);
    BIO_flush(b64);

    BUF_MEM* bptr;
    BIO_get_mem_ptr(b64, &bptr);
    std::string result(bptr->data, bptr->length - 1); // 移除换行符
    BIO_free_all(b64);

    return result;
}

std::string BinanceWsClient::build_order_payload(const std::map<std::string, std::string>& params) {
    std::stringstream payload;
    payload << "{";
    bool first = true;
    for (const auto& [key, value] : params) {
        if (!first) payload << ",";
        payload << "\"" << key << "\":\"" << value << "\"";
        first = false;
    }
    payload << "}";
    return payload.str();
}

void BinanceWsClient::place_order(const std::string& symbol,
                                const std::string& side,
                                const std::string& type,
                                double quantity,
                                double price) {
    auto timestamp = duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()).count();

    // 构建参数表
    std::map<std::string, std::string> params = {
        {"symbol", symbol},
        {"side", side},
        {"type", type},
        {"quantity", std::to_string(quantity)},
        {"price", std::to_string(price)},
        {"timestamp", std::to_string(timestamp)},
        {"apiKey", api_key_}
    };

    // 生成签名
    std::stringstream payload_ss;
    for (const auto& [key, val] : params) {
        payload_ss << key << "=" << val << "&";
    }
    std::string payload_str = payload_ss.str();
    payload_str.pop_back(); // 移除最后的&

    std::string signature = generate_signature(payload_str);
    params["signature"] = signature;

    // 构建最终请求
    std::string request = (boost::format(R"({"method":"order.place","params":%s,"id":%d})")
                          % build_order_payload(params)
                          % timestamp).str();

    // 通过libwebsockets发送
    send(request);
}

} // namespace cexpp::util::wss 