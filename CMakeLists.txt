cmake_minimum_required(VERSION 3.24)
project(yipie LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(/utf-8 /W3)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# 公共源码（不含 main，供测试复用纯逻辑层）
set(LOGIC_SOURCES src/config.cpp src/gesture_engine.cpp src/hotkey_parser.cpp src/ipc_router.cpp src/icon_lookup.cpp src/isolation.cpp)
include_directories(src third_party/doctest third_party/rapidjson/include third_party/webview2/include)

# 规格 §5 硬指标量测开关：ON 时 main.cpp 用 QPC 采样 pop/replay 延迟并追加
# %TEMP%\yipie_latency.log。默认 OFF —— 探针代码不编译，零开销。
option(YIPIE_LATENCY_PROBE "measure wheel pop and click replay latency" OFF)

add_executable(yipie WIN32
  src/main.cpp src/action_executor.cpp src/mouse_hook.cpp
  src/wheel_window.cpp src/icon_cache.cpp src/icon_flows.cpp src/icon_extract.cpp src/tray.cpp src/settings_window.cpp src/yipie.rc src/app.manifest
  ${LOGIC_SOURCES})
target_link_libraries(yipie PRIVATE d2d1 dwrite ole32 shell32 user32 gdi32 advapi32 shcore comdlg32 windowscodecs crypt32
  "${CMAKE_SOURCE_DIR}/third_party/webview2/x64/WebView2LoaderStatic.lib")
if(YIPIE_LATENCY_PROBE)
  target_compile_definitions(yipie PRIVATE YIPIE_LATENCY_PROBE)
endif()

# M2: ship ui/ next to the exe (ui/ may not exist before Task 3 -> guarded helper)
add_custom_command(TARGET yipie POST_BUILD
  COMMAND ${CMAKE_COMMAND}
    -DCOPY_FROM=${CMAKE_SOURCE_DIR}/ui
    -DCOPY_TO=$<TARGET_FILE_DIR:yipie>/ui
    -P ${CMAKE_SOURCE_DIR}/cmake/copy-if-exists.cmake)

# M3c: glyph table single source -> generated C++ header (ui/glyphs.json is truth)
add_custom_command(
  OUTPUT ${CMAKE_SOURCE_DIR}/src/glyph_table.generated.h
  COMMAND powershell -NoProfile -ExecutionPolicy Bypass
          -File ${CMAKE_SOURCE_DIR}/scripts/gen-glyph-header.ps1
  DEPENDS ${CMAKE_SOURCE_DIR}/ui/glyphs.json
          ${CMAKE_SOURCE_DIR}/scripts/gen-glyph-header.ps1
  COMMENT "generating glyph_table.generated.h from ui/glyphs.json")
add_custom_target(glyph_header DEPENDS ${CMAKE_SOURCE_DIR}/src/glyph_table.generated.h)
add_dependencies(yipie glyph_header)

enable_testing()
add_executable(yipie_tests
  tests/test_main.cpp tests/test_config.cpp
  tests/test_gesture_engine.cpp tests/test_hotkey_parser.cpp
  tests/test_ipc_router.cpp tests/test_icon_lookup.cpp tests/test_isolation.cpp ${LOGIC_SOURCES})
add_dependencies(yipie_tests glyph_header)
add_test(NAME unit COMMAND yipie_tests)

# M2: ui geometry tests run under node when available (dir appears in Task 4)
# 注：node 24 在 Windows 下不接受目录参数（会把目录本身当测试文件失败），传显式文件。
find_program(NODE_EXE node)
if(NODE_EXE AND EXISTS ${CMAKE_SOURCE_DIR}/tests/ui/geometry.test.mjs)
  add_test(NAME ui_geometry COMMAND ${NODE_EXE} --test ${CMAKE_SOURCE_DIR}/tests/ui/geometry.test.mjs ${CMAKE_SOURCE_DIR}/tests/ui/patch.test.mjs ${CMAKE_SOURCE_DIR}/tests/ui/glyphs.test.mjs)
endif()
