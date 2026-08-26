cmake_minimum_required(VERSION 3.24)
project(wpj2_rt64_bridge LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

get_filename_component(WPJ2_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(RT64_ROOT "${WPJ2_ROOT}/tools/wpj2-recomp/lib/RT64")
set(WPJ2_RT64_OUTPUT "${WPJ2_ROOT}/build/rt64_runtime")

if(NOT EXISTS "${RT64_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR "RT64 nao encontrado em ${RT64_ROOT}")
endif()

set(RT64_STATIC ON CACHE BOOL "" FORCE)
set(RT64_SDL_WINDOW_VULKAN OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

if(MSVC)
    add_compile_options(/MP /bigobj /wd4100 /wd4244 /wd4267)
endif()

add_subdirectory("${RT64_ROOT}" "${CMAKE_BINARY_DIR}/rt64")

# O SDK do Windows instalado antecede GPU Upload Heaps. O perfil integrado usa
# Vulkan; esta equivalencia apenas permite compilar o caminho D3D12 opcional do
# Plume sem exigir uma atualizacao global do SDK.
if(MSVC AND TARGET plume)
    target_compile_definitions(plume PRIVATE
        D3D12_HEAP_TYPE_GPU_UPLOAD=D3D12_HEAP_TYPE_DEFAULT
    )
endif()

add_library(wpj2_rt64_bridge SHARED wpj2_rt64_bridge.cpp)
target_link_libraries(wpj2_rt64_bridge PRIVATE rt64)
target_link_directories(wpj2_rt64_bridge PRIVATE
    "${RT64_ROOT}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/lib/x64"
)
target_include_directories(wpj2_rt64_bridge PRIVATE
    "${WPJ2_ROOT}/runtime"
    "${RT64_ROOT}/src"
    "${RT64_ROOT}/src/hle"
    "${RT64_ROOT}/src/contrib/plume"
    "${RT64_ROOT}/src/contrib/hlslpp/include"
    "${RT64_ROOT}/src/contrib/imgui"
    "${RT64_ROOT}/src/contrib"
    "${RT64_ROOT}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/include"
)
target_compile_definitions(wpj2_rt64_bridge PRIVATE
    HLSL_CPU NOMINMAX __PRFCHWINTRIN_H IMGUI_IMPL_VULKAN_NO_PROTOTYPES
)

set_target_properties(wpj2_rt64_bridge PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${WPJ2_RT64_OUTPUT}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${WPJ2_RT64_OUTPUT}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WPJ2_RT64_OUTPUT}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${WPJ2_RT64_OUTPUT}"
)

add_custom_command(TARGET wpj2_rt64_bridge POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${RT64_ROOT}/src/contrib/dxc/bin/x64/dxcompiler.dll"
        "${WPJ2_RT64_OUTPUT}/dxcompiler.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${RT64_ROOT}/src/contrib/dxc/bin/x64/dxil.dll"
        "${WPJ2_RT64_OUTPUT}/dxil.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${RT64_ROOT}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/lib/x64/SDL2.dll"
        "${WPJ2_RT64_OUTPUT}/SDL2.dll"
)
