#pragma once

#include <string>
#include <windows.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

class HttpClient {
public:
    HttpClient();
    ~HttpClient();
    
    // 禁用拷贝
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;
    
    // 初始化
    bool Initialize();
    
    // 发送 GET 请求（同步）
    std::string GetSync(const std::wstring& host,
                        const std::wstring& path,
                        int port,
                        bool isHttps,
                        bool& success);
    
    // 设置超时（毫秒）
    void SetTimeout(int resolveTimeout, int connectTimeout, int sendTimeout, int receiveTimeout);
    
private:
    HINTERNET hSession_;
    bool initialized_;
    
    static const wchar_t* kUserAgent;
};
