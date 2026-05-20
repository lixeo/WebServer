#include "bcrypt.h"
#include <iostream>
#include <string>

int main() {
    std::string password = "mySecret123";
    
    // 生成哈希
    std::string hash = BCrypt::generateHash(password, 10);
    std::cout << "Generated hash: " << hash << std::endl;
    
    // 验证正确密码
    bool ok = BCrypt::validatePassword(password, hash);
    std::cout << "Valid password: " << (ok ? "PASS" : "FAIL") << std::endl;
    
    // 验证错误密码
    bool fail = BCrypt::validatePassword("wrongPassword", hash);
    std::cout << "Wrong password: " << (!fail ? "PASS" : "FAIL") << std::endl;
    
    return 0;
}