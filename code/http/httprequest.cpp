#include "httprequest.h"
#include "../bcrypt/bcrypt.h"

using namespace std;

// 网页名称，和一般的前端跳转不同，这里需要将请求信息放到后端来验证一遍再上传（和小组成员还起过争执）
// 预定义的静态成员:不需要登陆即可访问的默认页面列表
const unordered_set<string> HttpRequest::DEFAULT_HTML {
    "/index", "/register", "/login", "/welcome", "/video", "/picture",
};

// 登录/注册
const unordered_map<string, int> HttpRequest::DEFAULT_HTML_TAG {
    {"/login.html", 1}, {"/register.html", 0}
};

// 初始化操作，一些清零操作
void HttpRequest::Init() {
    state_ = REQUEST_LINE;  // 初始状态
    method_ = path_ = version_= body_ = "";
    header_.clear();
    post_.clear();
    // 新增：API 标志和查询字符串的初始化
    isApiRequest_ = false;
    queryString_.clear();
    redirectUrl_.clear();
}

// 解析处理
bool HttpRequest::parse(Buffer& buff) {
    const char END[] = "\r\n";
    if(buff.ReadableBytes() == 0)   // 没有可读的字节
        return false;
    // 读取数据开始,状态机循环:直到解析完成或数据不足
    while(buff.ReadableBytes() && state_ != FINISH) {
        // 从buff中的读指针开始到读指针结束，这块区域是未读取得数据并去处"\r\n"，返回有效数据得行末指针
        // 在缓冲区中查找第一个 "\r\n" 的位置,将这一行(不含 \r\\n )提取为string
        const char* lineend = search(buff.Peek(), buff.BeginWriteConst(), END, END+2);
        string line(buff.Peek(), lineend);

        switch (state_)
        {
        case REQUEST_LINE:
            // 解析错误-请求格式错误
            if(!ParseRequestLine_(line)) {
                return false;
            }
            ParsePath_();   // 解析并规范化路径
            break;
        case HEADERS:
            ParseHeader_(line);
            if(buff.ReadableBytes() <= 2) {  // 说明是get请求，后面为\r\n
                state_ = FINISH;   // 提前结束
            }
            break;
        case BODY:
            ParseBody_(line);
            break;
        default:
            break;
        }
        if(lineend == buff.BeginWrite()) {  // 读完了
            buff.RetrieveAll();
            break;
        }
        buff.RetrieveUntil(lineend + 2);        // 跳过回车换行
    }
    LOG_DEBUG("[%s], [%s], [%s]", method_.c_str(), path_.c_str(), version_.c_str());
    return true;
}

bool HttpRequest::ParseRequestLine_(const string& line) {
    regex patten("^([^ ]*) ([^ ]*) HTTP/([^ ]*)$");
    smatch Match;
    if(regex_match(line, Match, patten)) {
        method_ = Match[1];
        string full_path = Match[2];          // 完整的路径（可能包含 ?query）
        version_ = Match[3];
        
        // 分离路径和查询字符串
        size_t qpos = full_path.find('?');
        if (qpos != string::npos) {
            path_ = full_path.substr(0, qpos);
            queryString_ = full_path.substr(qpos + 1);
        } else {
            path_ = full_path;
            queryString_.clear();
        }
        
        state_ = HEADERS;
        return true;
    }
    LOG_ERROR("RequestLine Error");
    return false;
}

// bool HttpRequest::ParseRequestLine_(const string& line) {
//     // 正则表达式:三个捕获组分别是方法/路径/版本
//     regex patten("^([^ ]*) ([^ ]*) HTTP/([^ ]*)$");
//     smatch Match;   // 用来匹配patten得到结果
//     // 在匹配规则中，以括号()的方式来划分组别 一共三个括号 [0]表示整体
//     if(regex_match(line, Match, patten)) {  // 匹配指定字符串整体是否符合
//         method_ = Match[1];
//         path_ = Match[2];
//         version_ = Match[3];
//         state_ = HEADERS;
//         return true;
//     }
//     LOG_ERROR("RequestLine Error");
//     return false;
// }

