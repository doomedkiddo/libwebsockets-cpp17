#include "crypto_utils.hpp"
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>
#include <cryptopp/base64.h>
#include <spdlog/spdlog.h>
#include <vector>

std::string CryptoUtils::base64_encode(const unsigned char* data, size_t length) {
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* mem = BIO_new(BIO_s_mem());
    BIO_push(b64, mem);
    BIO_write(b64, data, length);
    BIO_flush(b64);

    BUF_MEM* bptr;
    BIO_get_mem_ptr(b64, &bptr);

    std::string result(bptr->data, bptr->length - 1);
    BIO_free_all(b64);
    return result;
}

EVP_PKEY* CryptoUtils::load_ed25519_private_key(const std::string& private_key_path) {
    FILE* file = fopen(private_key_path.c_str(), "r");
    if (!file) {
        spdlog::error("Failed to open private key file.");
        return nullptr;
    }

    EVP_PKEY* private_key = PEM_read_PrivateKey(file, nullptr, nullptr, nullptr);
    fclose(file);

    if (!private_key) {
        spdlog::error("Failed to load Ed25519 private key.");
        return nullptr;
    }

    if (EVP_PKEY_id(private_key) != EVP_PKEY_ED25519) {
        spdlog::error("The key is not an Ed25519 key.");
        EVP_PKEY_free(private_key);
        return nullptr;
    }

    return private_key;
}

std::string CryptoUtils::generate_signature(const std::string& private_key_path, const std::string& data) {
    EVP_PKEY* private_key = load_ed25519_private_key(private_key_path);
    if (!private_key) {
        throw std::runtime_error("Failed to load Ed25519 private key.");
    }

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        EVP_PKEY_free(private_key);
        throw std::runtime_error("Failed to create EVP_MD_CTX.");
    }

    if (EVP_DigestSignInit(ctx, nullptr, nullptr, nullptr, private_key) <= 0) {
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(private_key);
        throw std::runtime_error("Failed to initialize signing context.");
    }

    size_t sig_len;
    if (EVP_DigestSign(ctx, nullptr, &sig_len, (const unsigned char*)data.c_str(), data.size()) <= 0) {
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(private_key);
        throw std::runtime_error("Failed to determine signature length.");
    }

    std::vector<unsigned char> signature(sig_len);
    if (EVP_DigestSign(ctx, signature.data(), &sig_len, (const unsigned char*)data.c_str(), data.size()) <= 0) {
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(private_key);
        throw std::runtime_error("Failed to generate signature.");
    }

    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(private_key);

    return base64_encode(signature.data(), sig_len);
} 