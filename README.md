# AI Subscription Monitor

Windows 桌面应用，用于监控 AI 订阅服务的使用情况。

## 功能特性

- 从 HTTP API 获取订阅数据
- 使用 nlohmann/json 解析 JSON
- GDI 渲染进度条显示使用量
- 支持滚动查看多个服务
- 自动定时刷新（60秒）
- 支持鼠标滚轮滚动

## 项目结构

```
src/
├── main.cpp          # Win32 窗口框架和消息循环
├── subscription.h/.cpp   # 数据结构定义和 JSON 解析
├── http_client.h/.cpp    # WinHTTP 封装
├── renderer.h/.cpp       # GDI 进度条渲染
└── json.hpp          # nlohmann/json 单头文件（需下载）
```

## 依赖

- Windows SDK
- nlohmann/json (header-only)

## 编译步骤

### 1. 下载 nlohmann/json

```powershell
# 在项目根目录执行
Invoke-WebRequest -Uri "https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp" -OutFile "src/json.hpp"
```

或手动下载：https://github.com/nlohmann/json/releases/tag/v3.11.3

### 2. 使用 CMake 编译

```powershell
# 创建构建目录
mkdir build
cd build

# 生成 Visual Studio 项目
cmake .. -G "Visual Studio 17 2022"

# 编译
cmake --build . --config Release
```

### 3. 使用 Visual Studio 直接编译

1. 打开 Visual Studio
2. 创建新的空项目
3. 添加所有 src/*.cpp 和 src/*.h 文件到项目
4. 下载 json.hpp 到 src 目录
5. 配置项目属性：
   - C/C++ -> 附加包含目录: `$(ProjectDir)src`
   - 链接器 -> 输入 -> 附加依赖项: `winhttp.lib gdi32.lib`
6. 编译运行

## 使用方法

```powershell
AISubscriptionMonitor.exe <host> [port] [path]

# 示例
AISubscriptionMonitor.exe api.example.com 8080 /subscriptions
AISubscriptionMonitor.exe localhost 3000 /api/usage
```

## JSON API 格式

API 应返回以下格式的 JSON 数组：

```json
[
  {
    "provider_id": "zenmux",
    "display_name": "ZenMux",
    "name": "ZenMux",
    "timestamp": "2026-02-17T13:44:27.892203022+08:00",
    "plan": {
      "name": "Ultra Plan",
      "type": "subscription"
    },
    "metrics": [
      {
        "name": "7d Flows",
        "window": {
          "id": "7d",
          "label": "7 Day",
          "resets_at": "2026-02-20T07:36:05Z"
        },
        "amount": {
          "used": 772.66,
          "limit": 1200,
          "remaining": 427.34,
          "unit": "flows"
        }
      }
    ],
    "status": "ok"
  }
]
```

## 快捷键

- `F5` - 手动刷新数据
- 鼠标滚轮 - 滚动查看

## 进度条颜色

- 绿色 (< 50%) - 使用量正常
- 黄色 (50-80%) - 使用量较高
- 红色 (> 80%) - 使用量接近上限
