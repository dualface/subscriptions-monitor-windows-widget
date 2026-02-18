#include <sstream>
#include <vector>

#include "http_client.h"

const wchar_t* HttpClient::kUserAgent = L"AI-Subscriptions-Monitor/1.0";

HttpClient::HttpClient() : hSession_(nullptr), initialized_(false) {}

HttpClient::~HttpClient()
{
    if (hSession_) {
        WinHttpCloseHandle(hSession_);
    }
}

bool HttpClient::Initialize()
{
    if (initialized_)
        return true;

    hSession_ =
        WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);

    initialized_ = (hSession_ != nullptr);
    return initialized_;
}

void HttpClient::SetTimeout(int resolveTimeout, int connectTimeout, int sendTimeout, int receiveTimeout)
{
    if (!hSession_)
        return;

    WinHttpSetTimeouts(hSession_, resolveTimeout, connectTimeout, sendTimeout, receiveTimeout);
}

std::string HttpClient::GetSync(const std::wstring& host, const std::wstring& path, int port, bool isHttps,
                                bool& success)
{
    success = false;
    std::string result;

    if (!initialized_ && !Initialize()) {
        return result;
    }

    // RAII wrapper for HINTERNET handles to ensure cleanup
    struct HandleGuard
    {
        HINTERNET handle;
        HandleGuard(HINTERNET h = nullptr) : handle(h) {}
        ~HandleGuard()
        {
            if (handle)
                WinHttpCloseHandle(handle);
        }
        HandleGuard(const HandleGuard&) = delete;
        HandleGuard& operator=(const HandleGuard&) = delete;
        HandleGuard(HandleGuard&& other) noexcept : handle(other.handle)
        {
            other.handle = nullptr;
        }
        HandleGuard& operator=(HandleGuard&& other) noexcept
        {
            if (this != &other) {
                if (handle)
                    WinHttpCloseHandle(handle);
                handle = other.handle;
                other.handle = nullptr;
            }
            return *this;
        }
        HINTERNET get() const
        {
            return handle;
        }
        explicit operator bool() const
        {
            return handle != nullptr;
        }
    };

    HandleGuard hConnect(WinHttpConnect(hSession_, host.c_str(), port, 0));
    if (!hConnect) {
        return result;
    }

    DWORD requestFlags = isHttps ? WINHTTP_FLAG_SECURE : 0;
    HandleGuard hRequest(WinHttpOpenRequest(hConnect.get(), L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, requestFlags));

    if (!hRequest) {
        return result;
    }

    // Send request
    BOOL bResults =
        WinHttpSendRequest(hRequest.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);

    if (bResults) {
        bResults = WinHttpReceiveResponse(hRequest.get(), nullptr);
    }

    if (!bResults) {
        return result;
    }

    // Check HTTP status code
    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    if (!WinHttpQueryHeaders(hRequest.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusCodeSize, WINHTTP_NO_HEADER_INDEX)) {
        return result;
    }

    if (statusCode < 200 || statusCode >= 300) {
        // Non-2xx status
        result = "HTTP " + std::to_string(statusCode);
        return result;
    }

    // Read response body
    DWORD dwSize = 0;
    DWORD dwDownloaded = 0;

    do {
        dwSize = 0;
        if (!WinHttpQueryDataAvailable(hRequest.get(), &dwSize)) {
            break;
        }

        if (dwSize == 0)
            break;

        // Guard against excessively large responses to prevent OOM
        if (result.size() + dwSize > kMaxResponseSize) {
            result = "Response too large (exceeded " + std::to_string(kMaxResponseSize / (1024 * 1024)) + " MB limit)";
            return result;  // success remains false
        }

        std::vector<char> buffer(dwSize + 1);
        ZeroMemory(buffer.data(), dwSize + 1);

        if (!WinHttpReadData(hRequest.get(), buffer.data(), dwSize, &dwDownloaded)) {
            break;
        }

        result.append(buffer.data(), dwDownloaded);
    } while (dwSize > 0);

    success = true;
    return result;
}
