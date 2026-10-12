# libtogl_client.so for Garry's Mod's x86-64 Linux branch: ToGL's interface on
# DXVK's native d3d9 (see togl.cpp). Built by scripts/build-dxvk-linux.sh with
# the DXVK checkout it builds (for DXVK's Direct3D 9 headers).
cmake_minimum_required(VERSION 3.20)
project(mmdhl_togl CXX)
set(MMDHL_DXVK_SOURCE "" CACHE PATH "DXVK source tree (include/native)")
if(NOT EXISTS "${MMDHL_DXVK_SOURCE}/include/native/directx/d3d9.h")
  message(FATAL_ERROR "MMDHL_DXVK_SOURCE must point to a DXVK checkout")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_library(togl_client SHARED togl.cpp gl_tables.cpp d3dx.cpp backend.cpp)
# Only backend.cpp sees DXVK's headers: they declare the same names as ToGL's.
set_source_files_properties(backend.cpp PROPERTIES INCLUDE_DIRECTORIES
  "${MMDHL_DXVK_SOURCE}/include/native/windows;${MMDHL_DXVK_SOURCE}/include/native/directx;${MMDHL_DXVK_SOURCE}/include/vulkan/include")
target_compile_options(togl_client PRIVATE -Wall -Wno-unused-parameter -Wno-invalid-offsetof -fno-strict-aliasing)
set_target_properties(togl_client PROPERTIES LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/exports.map")
# The game's runtime has an older libstdc++; nothing but the game's symbols leaves the library.
target_link_options(togl_client PRIVATE -static-libstdc++ -static-libgcc -Wl,--exclude-libs,ALL -Wl,-z,defs
  "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports.map")
target_link_libraries(togl_client PRIVATE ${CMAKE_DL_LIBS} pthread)
# Debug information goes beside the library, as for the native files.
add_custom_command(TARGET togl_client POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} --only-keep-debug "$<TARGET_FILE:togl_client>" "$<TARGET_FILE:togl_client>.debug"
  COMMAND ${CMAKE_OBJCOPY} --strip-debug "--add-gnu-debuglink=$<TARGET_FILE:togl_client>.debug" "$<TARGET_FILE:togl_client>"
  VERBATIM)

enable_testing()
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME togl_exports COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/check_exports.py" $<TARGET_FILE:togl_client> "${CMAKE_CURRENT_SOURCE_DIR}/exports.map")
endif()
