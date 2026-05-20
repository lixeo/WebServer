#ifndef BCRYPT_H
#define BCRYPT_H

#include <string>

namespace BCrypt {

// 生成 bcrypt 哈希
// password: 明文密码
// cost:     work factor (4-31)，推荐 10-12，默认 10
// 成功返回 bcrypt 哈希字符串，失败返回空字符串
std::string generateHash(const std::string& password, int cost = 10);

// 验证密码
// password: 明文密码
// hash:     数据库中存储的 bcrypt 哈希
// 返回 true 表示密码匹配
bool validatePassword(const std::string& password, const std::string& hash);

} // namespace BCrypt

#endif