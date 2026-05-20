#include "bcrypt.h"
#include <crypt.h>
#include <random>
#include <stdexcept>
#include <cstring>
#include <iostream>

// Base64 字符表（bcrypt 标准）
static const char base64_chars[] = 
    "./ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

namespace BCrypt {

std::string generateHash(const std::string& password, int cost) {
    if (password.empty()) {
        return "";
    }
    if (cost < 4) cost = 4;
    if (cost > 31) cost = 31;
    
    // 构造盐：$2y$cost$ + 22个随机base64字符
    std::string salt = "$2y$" + std::to_string(cost) + "$";
    
    // 生成22个随机字符
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 63);
    for (int i = 0; i < 22; ++i) {
        salt += base64_chars[dis(gen)];
    }
    
    // 使用 crypt_r 生成哈希（线程安全）
    struct crypt_data data;
    memset(&data, 0, sizeof(data));
    data.initialized = 0;
    
    char* hash = crypt_r(password.c_str(), salt.c_str(), &data);
    if (!hash) {
        return "";
    }
    return std::string(hash);
}

bool validatePassword(const std::string& password, const std::string& storedHash) {
    if (password.empty() || storedHash.empty()) {
        return false;
    }
    // bcrypt 哈希长度至少为 28（例如 "$2y$10$..."）
    if (storedHash.length() < 28 || storedHash[0] != '$') {
        return false;
    }
    
    struct crypt_data data;
    memset(&data, 0, sizeof(data));
    data.initialized = 0;
    
    char* hash = crypt_r(password.c_str(), storedHash.c_str(), &data);
    if (!hash) {
        return false;
    }
    // 常数时间比较不是绝对必要（哈希值长度固定），但为了安全可以使用
    return std::string(hash) == storedHash;
}

} // namespace BCrypt