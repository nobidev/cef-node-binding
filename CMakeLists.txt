cmake_minimum_required(VERSION 3.16)
project(cef_node_binding)

if(NOT DEFINED ENV{CEF_ROOT})
  message(FATAL_ERROR "Please set environment variable CEF_ROOT to your CEF binary distribution root")
endif()

set(CEF_ROOT $ENV{CEF_ROOT})
message(STATUS "CEF_ROOT: ${CEF_ROOT}")
include_directories(${CEF_ROOT})
link_directories(${CEF_ROOT}/Release)

execute_process(COMMAND node -p "process.config.variables.nodedir || '/usr/include/node'"
  OUTPUT_VARIABLE NODE_DIR
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
find_path(NODE_INCLUDE NAMES "node_api.h" PATHS ${NODE_DIR} NO_DEFAULT_PATH)

message(STATUS "NodeJS: ${NODE_DIR}")
include_directories(${NODE_DIR})

execute_process(COMMAND node -p "require('node-addon-api').include.replace(/(^\"|\"$)/g, '')"
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE NODE_ADDON_API_DIR
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
find_path(NODE_ADDON_API_INCLUDE NAMES "napi.h" PATHS ${NODE_ADDON_API_DIR} NO_DEFAULT_PATH)

message(STATUS "Node-addon-api: ${NODE_ADDON_API_DIR}")
include_directories(${NODE_ADDON_API_DIR})

set(SOURCES src/cef_node_binding.cpp src/main.cpp)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
add_executable(cef_node_app ${SOURCES})

add_library(cef_node_binding SHARED ${SOURCES})

if(WIN32)
  target_link_libraries(cef_node_binding PRIVATE ${CEF_ROOT}/Release/libcef.lib)
else()
  target_link_libraries(cef_node_binding PRIVATE ${CEF_ROOT}/Release/libcef.so)
endif()

if(UNIX AND NOT APPLE)
  target_link_libraries(cef_node_binding PRIVATE pthread dl)
endif()

set_target_properties(cef_node_binding PROPERTIES PREFIX "" SUFFIX ".node")