// 解析路径，统一一下path名称,方便后面解析资源
void HttpRequest::ParsePath_() {
    if(path_ == "/") {
        path_ = "/index.html";
    } else {
        if(DEFAULT_HTML.find(path_) != DEFAULT_HTML.end()) {
            path_ += ".html";
        }
    }
    // ---------- 新增：判断是否为 API 请求 ----------
    isApiRequest_ = (path_.find("/api/") == 0);
}

void HttpRequest::ParseHeader_(const string& line) {
    regex patten("^([^:]*): ?(.*)$");
    smatch Match;
    if(regex_match(line, Match, patten)) {
        header_[Match[1]] = Match[2];
    } else {    // 匹配失败说明首部行匹配完了，状态变化
        state_ = BODY;
    }
}

void HttpRequest::ParseBody_(const string& line) {
    body_ = line;
    ParsePost_();
    state_ = FINISH;    // 状态转换为下一个状态
    LOG_DEBUG("Body:%s, len:%d", line.c_str(), line.size());
}


// 16进制转化为10进制
int HttpRequest::ConverHex(char ch) {
    if(ch >= 'A' && ch <= 'F') 
        return ch -'A' + 10;
    if(ch >= 'a' && ch <= 'f') 
        return ch -'a' + 10;
    return ch;
}

void HttpRequest::ParsePost_() {
    // 安全获取 Content-Type 头（避免使用 operator[] 自动插入）
    std::string contentType;
    auto it = header_.find("Content-Type");
    if (it != header_.end()) {
        contentType = it->second;
    }
    LOG_INFO("ParsePost_ called, method=%s, Content-Type=%s", method_.c_str(), contentType.c_str());

    // 检查方法且 Content-Type 前缀匹配（忽略 charset 等参数）
    if (method_ == "POST" && !contentType.empty() 
            && contentType.find("application/x-www-form-urlencoded") == 0) {
        LOG_INFO("Entering ParseFromUrlencoded_");
        ParseFromUrlencoded_();     // 解析请求体

        // 登录/注册页面处理
        if (DEFAULT_HTML_TAG.count(path_)) {
            int tag = DEFAULT_HTML_TAG.find(path_)->second; 
            LOG_DEBUG("Tag:%d", tag);
            if (tag == 0 || tag == 1) {
                bool isLogin = (tag == 1);
                if (UserVerify(post_["username"], post_["password"], isLogin)) {
                    // 成功时，设置重定向 URL（带用户名参数）
                    SetRedirectUrl("/welcome.html?username=" + post_["username"]);
                } else {
                    path_ = "/error.html";
                }
            }
        }
    }   
}
// // 处理post请求
// void HttpRequest::ParsePost_() {
//     LOG_INFO("ParsePost_ called, method=%s, Content-Type=%s", method_.c_str(), header_["Content-Type"].c_str());
//     if(method_ == "POST" && header_["Content-Type"] == "application/x-www-form-urlencoded") {
//         LOG_INFO("Entering ParseFromUrlencoded_");
//         ParseFromUrlencoded_();     // POST请求体示例
//         if(DEFAULT_HTML_TAG.count(path_)) { // 如果是登录/注册的path
//             int tag = DEFAULT_HTML_TAG.find(path_)->second; 
//             LOG_DEBUG("Tag:%d", tag);
//             if(tag == 0 || tag == 1) {
//                 bool isLogin = (tag == 1);
//                 if(UserVerify(post_["username"], post_["password"], isLogin)) {
//                     // 成功时，设置重定向 URL（带用户名参数）
//                     SetRedirectUrl("/welcome.html?username=" + post_["username"]);
//                    // 注意：不再修改 path_
//                 } 
//                 else {
//                     path_ = "/error.html";
//                 }
//             }
//         }
//     }   
// }

static int HexCharToInt(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1;
}

static std::string UrlDecode(const std::string& src) {
    std::string res;
    for (size_t i = 0; i < src.size(); ++i) {
        if (src[i] == '%' && i + 2 < src.size()) {
            int high = HexCharToInt(src[i+1]);
            int low  = HexCharToInt(src[i+2]);
            if (high != -1 && low != -1) {
                res += static_cast<char>((high << 4) | low);
                i += 2;
            } else {
                res += src[i];
            }
        } else if (src[i] == '+') {
            res += ' ';
        } else {
            res += src[i];
        }
    }
    return res;
}

