cmake_minimum_required(VERSION 3.16)
# delss-overlay uses ScreenCaptureKit (macOS 12.3+).
set(CMAKE_OSX_DEPLOYMENT_TARGET 12.3 CACHE STRING "Minimum macOS version")
project(delss VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# GL flavour hosting the single-source GLSL ES 3.00 shaders (see include/delss/gl.hpp).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(_delss_default_gl GLES)
else()
  set(_delss_default_gl DESKTOP)
endif()
set(DELSS_GL ${_delss_default_gl} CACHE STRING "GLES (OpenGL ES 3.0) or DESKTOP (GL 3.3/4.1 core)")
set_property(CACHE DELSS_GL PROPERTY STRINGS GLES DESKTOP)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(_delss_tools_default ON)
else()
  set(_delss_tools_default OFF)
endif()
option(DELSS_BUILD_TOOLS "Build the headless CLI and tests (needs EGL)" ${_delss_tools_default})
option(DELSS_BUILD_OVERLAY "Build delss-overlay, the screen-wide filter app" OFF)

if(MSVC)
  # generated.hpp carries UTF-8 preset labels; windows.h must not define min/max.
  add_compile_options(/utf-8)
  add_compile_definitions(NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()

set(DELSS_WARNINGS "")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set(DELSS_WARNINGS -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
elseif(MSVC)
  set(DELSS_WARNINGS /W4 /WX /permissive- /wd4100)
endif()

# delss_add_library(<name> <GLES|DESKTOP>): the library for one GL flavour.
function(delss_add_library name flavour)
  add_library(${name} src/delss.cpp src/gl.cpp)
  target_include_directories(${name} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_compile_options(${name} PRIVATE ${DELSS_WARNINGS})
  if(flavour STREQUAL "GLES")
    find_library(GLESv2_LIBRARY NAMES GLESv2 REQUIRED)
    find_path(GLES3_INCLUDE_DIR GLES3/gl3.h REQUIRED)
    target_include_directories(${name} PUBLIC ${GLES3_INCLUDE_DIR})
    target_link_libraries(${name} PUBLIC ${GLESv2_LIBRARY})
  else()
    target_compile_definitions(${name} PUBLIC DELSS_GL_DESKTOP)
    if(APPLE)
      find_library(OPENGL_FRAMEWORK OpenGL REQUIRED)
      target_link_libraries(${name} PUBLIC ${OPENGL_FRAMEWORK})
    else()
      # Entry points are loaded at runtime; only the Khronos headers are needed.
      target_include_directories(${name} SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
    endif()
  endif()
endfunction()

delss_add_library(delss ${DELSS_GL})
add_library(delss::delss ALIAS delss)

if(DELSS_BUILD_TOOLS)
  find_library(EGL_LIBRARY NAMES EGL REQUIRED)
  find_path(EGL_INCLUDE_DIR EGL/egl.h REQUIRED)
  enable_testing()

  # delss_add_tools(<suffix> <lib>): headless helpers + golden test for one flavour.
  function(delss_add_tools suffix lib)
    add_library(delss_headless${suffix} STATIC apps/headless.cpp)
    target_include_directories(delss_headless${suffix} PUBLIC apps ${EGL_INCLUDE_DIR})
    target_include_directories(delss_headless${suffix} SYSTEM PUBLIC third_party)
    target_link_libraries(delss_headless${suffix} PUBLIC ${lib} ${EGL_LIBRARY})

    add_executable(delss_golden_test${suffix} tests/golden_test.cpp)
    target_link_libraries(delss_golden_test${suffix} PRIVATE delss_headless${suffix})
    target_compile_definitions(delss_golden_test${suffix} PRIVATE DELSS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/.."
                                                                    DELSS_OUT_DIR="golden-cpp${suffix}")
    add_test(NAME golden${suffix} COMMAND delss_golden_test${suffix})
  endfunction()

  delss_add_tools("" delss)
  add_executable(delss-cli apps/delss_cli.cpp)
  target_link_libraries(delss-cli PRIVATE delss_headless)
  add_test(NAME cli_list COMMAND delss-cli --list)
  add_executable(delss-imgdiff apps/imgdiff.cpp)
  target_link_libraries(delss-imgdiff PRIVATE delss_headless)

  # The desktop-GL path that Windows and macOS use, exercised on Mesa's desktop
  # GL 3.3 core profile: proves the shader translation matches the goldens.
  if(DELSS_GL STREQUAL "GLES")
    delss_add_library(delss_desktop DESKTOP)
    delss_add_tools("_desktop" delss_desktop)
  endif()
endif()

if(DELSS_BUILD_OVERLAY)
  add_subdirectory(overlay)
endif()
