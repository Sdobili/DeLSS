cmake_minimum_required(VERSION 3.16)
project(delss VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(DELSS_BUILD_TOOLS "Build the headless CLI and tests (needs EGL)" ON)

find_package(OpenGL REQUIRED COMPONENTS EGL)
find_library(GLESv2_LIBRARY NAMES GLESv2 REQUIRED)

# The library itself only needs a current GLES 3.0 context, however you made it.
add_library(delss src/delss.cpp)
add_library(delss::delss ALIAS delss)
target_include_directories(delss PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(delss PUBLIC ${GLESv2_LIBRARY})
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(delss PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()

if(DELSS_BUILD_TOOLS)
  add_library(delss_headless STATIC apps/headless.cpp)
  target_include_directories(delss_headless PUBLIC apps)
  target_include_directories(delss_headless SYSTEM PRIVATE third_party)
  target_link_libraries(delss_headless PUBLIC delss OpenGL::EGL)

  add_executable(delss-cli apps/delss_cli.cpp)
  target_link_libraries(delss-cli PRIVATE delss_headless)

  enable_testing()
  add_executable(delss_golden_test tests/golden_test.cpp)
  target_link_libraries(delss_golden_test PRIVATE delss_headless)
  target_compile_definitions(delss_golden_test PRIVATE DELSS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/..")
  add_test(NAME golden COMMAND delss_golden_test)
  add_test(NAME cli_list COMMAND delss-cli --list)
endif()
