# MARE LIBERUM - CMake project for IDE users (Visual Studio "Open Folder", CLion, VS Code).
# The canonical builds remain game/build.sh and game/build.bat (single translation unit).
#
#   cmake -S game -B build && cmake --build build --config Release
#   ctest --test-dir build -C Release
cmake_minimum_required(VERSION 3.16)
project(mare_liberum C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)          # mare::core and everything the C++ rewrite ports (docs/REWRITE_PLAN.md)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(V3_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

# The ported C++ code behind the C ABIs in src/core/*.h and src/engine/entities.h.
set(MARE_CORE_CXX src/core/diag.cc src/core/worker_pool.cc)
set(MARE_ENGINE_CXX src/engine/entity_manager.cc src/engine/shader_library.cc)
set(MARE_GAME_CXX src/game/weather.cc src/game/game_entities.cc)

# The C11 unity build of the unported modules, plus the C++ units linked alongside.
add_executable(mare mare.c ${MARE_CORE_CXX} ${MARE_ENGINE_CXX} ${MARE_GAME_CXX})
target_include_directories(mare PRIVATE "${V3_ROOT}")
if(WIN32)
    target_compile_definitions(mare PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(mare PRIVATE /utf-8 /wd4244 /wd4305 /wd4996)   # float narrowing / deprecation noise from the framework
        set_property(TARGET mare PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    else()
        target_link_libraries(mare PRIVATE gdi32 dbghelp ole32 wininet winmm)   # mingw
    endif()
elseif(APPLE)
    set_source_files_properties(mare.c PROPERTIES COMPILE_FLAGS "-ObjC")
    target_link_libraries(mare PRIVATE "-framework Cocoa" "-framework IOKit" "-framework AudioToolbox")
else()
    find_package(Threads REQUIRED)
    target_link_libraries(mare PRIVATE m dl Threads::Threads asound)
endif()

# headless tests: no framework, no GL (each links the C++ core it exercises)
enable_testing()
add_executable(sim_test tests/sim_test.c ${MARE_CORE_CXX})   # nav_ports_precompute runs on the job system
add_executable(diag_test tests/diag_test.c src/core/diag.cc)
add_executable(asset_test tests/asset_test.c src/core/diag.cc)
add_executable(jobs_test tests/jobs_test.cpp)                # includes worker_pool.cc directly
add_executable(stream_test tests/stream_test.c ${MARE_CORE_CXX})   # world streaming loads on the pool
add_executable(entity_test tests/entity_test.cpp src/engine/entity_manager.cc ${MARE_CORE_CXX})
foreach(t sim_test diag_test asset_test jobs_test stream_test entity_test)
    if(WIN32)
        target_compile_definitions(${t} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_link_libraries(${t} PRIVATE m)
    endif()
endforeach()
if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(jobs_test PRIVATE Threads::Threads)
    target_link_libraries(sim_test PRIVATE Threads::Threads)
    target_link_libraries(stream_test PRIVATE Threads::Threads)
    target_link_libraries(entity_test PRIVATE Threads::Threads)
endif()
add_test(NAME entity_test COMMAND entity_test)
add_test(NAME diag_test COMMAND diag_test)
add_test(NAME stream_test COMMAND stream_test)
add_test(NAME asset_test COMMAND asset_test)
add_test(NAME jobs_test COMMAND jobs_test)
add_test(NAME sim_test COMMAND sim_test 2)
