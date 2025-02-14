cmake_minimum_required(VERSION 3.15)
project(websocket_client VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# # 设置构建类型为 Debug，如果没有指定构建类型
# if(NOT CMAKE_BUILD_TYPE)
#     set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
#     set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
# endif()
#
# # 为 Debug 构建类型添加调试信息标志
# set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g")
# set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g")

# Find dependencies (Conan packages)
find_package(Libwebsockets REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(spdlog REQUIRED)

add_definitions(
    -UNDEBUG
    -DDTF_HEADER_ONLY
)

include_directories(
    include
)

set(WEBSOCKET_CLIENT_HEADERS
    websocket_client_base.h
    ws_client.h
)

set(WEBSOCKET_CLIENT_SOURCES
    src/ws_client.cpp
)

add_executable(
    ${PROJECT_NAME}
    main.cpp
    ${WEBSOCKET_CLIENT_SOURCES}
)

# Create library target
# Link dependencies to the library
target_link_libraries(
    ${PROJECT_NAME}
    PUBLIC
        websockets
        nlohmann_json::nlohmann_json
        spdlog::spdlog
         
)