void HttpRequest::ParseFromUrlencoded_() {
    if (body_.empty()) {
        LOG_DEBUG("ParseFromUrlencoded_: body is empty");
        return;
    }

    LOG_DEBUG("ParseFromUrlencoded_: body = %s", body_.c_str());

    size_t start = 0;
    int pairCount = 0;
    while (start < body_.size()) {
        size_t end = body_.find('&', start);
        if (end == std::string::npos) end = body_.size();

        std::string pair = body_.substr(start, end - start);
        size_t eq = pair.find('=');
        if (eq != std::string::npos) {
            std::string raw_key = pair.substr(0, eq);
            std::string raw_value = pair.substr(eq + 1);
            std::string key = UrlDecode(raw_key);
            std::string value = UrlDecode(raw_value);
            post_[key] = value;
            pairCount++;
            LOG_DEBUG("Parsed pair %d: raw_key='%s' raw_value='%s' -> key='%s' value='%s'",
                      pairCount, raw_key.c_str(), raw_value.c_str(), key.c_str(), value.c_str());
        } else {
            LOG_DEBUG("Skipped invalid pair (no '='): %s", pair.c_str());
        }
        start = end + 1;
    }

    LOG_DEBUG("ParseFromUrlencoded_ finished, total %d pairs parsed, post_ size = %zu", pairCount, post_.size());

    // 可选：打印所有 post_ 中的键，方便调试
    for (auto& kv : post_) {
        LOG_DEBUG("post_ final: %s = %s", kv.first.c_str(), kv.second.c_str());
    }
}

// void HttpRequest::ParseFromUrlencoded_() {
//     if (body_.empty()) return;

//     size_t start = 0;
//     while (start < body_.size()) {
//         // 找到下一个 '&' 分隔符
//         size_t end = body_.find('&', start);
//         if (end == std::string::npos) end = body_.size();

//         std::string pair = body_.substr(start, end - start);
//         size_t eq = pair.find('=');
//         if (eq != std::string::npos) {
//             std::string key = pair.substr(0, eq);
//             std::string value = pair.substr(eq + 1);
//             // URL 解码后存储
//             post_[UrlDecode(key)] = UrlDecode(value);
//         }
//         start = end + 1;
//     }
// }

// // 从url中解析编码
// void HttpRequest::ParseFromUrlencoded_() {
//     if(body_.size() == 0) { return; }

//     string key, value;
//     int num = 0;
//     int n = body_.size();
//     int i = 0, j = 0;

//     for(; i < n; i++) {
//         char ch = body_[i];
//         switch (ch) {
//         case '=':
//             key = body_.substr(j, i - j);
//             j = i + 1;
//             break;
//         case '+':
//             body_[i] = ' ';
//             break;
//         case '%':
//             num = ConverHex(body_[i + 1]) * 16 + ConverHex(body_[i + 2]);
//             body_[i + 2] = num % 10 + '0';
//             body_[i + 1] = num / 10 + '0';
//             i += 2;
//             break;
//         case '&':
//             value = body_.substr(j, i - j);
//             j = i + 1;
//             post_[key] = value;
//             LOG_DEBUG("%s = %s", key.c_str(), value.c_str());
//             break;
//         default:
//             break;
//         }
//     }
//     assert(j <= i);
//     if(post_.count(key) == 0 && j < i) {
//         value = body_.substr(j, i - j);
//         post_[key] = value;
//     }
// }

/**
 * 用户验证（登录 / 注册）
 * @param name    用户名
 * @param pwd     明文密码
 * @param isLogin true=登录，false=注册
 * @return true=验证通过，false=失败
 */
