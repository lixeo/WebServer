#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include <unordered_map>
#include <fcntl.h>       // open
#include <unistd.h>      // close
#include <sys/stat.h>    // stat
#include <sys/mman.h>    // mmap, munmap

#include "../buffer/buffer.h"
#include "../log/log.h"

class HttpResponse {
public:
    HttpResponse();
    ~HttpResponse();

    /**
     * 初始化响应参数
     * srcDir 静态文件根目录（例如 "/var/www/html"）
     * path   请求的资源路径（例如 "/index.html"）
     * isKeepAlive 是否保持连接
     * code   HTTP 状态码（例如 200, 404），-1 表示根据资源是否存在自动决定
     */
    void Init(const std::string& srcDir, std::string& path, bool isKeepAlive = false, int code = -1);
    
    /**
     * 生成响应报文并写入 Buffer
     * buff 输出缓冲区（类中定义的 Buffer）
     * 
     * 该函数会依次调用 AddStateLine_、AddHeader_、AddContent_，
     * 将完整的响应（状态行+头部+正文）追加到 buff 中。
     */
    void MakeResponse(Buffer& buff);
    void UnmapFile();                  // 解除内存映射(如果 mmFile_ 不为空)
    char* File();                      // 获取内存映射的文件内容指针(用于响应正文)
    size_t FileLen() const;            // 获取文件大小(字节)

    /**
     * 生成错误响应的正文内容（纯文本）
     * buff 输出缓冲区
     * message 错误描述字符串
     */
    void ErrorContent(Buffer& buff, std::string message);
    int Code() const { return code_; }  // 获取 HTTP 状态码

private:
    void AddStateLine_(Buffer &buff);   // 添加状态行到缓冲区,例如 "HTTP/1.1 200 OK\r\n"
    void AddHeader_(Buffer &buff);      // 添加响应头到缓冲区,包括 Connection、Content-Type、Content-Length 等
    void AddContent_(Buffer &buff);     // 添加响应正文（如果是静态文件则拷贝映射内容，否则是错误页面）

    void ErrorHtml_();                  // 根据状态码生成错误页面 HTML 内容(写入 mmFile_)
    std::string GetFileType_();         // 根据文件后缀返回 MIME 类型（例如 ".html" -> "text/html"）

    int code_;                          // HTTP 状态码, 如 200 , 404
    bool isKeepAlive_;                  // 是否保持连接（对应 Connection: keep-alive）

    std::string path_;                  // 请求资源的路径（例如 "/index.html"）
    std::string srcDir_;                // 静态文件根目录
    
    char* mmFile_;                      // 通过 mmap 映射的文件内存指针
    struct stat mmFileStat_;            // 文件状态信息(用于获取文件大小等)

    static const std::unordered_map<std::string, std::string> SUFFIX_TYPE;  // 后缀类型集
    static const std::unordered_map<int, std::string> CODE_STATUS;          // 编码状态集
    static const std::unordered_map<int, std::string> CODE_PATH;            // 编码路径集
};


#endif //HTTP_RESPONSE_H
