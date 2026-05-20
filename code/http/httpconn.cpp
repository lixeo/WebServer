#include "httpconn.h"
using namespace std;

const char* HttpConn::srcDir;
std::atomic<int> HttpConn::userCount;
bool HttpConn::isET;

HttpConn::HttpConn() { 
    fd_ = -1;
    addr_ = { 0 };
    isClose_ = true;
};

HttpConn::~HttpConn() { 
    Close(); 
};

void HttpConn::init(int fd, const sockaddr_in& addr) {
    assert(fd > 0);
    userCount++;
    addr_ = addr;
    fd_ = fd;
    writeBuff_.RetrieveAll();
    readBuff_.RetrieveAll();
    isClose_ = false;
    LOG_INFO("Client[%d](%s:%d) in, userCount:%d", fd_, GetIP(), GetPort(), (int)userCount);
}

void HttpConn::Close() {
    response_.UnmapFile();
    if(isClose_ == false){
        isClose_ = true; 
        userCount--;
        close(fd_);
        LOG_INFO("Client[%d](%s:%d) quit, UserCount:%d", fd_, GetIP(), GetPort(), (int)userCount);
    }
}

int HttpConn::GetFd() const {
    return fd_;
};

struct sockaddr_in HttpConn::GetAddr() const {
    return addr_;
}

const char* HttpConn::GetIP() const {
    return inet_ntoa(addr_.sin_addr);
}

int HttpConn::GetPort() const {
    return addr_.sin_port;
}

ssize_t HttpConn::read(int* saveErrno) {
    ssize_t len = -1;
    do {
        len = readBuff_.ReadFd(fd_, saveErrno);
        if (len <= 0) {
            break;
        }
    } while (isET); // ET:边沿触发要一次性全部读出
    return len;
}

// 主要采用writev连续写函数
ssize_t HttpConn::write(int* saveErrno) {
    ssize_t len = -1;
    do {
        len = writev(fd_, iov_, iovCnt_);   // 将iov的内容写到fd中
        if(len <= 0) {
            *saveErrno = errno;
            break;
        }
        // 已发送完所有数据
        if(iov_[0].iov_len + iov_[1].iov_len  == 0) { break; } 
        // 已发送的 len 大于 iov_[0] 的长度（即至少发完了头部，部分发送了文件）
        else if(static_cast<size_t>(len) > iov_[0].iov_len) {
            // 调整 iov_[1]：偏移量增加 (len - iov_[0].iov_len)
            iov_[1].iov_base = (uint8_t*) iov_[1].iov_base + (len - iov_[0].iov_len);
            iov_[1].iov_len -= (len - iov_[0].iov_len);
            // 头部已发完，清空 writeBuff_ 并置 iov_[0].iov_len = 0
            if(iov_[0].iov_len) {
                writeBuff_.RetrieveAll();
                iov_[0].iov_len = 0;
            }
        }
        else {
            // 只发送了部分头部
            iov_[0].iov_base = (uint8_t*)iov_[0].iov_base + len; 
            iov_[0].iov_len -= len; 
            writeBuff_.Retrieve(len);
        }
    } while(isET || ToWriteBytes() > 10240);
    return len;
}

bool HttpConn::process() {
    request_.Init();
    if (readBuff_.ReadableBytes() <= 0) {
        return false;
    }

    // 解析请求
    if (request_.parse(readBuff_)) {
        LOG_DEBUG("%s", request_.path().c_str());

        // 优先处理重定向（登录/注册成功后的跳转）
        if (request_.NeedRedirect()) {
            std::string redirectUrl = request_.GetRedirectUrl();
            writeBuff_.Append("HTTP/1.1 302 Found\r\n");
            writeBuff_.Append("Location: " + redirectUrl + "\r\n");
            writeBuff_.Append("Content-Length: 0\r\n");
            writeBuff_.Append("Connection: close\r\n");
            writeBuff_.Append("\r\n");
        }
        // 其次处理 API 请求
        else if (request_.IsApiRequest()) {
            handleApiRequest(writeBuff_);
        }
        // 最后处理普通静态文件或登录/注册页面（原有逻辑）
        else {
            response_.Init(srcDir, request_.path(), request_.IsKeepAlive(), 200);
            response_.MakeResponse(writeBuff_);
        }
    } else {
        // 解析失败，返回 400
        response_.Init(srcDir, request_.path(), false, 400);
        response_.MakeResponse(writeBuff_);
    }

    // 设置 iov 用于发送
    iov_[0].iov_base = const_cast<char*>(writeBuff_.Peek());
    iov_[0].iov_len = writeBuff_.ReadableBytes();
    iovCnt_ = 1;

    // 仅当不是重定向、不是API请求、且存在映射文件时，才附加文件内容
    if (!request_.NeedRedirect() && !request_.IsApiRequest() && response_.FileLen() > 0 && response_.File()) {
        iov_[1].iov_base = response_.File();
        iov_[1].iov_len = response_.FileLen();
        iovCnt_ = 2;
    }

    LOG_DEBUG("filesize:%ld, iovCnt:%d, toWrite:%ld", response_.FileLen(), iovCnt_, ToWriteBytes());
    return true;
}

