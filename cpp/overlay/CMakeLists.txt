# delss-overlay: shared core + per-OS capture/overlay/hotkey/tray backends.
include(FetchContent)

# Pure C++ core (state machine, settings, frame conversion, HUD): no GL, no
# OS APIs, unit-tested on every CI runner.
add_library(delss_overlay_core STATIC core/state.cpp core/settings.cpp core/frame.cpp core/hud.cpp)
target_include_directories(delss_overlay_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(delss_overlay_core PRIVATE ${DELSS_WARNINGS})

enable_testing()
add_executable(delss_overlay_core_test tests/core_test.cpp)
target_link_libraries(delss_overlay_core_test PRIVATE delss_overlay_core)
target_compile_options(delss_overlay_core_test PRIVATE ${DELSS_WARNINGS})
add_test(NAME overlay_core COMMAND delss_overlay_core_test)

# GLFW 3.4: transparent, undecorated, floating, mouse-passthrough windows on
# all three OSes. Wayland is off: a Wayland client cannot place a global
# overlay; Wayland sessions use the compositor shaders (tools/compositor/).
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw URL https://github.com/glfw/glfw/releases/download/3.4/glfw-3.4.zip
                     URL_HASH SHA256=b5ec004b2712fd08e8861dc271428f048775200a2df719ccf575143ba749a3e9
                     DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(glfw)

add_executable(delss-overlay main.cpp gl/renderer.cpp)
target_link_libraries(delss-overlay PRIVATE delss delss_overlay_core glfw)
target_include_directories(delss-overlay SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../third_party)
target_compile_options(delss-overlay PRIVATE ${DELSS_WARNINGS})

if(WIN32)
  target_sources(delss-overlay PRIVATE platform/win32/win32_platform.cpp platform/win32/win32_tray.cpp
                                       platform/win32/delss-overlay.rc)
  target_link_libraries(delss-overlay PRIVATE d3d11 dxgi user32 shell32)
  set_target_properties(delss-overlay PROPERTIES WIN32_EXECUTABLE OFF)
elseif(APPLE)
  enable_language(OBJCXX)
  target_sources(delss-overlay PRIVATE platform/macos/macos_platform.mm platform/macos/macos_tray.mm)
  set_source_files_properties(platform/macos/macos_platform.mm platform/macos/macos_tray.mm PROPERTIES COMPILE_FLAGS
                                                                                                     "-fobjc-arc")
  target_link_libraries(
    delss-overlay PRIVATE "-framework Cocoa" "-framework ScreenCaptureKit" "-framework CoreMedia"
                          "-framework CoreVideo" "-framework Carbon")
else()
  find_package(X11 REQUIRED)
  if(NOT X11_Xcomposite_FOUND OR NOT X11_Xshape_FOUND)
    message(FATAL_ERROR "delss-overlay needs libXcomposite and libXext (XShape) development files")
  endif()
  target_sources(delss-overlay PRIVATE platform/x11/x11_platform.cpp)
  target_link_libraries(delss-overlay PRIVATE X11::X11 X11::Xcomposite X11::Xext)
  find_package(PkgConfig)
  if(PkgConfig_FOUND)
    pkg_check_modules(APPINDICATOR IMPORTED_TARGET ayatana-appindicator3-0.1)
  endif()
  if(APPINDICATOR_FOUND)
    target_sources(delss-overlay PRIVATE platform/x11/appindicator_tray.cpp)
    target_link_libraries(delss-overlay PRIVATE PkgConfig::APPINDICATOR)
    message(STATUS "delss-overlay: tray icon via Ayatana AppIndicator")
  else()
    target_sources(delss-overlay PRIVATE platform/no_tray.cpp)
    message(STATUS "delss-overlay: no tray (install libayatana-appindicator3-dev for one)")
  endif()
endif()

install(TARGETS delss-overlay RUNTIME DESTINATION bin)

# X11 end-to-end test (Linux only): Xvfb + Mesa llvmpipe, see tests/x11/e2e.sh.
if(NOT WIN32 AND NOT APPLE)
  add_executable(x11-testpattern tests/x11/testpattern.cpp)
  target_include_directories(x11-testpattern SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../third_party)
  target_link_libraries(x11-testpattern PRIVATE X11::X11)
  find_program(XVFB_RUN NAMES Xvfb)
  find_program(XDOTOOL NAMES xdotool)
  if(XVFB_RUN AND XDOTOOL AND TARGET delss-imgdiff)
    add_test(NAME overlay_x11_e2e
             COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/x11/e2e.sh $<TARGET_FILE:delss-overlay>
                     $<TARGET_FILE:x11-testpattern> $<TARGET_FILE:delss-imgdiff> ${CMAKE_CURRENT_SOURCE_DIR}/../.. ${CMAKE_CURRENT_BINARY_DIR}/e2e)
  endif()
endif()