bool HttpRequest::UserVerify(const string &name, const string &pwd, bool isLogin) {
    // 基本校验：用户名和密码不能为空
    if (name.empty() || pwd.empty()) {
        LOG_WARN("Username or password empty");
        return false;
    }
    LOG_INFO("Verify name: %s", name.c_str());   // 日志中不输出密码，防止泄露

    // 从连接池获取 MySQL 连接（RAII 自动管理生命周期）
    MYSQL* sql = nullptr;
    SqlConnRAII(&sql, SqlConnPool::Instance());
    if (!sql) {
        LOG_ERROR("Failed to get SQL connection");
        return false;
    }

    // ========== 1. 使用预编译语句查询用户 ==========
    const char* select_query = "SELECT password FROM user WHERE username = ? LIMIT 1";
    MYSQL_STMT* stmt = mysql_stmt_init(sql);
    if (!stmt) {
        LOG_ERROR("mysql_stmt_init failed: %s", mysql_error(sql));
        return false;
    }
    if (mysql_stmt_prepare(stmt, select_query, strlen(select_query)) != 0) {
        LOG_ERROR("mysql_stmt_prepare error: %s", mysql_stmt_error(stmt));
        return false;
    }

    // 绑定输入参数（用户名）
    MYSQL_BIND param;
    memset(&param, 0, sizeof(param));
    param.buffer_type = MYSQL_TYPE_STRING;
    param.buffer = (void*)name.c_str();
    param.buffer_length = name.length();
    if (mysql_stmt_bind_param(stmt, &param) != 0) {
        LOG_ERROR("mysql_stmt_bind_param error: %s", mysql_stmt_error(stmt));
        return false;
    }

    // 执行查询
    if (mysql_stmt_execute(stmt) != 0) {
        LOG_ERROR("mysql_stmt_execute error: %s", mysql_stmt_error(stmt));
        return false;
    }

    // 绑定结果列（存储密码哈希）
    char hash_from_db[128] = {0};
    unsigned long hash_len = 0;
    MYSQL_BIND result;
    memset(&result, 0, sizeof(result));
    result.buffer_type = MYSQL_TYPE_STRING;
    result.buffer = hash_from_db;
    result.buffer_length = sizeof(hash_from_db);
    result.length = &hash_len;
    if (mysql_stmt_bind_result(stmt, &result) != 0) {
        LOG_ERROR("mysql_stmt_bind_result error: %s", mysql_stmt_error(stmt));
        return false;
    }

    // 获取查询结果
     bool user_exists = (mysql_stmt_fetch(stmt) == 0);
    // 注意：mysql_stmt_fetch 返回 0 表示成功取到一行，返回 1 表示错误，返回 100 表示无数据
    if (mysql_stmt_errno(stmt) != 0 && mysql_stmt_errno(stmt) != 100) {
        LOG_ERROR("mysql_stmt_fetch error: %s", mysql_stmt_error(stmt));
        return false;
    }

    // ========== 2. 登录逻辑 ==========
    if (isLogin) {
        if (!user_exists) {
            LOG_INFO("User %s not found", name.c_str());
            return false;                // 用户名不存在，返回失败（不区分具体原因）
        }
        // 使用 bcrypt 验证密码（哈希值存储在 hash_from_db 中）
        bool valid = BCrypt::validatePassword(pwd, hash_from_db);
        if (!valid) {
            LOG_INFO("Password mismatch for %s", name.c_str());
        }
        return valid;
    }

    // ========== 3. 注册逻辑 ==========
    else {
        if (user_exists) {
            LOG_INFO("User %s already exists", name.c_str());
            return false;                // 用户名已存在，注册失败（不提示具体原因）
        }

        // 生成 bcrypt 哈希（cost 因子=10，推荐生产环境使用 12）
        std::string hashed = BCrypt::generateHash(pwd, 10);
        if (hashed.empty()) {
            LOG_ERROR("Failed to generate bcrypt hash for user %s", name.c_str());
            return false;
        }

        // 默认值设置
        std::string defaultNick = name;          // 昵称默认同用户名
        std::string empty = "";
        std::string defaultGender = "other";
        // birthday 默认 NULL，用 MYSQL_TYPE_NULL 表示

        // 使用预编译语句插入新用户
        const char* insert_query = "INSERT INTO user(username, password, nickname, avatar, bio, email, gender, birthday) VALUES(?, ?, ?, ?, ?, ?, ?, ?)";
        MYSQL_STMT* insert_stmt = mysql_stmt_init(sql);
        if (!insert_stmt) {
            LOG_ERROR("mysql_stmt_init for insert failed: %s", mysql_error(sql));
            return false;
        }
        if (mysql_stmt_prepare(insert_stmt, insert_query, strlen(insert_query)) != 0) {
            LOG_ERROR("mysql_stmt_prepare insert error: %s", mysql_stmt_error(insert_stmt));
            return false;
        }

        MYSQL_BIND insert_bind[8];
        memset(insert_bind, 0, sizeof(insert_bind));

        // 绑定用户名
        insert_bind[0].buffer_type = MYSQL_TYPE_STRING;
        insert_bind[0].buffer = (void*)name.c_str();
        insert_bind[0].buffer_length = name.length();

        // 绑定 bcrypt 哈希
        insert_bind[1].buffer_type = MYSQL_TYPE_STRING;
        insert_bind[1].buffer = (void*)hashed.c_str();
        insert_bind[1].buffer_length = hashed.length();

        insert_bind[2].buffer_type = MYSQL_TYPE_STRING;
        insert_bind[2].buffer = (void*)defaultNick.c_str();
        insert_bind[2].buffer_length = defaultNick.length();

        insert_bind[3].buffer_type = MYSQL_TYPE_STRING;
        insert_bind[3].buffer = (void*)empty.c_str();
        insert_bind[3].buffer_length = empty.length();

        insert_bind[4].buffer_type = MYSQL_TYPE_STRING;
        insert_bind[4].buffer = (void*)empty.c_str();
        insert_bind[4].buffer_length = empty.length();

        insert_bind[5].buffer_type = MYSQL_TYPE_STRING;
        insert_bind[5].buffer = (void*)empty.c_str();
        insert_bind[5].buffer_length = empty.length();

        insert_bind[6].buffer_type = MYSQL_TYPE_STRING;
        insert_bind[6].buffer = (void*)defaultGender.c_str();
        insert_bind[6].buffer_length = defaultGender.length();

        // birthday 字段：设置为 NULL
        bool is_null_true = true; 
        insert_bind[7].buffer_type = MYSQL_TYPE_NULL;
        insert_bind[7].is_null = &is_null_true;

        if (mysql_stmt_bind_param(insert_stmt, insert_bind) != 0) {
            LOG_ERROR("mysql_stmt_bind_param insert error: %s", mysql_stmt_error(insert_stmt));
            return false;
        }

        bool insert_ok = (mysql_stmt_execute(insert_stmt) == 0);

        if (insert_ok) {
            LOG_INFO("User %s registered successfully", name.c_str());
        } else {
            LOG_ERROR("Failed to insert user %s", name.c_str());
        }
        return insert_ok;
    }
}