// bool HttpConn::process() {
//     request_.Init();
//     if(readBuff_.ReadableBytes() <= 0) {
//         return false;
//     }
//     else if(request_.parse(readBuff_)) {    // 解析成功
//         LOG_DEBUG("%s", request_.path().c_str());
//         response_.Init(srcDir, request_.path(), request_.IsKeepAlive(), 200);
//     } else {
//         response_.Init(srcDir, request_.path(), false, 400);
//     }

//     response_.MakeResponse(writeBuff_); // 生成响应报文放入writeBuff_中
//     // 响应头
//     iov_[0].iov_base = const_cast<char*>(writeBuff_.Peek());
//     iov_[0].iov_len = writeBuff_.ReadableBytes();
//     iovCnt_ = 1;

//     // 文件
//     if(response_.FileLen() > 0  && response_.File()) {
//         iov_[1].iov_base = response_.File();
//         iov_[1].iov_len = response_.FileLen();
//         iovCnt_ = 2;
//     }
//     LOG_DEBUG("filesize:%d, %d  to %d", response_.FileLen() , iovCnt_, ToWriteBytes());
//     return true;
// }


std::string HttpConn::escapeJson(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

void HttpConn::sendJsonResponse(Buffer& buff, int httpCode, const std::string& json) {
    std::string statusLine;
    if (httpCode == 200) statusLine = "HTTP/1.1 200 OK\r\n";
    else if (httpCode == 400) statusLine = "HTTP/1.1 400 Bad Request\r\n";
    else if (httpCode == 404) statusLine = "HTTP/1.1 404 Not Found\r\n";
    else if (httpCode == 500) statusLine = "HTTP/1.1 500 Internal Server Error\r\n";
    else statusLine = "HTTP/1.1 200 OK\r\n";

    buff.Append(statusLine);
    buff.Append("Content-Type: application/json\r\n");
    buff.Append("Content-Length: " + std::to_string(json.size()) + "\r\n");
    buff.Append("Connection: close\r\n");
    buff.Append("\r\n");
    buff.Append(json);
}

void HttpConn::sendJsonError(Buffer& buff, int httpCode, const std::string& msg) {
    std::string json = "{\"code\":" + std::to_string(httpCode) + ",\"msg\":\"" + escapeJson(msg) + "\"}";
    sendJsonResponse(buff, httpCode, json);
}

void HttpConn::handleApiRequest(Buffer& writeBuff) {
    const std::string& method = request_.method();
    const std::string& path = request_.path();

    // 获取用户名
    std::string username;
    if (method == "GET") {
        username = request_.GetQueryParam("username");
    } else if (method == "POST") {
        username = request_.GetPost("username");
    }

    if (username.empty()) {
        sendJsonError(writeBuff, 400, "Missing username");
        return;
    }

    // 获取数据库连接
    MYSQL* sql = nullptr;
    SqlConnRAII(&sql, SqlConnPool::Instance());
    if (!sql) {
        sendJsonError(writeBuff, 500, "Database connection failed");
        return;
    }

    // ----- 处理 GET /api/user/info -----
    if (path == "/api/user/info" && method == "GET") {
        // 查询语句增加 gender, birthday 字段
        const char* query = "SELECT nickname, avatar, bio, email, gender, birthday, DATE_FORMAT(created_at, '%Y-%m-%d %H:%i:%s') FROM user WHERE username=?";
        MYSQL_STMT* stmt = mysql_stmt_init(sql);
        if (!stmt || mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
            sendJsonError(writeBuff, 500, "Database prepare error");
            if (stmt) mysql_stmt_close(stmt);
            return;
        }

        MYSQL_BIND param;
        memset(&param, 0, sizeof(param));
        param.buffer_type = MYSQL_TYPE_STRING;
        param.buffer = (void*)username.c_str();
        param.buffer_length = username.length();
        mysql_stmt_bind_param(stmt, &param);

        if (mysql_stmt_execute(stmt) != 0) {
            sendJsonError(writeBuff, 500, "Database execute error");
            mysql_stmt_close(stmt);
            return;
        }

        // 绑定结果：增加 gender 和 birthday
        char nickname[65] = {0}, avatar[257] = {0}, bio[1024] = {0}, email[129] = {0}, gender[16] = {0}, birthday[16] = {0}, created[32] = {0};
        unsigned long lengths[7];  // 对应7个字段
        MYSQL_BIND result[7];
        memset(result, 0, sizeof(result));
        for (int i = 0; i < 7; ++i) {
            result[i].buffer_type = MYSQL_TYPE_STRING;
            result[i].length = &lengths[i];
        }
        result[0].buffer = nickname;    result[0].buffer_length = sizeof(nickname);
        result[1].buffer = avatar;      result[1].buffer_length = sizeof(avatar);
        result[2].buffer = bio;         result[2].buffer_length = sizeof(bio);
        result[3].buffer = email;       result[3].buffer_length = sizeof(email);
        result[4].buffer = gender;      result[4].buffer_length = sizeof(gender);
        result[5].buffer = birthday;    result[5].buffer_length = sizeof(birthday);
        result[6].buffer = created;     result[6].buffer_length = sizeof(created);
        mysql_stmt_bind_result(stmt, result);

        int fetch_ret = mysql_stmt_fetch(stmt);
        if (fetch_ret != 0) {
            sendJsonError(writeBuff, 404, "User not found");
            mysql_stmt_close(stmt);
            return;
        }
        mysql_stmt_close(stmt);

        // 构造 JSON 响应，增加 gender 和 birthday
        std::string json = "{"
            "\"code\":200,"
            "\"data\":{"
            "\"nickname\":\"" + escapeJson(nickname) + "\","
            "\"avatar\":\"" + escapeJson(avatar) + "\","
            "\"bio\":\"" + escapeJson(bio) + "\","
            "\"email\":\"" + escapeJson(email) + "\","
            "\"gender\":\"" + escapeJson(gender) + "\","
            "\"birthday\":\"" + escapeJson(birthday) + "\","
            "\"created_at\":\"" + escapeJson(created) + "\""
            "}}";
        sendJsonResponse(writeBuff, 200, json);
        return;
    }

    // ----- POST /api/user/update -----
    if (path == "/api/user/update" && method == "POST") {
        // 使用 GetPost 获取所有字段（已自动 URL 解码）
        std::string nickname = request_.GetPost("nickname");
        std::string avatar   = request_.GetPost("avatar");
        std::string bio      = request_.GetPost("bio");
        std::string email    = request_.GetPost("email");
        std::string gender   = request_.GetPost("gender");
        std::string birthday = request_.GetPost("birthday");

        if (nickname.empty()) nickname = username;

        const char* update_query = "UPDATE user SET nickname=?, avatar=?, bio=?, email=?, gender=?, birthday=? WHERE username=?";
        MYSQL_STMT* stmt = mysql_stmt_init(sql);
        if (!stmt || mysql_stmt_prepare(stmt, update_query, strlen(update_query)) != 0) {
            sendJsonError(writeBuff, 500, "Database prepare error");
            if (stmt) mysql_stmt_close(stmt);
            return;
        }

        MYSQL_BIND bind[7];
        memset(bind, 0, sizeof(bind));
        bind[0].buffer_type = MYSQL_TYPE_STRING; bind[0].buffer = (void*)nickname.c_str(); bind[0].buffer_length = nickname.length();
        bind[1].buffer_type = MYSQL_TYPE_STRING; bind[1].buffer = (void*)avatar.c_str();   bind[1].buffer_length = avatar.length();
        bind[2].buffer_type = MYSQL_TYPE_STRING; bind[2].buffer = (void*)bio.c_str();       bind[2].buffer_length = bio.length();
        bind[3].buffer_type = MYSQL_TYPE_STRING; bind[3].buffer = (void*)email.c_str();     bind[3].buffer_length = email.length();
        bind[4].buffer_type = MYSQL_TYPE_STRING; bind[4].buffer = (void*)gender.c_str();    bind[4].buffer_length = gender.length();

        bool is_null_true = true;
        if (birthday.empty()) {
            bind[5].buffer_type = MYSQL_TYPE_NULL;
            bind[5].is_null = &is_null_true;
        } else {
            bind[5].buffer_type = MYSQL_TYPE_STRING;
            bind[5].buffer = (void*)birthday.c_str();
            bind[5].buffer_length = birthday.length();
        }

        bind[6].buffer_type = MYSQL_TYPE_STRING; bind[6].buffer = (void*)username.c_str(); bind[6].buffer_length = username.length();

        if (mysql_stmt_bind_param(stmt, bind) != 0) {
            sendJsonError(writeBuff, 500, "Bind parameters failed");
            mysql_stmt_close(stmt);
            return;
        }

        int ret = mysql_stmt_execute(stmt);
        mysql_stmt_close(stmt);

        if (ret == 0) {
            sendJsonResponse(writeBuff, 200, "{\"code\":200,\"msg\":\"success\"}");
        } else {
            LOG_ERROR("Database update failed for user %s", username.c_str());
            sendJsonError(writeBuff, 500, "Database update failed");
        }
        return;
    }

    sendJsonError(writeBuff, 404, "API not found");
}
