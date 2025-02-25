#pragma once
#include <string>
#include <openssl/evp.h>

class CryptoUtils {
public:
    static std::string base64_encode(const unsigned char* data, size_t length);
    static EVP_PKEY* load_ed25519_private_key(const std::string& private_key_path);
    static std::string generate_signature(const std::string& private_key_path, const std::string& data);
}; 