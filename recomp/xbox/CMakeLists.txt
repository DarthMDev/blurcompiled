cmake_minimum_required(VERSION 3.25)
project(blur_x360 LANGUAGES C CXX)
if(APPLE)
  enable_language(OBJC OBJCXX)
endif()
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(BLUR_METAL_PLUME "Use the pinned native Metal SDK and bounded Plume bridge" OFF)
option(BLUR_METAL_PREPARE_FIXTURES "Build the CPU fixture translator before Plume hooks" OFF)
set(BLURCOMPILED_SOURCE_DIR "" CACHE PATH "BlurCompiled source checkout")
if(BLUR_METAL_PLUME)
  set(REXGLUE_USE_METAL ON CACHE BOOL "" FORCE)
  set(REXGLUE_BUILD_METAL_SHADER_TOOLS ON CACHE BOOL "" FORCE)
endif()
include(generated/rexglue.cmake)
if(REXSDK_DIR)
  # Source SDK staging helpers execute in the host's directory scope.
  set(REXGLUE_ROOT "${REXSDK_DIR}")
endif()
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

if(BLUR_METAL_PLUME)
  include("${BLURCOMPILED_SOURCE_DIR}/recomp/metal/Integration.cmake")
endif()
