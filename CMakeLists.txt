cmake_minimum_required(VERSION 3.16)
project(AISubscriptionMonitor VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows 特定设置
if(WIN32)
    # 使用 Unicode 字符集
    add_definitions(-DUNICODE -D_UNICODE)
    
    # 静态链接运行时库（可选）
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    
    # 使用 UTF-8 编码
    add_compile_options(/utf-8)
endif()

# 源文件
set(SOURCES
    src/main.cpp
    src/subscription.cpp
    src/http_client.cpp
    src/renderer.cpp
)

# 头文件
set(HEADERS
    src/subscription.h
    src/http_client.h
    src/renderer.h
    src/json.hpp
)

# 创建可执行文件（Windows GUI 程序）
if(WIN32)
    add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${HEADERS})
else()
    add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})
endif()

# Windows 特定库
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        winhttp
        gdi32
        user32
        kernel32
        comctl32
    )
endif()

# 设置输出目录
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# 安装目标
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)
