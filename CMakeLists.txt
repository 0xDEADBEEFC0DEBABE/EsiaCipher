# Esia Cipher UI Lib - a flat, animated UI library drawn with Esia's render class (esia::Painter) on Esia's core
# (esia::Context), and its demo - the hub, floating windows, notifications, the watermark, the intro and the pet - in the
# frame of Esia's glass_window example (glass_app).
#
#   build.bat              configure (once) and build, Ninja, Release, into build/ (clang-cl when LLVM is there, else cl)
#   build\bin\cipher.exe   the demo (glass_window's options: --api, --size, --scale, --frames, --screenshot ...)
#
# Esia: its source tree beside this one (../Esia, or CIPHER_ESIA_DIR) when there is one, else fetched from GitHub at
# CIPHER_ESIA_TAG, the commit Cipher is known to build with.
cmake_minimum_required(VERSION 3.28)
project(Cipher LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Esia: its libraries, backends and glass_window's frame for the demo
set(CIPHER_ESIA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Esia" CACHE PATH "Esia's source tree (missing: fetched from GitHub)")
set(CIPHER_ESIA_TAG 6e5edd387cc1ac428e6e1428a9e7ad73610e7d90 CACHE STRING "The Esia commit fetched when there is no tree")
set(ESIA_BUILD_EXAMPLES ON CACHE BOOL "glass_window's frame (glass_app) for the demo" FORCE)
set(ESIA_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
if(CIPHER_ESIA_DIR AND EXISTS "${CIPHER_ESIA_DIR}/CMakeLists.txt")
    add_subdirectory(${CIPHER_ESIA_DIR} esia EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(esia
        URL https://github.com/0xDEADBEEFC0DEBABE/Esia/archive/${CIPHER_ESIA_TAG}.tar.gz
        EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(esia)
endif()

add_library(cipher STATIC
    cipher/cipher.hpp
    cipher/ui.cpp
    cipher/hub.cpp
    cipher/widgets.cpp
    cipher/popups.cpp
    cipher/inputs.cpp
    cipher/sections.cpp
    cipher/data.cpp
    cipher/model.hpp
    cipher/model.cpp
    cipher/pet.cpp
    cipher/boot.hpp
    cipher/boot.cpp
    cipher/anim.hpp
    cipher/anim.cpp
    cipher/icons.hpp
    cipher/icons.cpp
    cipher/lucide_icons.hpp
    cipher/lucide_data.cpp
    cipher/theme.hpp
    cipher/theme.cpp
)
target_include_directories(cipher PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/cipher)
target_link_libraries(cipher PUBLIC esia::render esia::text)
# models' PNG textures: Esia's decoder, in its FreeType text system
if(TARGET esia_text_ft)
    target_link_libraries(cipher PUBLIC esia_text_ft)
    target_compile_definitions(cipher PRIVATE CIPHER_PNG=1)
endif()
esia_target_defaults(cipher)

glass_add_app(cipher_demo demo/main.cpp)
set_target_properties(cipher_demo PROPERTIES OUTPUT_NAME cipher)
target_link_libraries(cipher_demo PRIVATE glass_app cipher)
esia_target_defaults(cipher_demo)
