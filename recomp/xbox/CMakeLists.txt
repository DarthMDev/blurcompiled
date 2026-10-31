cmake_minimum_required(VERSION 3.25)
project(blur_x360 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(generated/rexglue.cmake)
# The entrypoint uses PPCImageConfig.func_mappings. The emitted legacy
# registrar is unused here and extremely expensive to optimize as one function.
# Match the previously validated Xbox host; retain all translated guest code.
list(FILTER REXGLUE_ENTRYPOINT_GENERATED_SOURCES EXCLUDE REGEX "/blur_x360_register[.]cpp$")
if(WIN32)
  add_executable(blur_x360 WIN32 src/main.cpp)
else()
  add_executable(blur_x360 src/main.cpp)
endif()
rexglue_setup_target(blur_x360 GPU_PLUGINS xenos)
if(APPLE)
  # SDK 0.10.0 initializes SDL even after its generic --help parse path.
  # Keep a host-only help entrypoint; normal launches delegate to the SDK.
  get_target_property(_host_sources blur_x360 SOURCES)
  foreach(_source IN LISTS _host_sources)
    if(_source MATCHES "/windowed_app_main_sdl[.]cpp$")
      set_source_files_properties("${_source}" PROPERTIES
        COMPILE_DEFINITIONS "main=blurcompiled_sdk_main")
    endif()
  endforeach()
endif()
