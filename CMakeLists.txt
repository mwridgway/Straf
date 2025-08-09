cmake_minimum_required(VERSION 3.24)
project(Straf LANGUAGES CXX)

# Options
option(STRAF_USE_VCPKG "Use vcpkg for dependencies" ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC settings
if(MSVC)
  add_compile_options(/W4 /permissive-)
  add_definitions(-DUNICODE -D_UNICODE)
endif()

# Optional dependencies (can be enabled via vcpkg or local includes)
# Prefer vcpkg config package when available; otherwise fall back to header include paths.
if(STRAF_USE_VCPKG)
  find_package(nlohmann_json CONFIG QUIET)
endif()
find_path(NLOHMANN_JSON_INCLUDE_DIRS nlohmann/json.hpp)
find_path(SPDLOG_INCLUDE_DIRS spdlog/spdlog.h PATHS ${CMAKE_SOURCE_DIR}/third_party NO_DEFAULT_PATH)

# Sources
add_executable(StrafAgent WIN32
  src/main.cpp
  src/Config.cpp
  src/Logging.cpp
  src/DetectorStub.cpp
  src/OverlayStub.cpp
  src/AudioStub.cpp
  src/PenaltyManager.cpp
)

target_include_directories(StrafAgent PRIVATE include)
if(nlohmann_json_FOUND)
  target_link_libraries(StrafAgent PRIVATE nlohmann_json::nlohmann_json)
elseif(NLOHMANN_JSON_INCLUDE_DIRS)
  target_include_directories(StrafAgent PRIVATE ${NLOHMANN_JSON_INCLUDE_DIRS})
else()
  message(WARNING "nlohmann_json not found. Place nlohmann/json.hpp in a discoverable include path (e.g., third_party) or enable vcpkg.")
endif()
if(SPDLOG_INCLUDE_DIRS)
  target_include_directories(StrafAgent PRIVATE ${SPDLOG_INCLUDE_DIRS})
endif()

# Windows libs
if(WIN32)
  target_link_libraries(StrafAgent PRIVATE
    winmm
    dxgi
    d3d11
    dcomp
  ole32
  shell32
  )
endif()

# Install config template
install(FILES ${CMAKE_SOURCE_DIR}/config.sample.json DESTINATION .)

# Set startup project in Visual Studio
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT StrafAgent)
