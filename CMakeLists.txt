cmake_minimum_required(VERSION 3.24)
project(Straf LANGUAGES CXX)

# Options
option(STRAF_USE_VCPKG "Use vcpkg for dependencies" ON)
option(STRAF_ENABLE_CLANG_TIDY "Run clang-tidy during builds (if available)" OFF)
option(STRAF_ENABLE_VOSK "Enable Vosk STT backend" ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler settings
if(MSVC)
  add_compile_options(/W4 /permissive- /MP /EHsc)
  # Ensure PDB debug symbols are generated for Debug config
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT ProgramDatabase)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
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
  src/OverlayClassic.cpp
  src/OverlayBar.cpp
  src/AudioSilent.cpp
  src/STTStub.cpp
  src/STTSapi.cpp
  src/AudioWasapi.cpp
  src/PenaltyManager.cpp
  src/TrayWin.cpp
  src/STTVosk.cpp
  src/DetectorText.cpp
  resources/StrafAgent.rc
)

# Debugger defaults for Visual Studio: use sample config and stub backends
set_target_properties(StrafAgent PROPERTIES
  VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  VS_DEBUGGER_ENVIRONMENT "STRAF_USE_SAMPLE_CONFIG=1\nSTRAF_STT=vosk\nSTRAF_AUDIO_SOURCE=wasapi"
)

# Optional Vosk backend with safe fallback
set(HAS_VOSK FALSE)
if(STRAF_ENABLE_VOSK)
  # Try env-provided lib first
  if(DEFINED ENV{VOSK_LIBRARY})
    set(HAS_VOSK TRUE)
    target_link_libraries(StrafAgent PRIVATE $ENV{VOSK_LIBRARY})
  else()
    find_library(VOSK_LIB NAMES libvosk vosk)
    if(VOSK_LIB)
      set(HAS_VOSK TRUE)
      target_link_libraries(StrafAgent PRIVATE ${VOSK_LIB})
    endif()
  endif()
endif()

# Vosk backend
if(DEFINED ENV{VOSK_INCLUDE_DIR})
  target_include_directories(StrafAgent PRIVATE $ENV{VOSK_INCLUDE_DIR})
endif()
if(DEFINED ENV{VOSK_LIBRARY})
  target_link_libraries(StrafAgent PRIVATE $ENV{VOSK_LIBRARY})
else()
  find_library(VOSK_LIB NAMES libvosk)
  if(VOSK_LIB)
    target_link_libraries(StrafAgent PRIVATE ${VOSK_LIB})
  else()
    target_link_libraries(StrafAgent PRIVATE vosk)
  endif()
endif()

# Target-level defines
target_compile_definitions(StrafAgent PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

# Optional static analysis with clang-tidy (when enabled and found)
if(STRAF_ENABLE_CLANG_TIDY)
  find_program(CLANG_TIDY_EXE NAMES clang-tidy)
  if(CLANG_TIDY_EXE)
    set_target_properties(StrafAgent PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
  else()
    message(WARNING "STRAF_ENABLE_CLANG_TIDY=ON but clang-tidy was not found.")
  endif()
endif()

# Includes and optional JSON lib
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
  mmdevapi
  uuid
  sapi
    dxgi
    d3d11
    dcomp
  d2d1
  dwrite
  ole32
  shell32
  )
endif()

# Install config template
install(FILES ${CMAKE_SOURCE_DIR}/config.sample.json DESTINATION .)

# Set startup project in Visual Studio
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT StrafAgent)
