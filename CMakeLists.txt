cmake_minimum_required(VERSION 3.16)
project(bbmod C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(BBMOD_WITH_ZYDIS "Build inline hooks with Zydis" ON)
option(BBMOD_BUILD_TESTS "Build the test harness and tests" ON)

add_library(bbmod_host STATIC
    src/host/src/modhost.c
    src/host/src/host.c
    src/host/src/loader.c
    src/host/src/code.c
    src/host/src/xmlpatch.c
    src/host/src/manifest.c
    src/host/src/vfs.c
    src/host/src/net.c
    src/host/src/gameplay.c
    src/host/src/equipment.c
    src/host/src/characters.c
    src/host/src/characters_service.c
    src/host/src/inventory.c
    src/host/src/inventory_service.c
    src/host/src/equipment_service.c
    src/host/src/item_actions.c
    src/host/src/native_attack.c
    src/host/src/native_observation.c
    src/host/src/ui_tables.c
    src/host/src/ui_slots.c
    src/host/src/native_menu.c
    src/host/src/native_menu_bridge.c
    src/host/src/text_entry.c
    src/host/src/pad.c
    src/host/src/platform_posix.c
    src/host/src/platform_win.c
)
target_include_directories(bbmod_host PUBLIC src/host/include sdk/include)
if(NOT WIN32)
    target_link_libraries(bbmod_host PUBLIC dl pthread)
endif()

if(BBMOD_WITH_ZYDIS)
    find_package(Zydis QUIET)
    if(Zydis_FOUND)
        target_compile_definitions(bbmod_host PRIVATE BBM_WITH_ZYDIS=1)
        target_link_libraries(bbmod_host PUBLIC Zydis::Zydis)
    else()
        message(FATAL_ERROR "Zydis requested but not found. Install Zydis or explicitly set BBMOD_WITH_ZYDIS=OFF for a reduced build.")
    endif()
endif()

if(BBMOD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(src/tests)
endif()
