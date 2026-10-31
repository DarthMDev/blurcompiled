cmake_minimum_required(VERSION 3.24)
project(BlurCompiled VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
enable_testing()

# Portable Xbox packet/state contracts need neither a game nor a GPU backend.
add_library(blur-guest-packet STATIC src/render/guest_packet.cpp
  src/render/guest_contract.cpp src/render/guest_frontend.cpp)
target_include_directories(blur-guest-packet PUBLIC src)
add_executable(blur-guest-packet-tests tests/render/packet_tests.cpp)
target_link_libraries(blur-guest-packet-tests PRIVATE blur-guest-packet)
add_executable(blur-guest-frontend-tests tests/render/frontend_tests.cpp)
target_link_libraries(blur-guest-frontend-tests PRIVATE blur-guest-packet)
add_test(NAME guest-packet-contract COMMAND blur-guest-packet-tests)
add_test(NAME guest-frontend-contract COMMAND blur-guest-frontend-tests)

option(BLUR_PLUME_RENDERER "Build the Plume/Metal renderer and validation hosts" ${APPLE})
if(BLUR_PLUME_RENDERER)
  find_package(SDL3 3.2 CONFIG REQUIRED)
  include(cmake/Plume.cmake)
endif()
