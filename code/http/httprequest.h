#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include <unordered_map>
#include <unordered_set>
#include <string>
#include <regex>    // 正则表达式
#include <errno.h>     
#include <mysql/mysql.h>  //mysql

#include "../buffer/buffer.h"
#include "../log/log.h"
#include "../pool/sqlconnpool.h"

class HttpRequest {
public:
    enum PARSE_STATE {    //公有枚举,表示解析过程所处的阶段,是一个状态机
        REQUEST_LINE,
        HEADERS,
        BODY,
        FINISH,        
    };
    
    HttpRequest() { Init(); }
    ~HttpRequest() = default;

    void Init(); 
    bool parse(Buffer& buff);   //解析入口,从 buff 中读取数据,按HTTP协议解析,直到解析完一个完整的请求或数据不足
    
    // 获取请求的路径/方法/HTTP版本
    std::string path() const;
    std::string& path();
    std::string method() const;
    std::string version() const;

    // 对于POST请求,通过键名获取对应的值
    std::string GetPost(const std::string& key) const;
    std::string GetPost(const char* key) const;
    
    // 根据HTTP版本和Connection请求头,判断是否保持连接
    bool IsKeepAlive() const;

private:
    bool ParseRequestLine_(const std::string& line);    // 处理请求行
    void ParseHeader_(const std::string& line);         // 处理请求头
    void ParseBody_(const std::string& line);           // 处理请求体

    void ParsePath_();                                  // 处理请求路径
    void ParsePost_();                                  // 处理Post事件
    void ParseFromUrlencoded_();                        // 从url种解析编码
    
    // 静态方法,用于验证用户名和密码,链接MySQL数据库(通过 sqlConnPool )查询或插入用户记录,isLogin 为 true 表示登陆验证,false表示注册
    static bool UserVerify(const std::string& name, const std::string& pwd, bool isLogin);  // 用户验证

    PARSE_STATE state_;                                                   // 当前解析状态
    std::string method_, path_, version_, body_;                          // 请求行解析结果
    std::unordered_map<std::string, std::string> header_;                 // 请求头键值对 <字段名, 值>
    std::unordered_map<std::string, std::string> post_;                   // POST 键值对 <键, 值>

    static const std::unordered_set<std::string> DEFAULT_HTML;            // 集合,包括某些预设的HTML页面名称
    static const std::unordered_map<std::string, int> DEFAULT_HTML_TAG;   // 映射,为预设页面赋予证书编号或状态码
    static int ConverHex(char ch);  // 16进制转换为10进制
};

#endif