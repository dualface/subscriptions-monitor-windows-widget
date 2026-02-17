#include "http_client.h"
#include <sstream>
#include <vector>

const wchar_t* HttpClient::kUserAgent = L"AI-Subscriptions-Monitor/1.0";

HttpClient::HttpClient() : hSession_(nullptr), initialized_(false) {}

HttpClient::~HttpClient() {
    if (hSession_) {
        WinHttpCloseHandle(hSession_);
    }
}

bool HttpClient::Initialize() {
    if (initialized_) return true;
    
    hSession_ = WinHttpOpen(kUserAgent,
                           WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                           WINHTTP_NO_PROXY_NAME,
                           WINHTTP_NO_PROXY_BYPASS,
                           0);
    
    initialized_ = (hSession_ != nullptr);
    return initialized_;
}

void HttpClient::SetTimeout(int resolveTimeout, int connectTimeout, int sendTimeout, int receiveTimeout) {
    if (!hSession_) return;
    
    WinHttpSetTimeouts(hSession_, resolveTimeout, connectTimeout, sendTimeout, receiveTimeout);
}

std::string HttpClient::GetSync(const std::wstring& host,
                                const std::wstring& path,
                                int port,
                                bool& success) {
    success = false;
    std::string result;
    
    if (!initialized_ && !Initialize()) {
        return result;
    }
    
    HINTERNET hConnect = WinHttpConnect(hSession_, host.c_str(), port, 0);
    if (!hConnect) {
        return result;
    }
    
    HINTERNET hRequest = WinHttpOpenRequest(hConnect,
                                           L"GET",
                                           path.c_str(),
                                           nullptr,
                                           WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           0);
    
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        return result;
    }
    
    // 发送请求
    BOOL bResults = WinHttpSendRequest(hRequest,
                                      WINHTTP_NO_ADDITIONAL_HEADERS,
                                      0,
                                      WINHTTP_NO_REQUEST_DATA,
                                      0,
                                      0,
                                      0);
    
    if (bResults) {
        bResults = WinHttpReceiveResponse(hRequest, nullptr);
    }
    
    if (bResults) {
        DWORD dwSize = 0;
        DWORD dwDownloaded = 0;
        
        do {
            dwSize = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) {
                break;
            }
            
            if (dwSize == 0) break;
            
            std::vector<char> buffer(dwSize + 1);
            ZeroMemory(buffer.data(), dwSize + 1);
            
            if (!WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) {
                break;
            }
            
            result.append(buffer.data(), dwDownloaded);
        } while (dwSize > 0);
        
        success = true;
    }
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    
    return result;
}

bool HttpClient::Get(const std::wstring& host,
                     const std::wstring& path,
                     int port,
                     const HttpResponseCallback& callback) {
    bool success = false;
    std::string response = GetSync(host, path, port, success);
    
    if (callback) {
        callback(response, success);
    }
    
    return success;
}