std::string HttpRequest::path() const{
    return path_;
}

std::string& HttpRequest::path(){
    return path_;
}
std::string HttpRequest::method() const {
    return method_;
}

std::string HttpRequest::version() const {
    return version_;
}

std::string HttpRequest::GetPost(const std::string& key) const {
    assert(key != "");
    if(post_.count(key) == 1) {
        return post_.find(key)->second;
    }
    return "";
}

std::string HttpRequest::GetPost(const char* key) const {
    assert(key != nullptr);
    if(post_.count(key) == 1) {
        return post_.find(key)->second;
    }
    return "";
}

// 判断是否保持连接(HTTP Keep-Alive)
bool HttpRequest::IsKeepAlive() const {
    if(header_.count("Connection") == 1) {
        return header_.find("Connection")->second == "keep-alive" && version_ == "1.1";
    }
    return false;
}

std::string HttpRequest::GetQueryParam(const std::string& key) const {
    if (queryString_.empty()) return "";
    // 解析 queryString_，格式为 key1=value1&key2=value2
    size_t start = 0;
    while (start < queryString_.size()) {
        size_t eq = queryString_.find('=', start);
        if (eq == std::string::npos) break;
        size_t amp = queryString_.find('&', eq);
        if (amp == std::string::npos) amp = queryString_.size();
        std::string k = queryString_.substr(start, eq - start);
        std::string v = queryString_.substr(eq + 1, amp - eq - 1);
        if (k == key) {
            // URL 解码
            std::string decoded;
            for (size_t i = 0; i < v.size(); ++i) {
                if (v[i] == '%' && i + 2 < v.size()) {
                    int num = ConverHex(v[i+1]) * 16 + ConverHex(v[i+2]);
                    decoded += static_cast<char>(num);
                    i += 2;
                } else if (v[i] == '+') {
                    decoded += ' ';
                } else {
                    decoded += v[i];
                }
            }
            return decoded;
        }
        start = amp + 1;
    }
    return "";
}