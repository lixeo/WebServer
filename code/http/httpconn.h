#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include <sys/types.h>
#include <sys/uio.h>     // readv/writev
#include <arpa/inet.h>   // sockaddr_in
#include <stdlib.h>      // atoi()
#include <errno.h>      

#include "../log/log.h"
#include "../buffer/buffer.h"
#include "httprequest.h"
#include "httpresponse.h"

/**
 * HTTP 连接类
 * 负责单个客户端连接的生命周期管理：读取请求数据，解析请求，生成响应，发送响应。
 * 支持边缘触发（ET）和非阻塞 I/O。
 */
class HttpConn {
public:
    HttpConn();
    ~HttpConn();
    
    /**
     * 初始化连接对象，绑定 socket 和地址
     * sockFd 已建立连接的 socket 文件描述符
     * addr 客户端地址信息
     */
    void init(int sockFd, const sockaddr_in& addr);

    /**
     * 从 socket 非阻塞读取数据到读缓冲区
     * saveErrno 用于保存发生的错误码（errno）
     * 读取的字节数，-1 表示出错
     */
    ssize_t read(int* saveErrno);

    /**
     * 将写缓冲区中的数据非阻塞写入 socket
     * saveErrno 用于保存发生的错误码
     * 写入的字节数，-1 表示出错
     */
    ssize_t write(int* saveErrno);
    void Close();                     // 关闭连接,释放资源
    int GetFd() const;                // 获取 socket fd
    int GetPort() const;              // 获取客户端端口
    const char* GetIP() const;        // 获取客户端 IP 字符串
    sockaddr_in GetAddr() const;      // 获取客户端地址结构体

    /**
     * 处理 HTTP 请求：解析请求，生成响应
     * 是否成功处理（如果解析失败返回 false）
     */
    bool process();

    // 写的总长度, 返回待发送的字节总数（包括头部和文件内容
    int ToWriteBytes() { 
        return iov_[0].iov_len +  iov_[1].iov_len; 
    }

    // 判断连接是否保持(Keep-Alive)
    bool IsKeepAlive() const {
        return request_.IsKeepAlive();
    }

    // 静态成员: 整个服务器共享的配置
    static bool isET;                   // 是否使用边缘触发(Epoll ET)
    static const char* srcDir;          // 静态文件根目录
    static std::atomic<int> userCount;  // 当前活跃连接数, 原子操作，支持锁
    
private:
   
    int fd_;                            // 连接的 socket 文件描述符
    struct  sockaddr_in addr_;          // 客户端地址

    bool isClose_;                      // 连接是否关闭
    
    int iovCnt_;                        // 实际使用的 iovec 个数 (1 或 2)
    struct iovec iov_[2];               // 用于 writev 的分散写结构, iov_[0] 指向响应头部（writeBuff_）, iov_[1] 指向响应体
    
    Buffer readBuff_;                   // 读缓冲区: 存储从 socket 读到的原始数据
    Buffer writeBuff_;                  // 写缓冲区: 存储要发送的响应头部(以及可能的小响应体)

    HttpRequest request_;               // 请求解析器
    HttpResponse response_;             // 响应生成器

    // 处理 API 请求（/api/*）
    void handleApiRequest(Buffer& writeBuff);
    
    // 辅助函数：发送 JSON 响应
    void sendJsonResponse(Buffer& buff, int httpCode, const std::string& json);
    void sendJsonError(Buffer& buff, int httpCode, const std::string& msg);
    
    // 辅助函数：JSON 字符串转义
    std::string escapeJson(const std::string& s);
};


#endif //HTTP_CONN_H