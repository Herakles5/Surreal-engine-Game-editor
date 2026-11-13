cmake_minimum_required(VERSION 3.16)
project(SurrealEngine VERSION 2.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenGL REQUIRED)
find_package(SDL2 REQUIRED)
find_package(Threads REQUIRED)
find_package(OpenMP REQUIRED)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx2 -mfma -O3 ${OpenMP_CXX_FLAGS}")

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SDL2_INCLUDE_DIRS}
    ${OPENGL_INCLUDE_DIRS}
)

# Shared third-party implementation compilation unit (prevents ODR duplication across TUs)
add_library(surreal_third_party STATIC surreal_third_party.cpp)

# 1. Surreal 3D Studio & Live Game Editor
add_executable(surreal-editor surreal_live_dojo.cpp)
target_compile_definitions(surreal-editor PRIVATE SURREAL_EXTERNAL_THIRD_PARTY_IMPL)
target_link_libraries(surreal-editor
    PRIVATE
    surreal_third_party
    ${OPENGL_LIBRARIES}
    ${SDL2_LIBRARIES}
    Threads::Threads
    OpenMP::OpenMP_CXX
    ${CMAKE_DL_LIBS}
    m
)

# 2. Surreal Automated Benchmark
add_executable(surreal-bench test_surreal_benchmark.cpp)
target_compile_definitions(surreal-bench PRIVATE SURREAL_EXTERNAL_THIRD_PARTY_IMPL)
target_link_libraries(surreal-bench
    PRIVATE
    surreal_third_party
    ${OPENGL_LIBRARIES}
    ${SDL2_LIBRARIES}
    Threads::Threads
    OpenMP::OpenMP_CXX
    m
)

# 3. Photo-to-3D Offline Preview Tool
add_executable(surreal-photo3d photo3d_preview.cpp)
target_compile_definitions(surreal-photo3d PRIVATE SURREAL_EXTERNAL_THIRD_PARTY_IMPL)
target_link_libraries(surreal-photo3d
    PRIVATE
    surreal_third_party
    Threads::Threads
    OpenMP::OpenMP_CXX
    m
)

# 4. Headless Engine Verification Suite & glTF Check
add_executable(test_surreal_suite test_surreal_suite.cpp)
target_compile_definitions(test_surreal_suite PRIVATE SURREAL_EXTERNAL_THIRD_PARTY_IMPL)
target_link_libraries(test_surreal_suite
    PRIVATE
    surreal_third_party
    Threads::Threads
    OpenMP::OpenMP_CXX
    m
)

add_executable(test_surreal_gltf test_surreal_gltf.cpp)
target_compile_definitions(test_surreal_gltf PRIVATE SURREAL_EXTERNAL_THIRD_PARTY_IMPL)
target_link_libraries(test_surreal_gltf
    PRIVATE
    surreal_third_party
    Threads::Threads
    OpenMP::OpenMP_CXX
    m
)

enable_testing()
add_test(NAME surreal_suite COMMAND test_surreal_suite WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME surreal_gltf COMMAND test_surreal_gltf WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME surreal_dojo_headless COMMAND surreal-editor --benchmark-headless WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

# Install rules
install(TARGETS surreal-editor surreal-bench surreal-photo3d DESTINATION bin)
install(FILES
    surreal_engine.hpp
    surreal_hybrid_renderer.hpp
    surreal_math.hpp
    surreal_pbr.hpp
    surreal_meshlet.hpp
    surreal_terrain.hpp
    surreal_animation.hpp
    surreal_gltf.hpp
    surreal_ecs.hpp
    surreal_camera.hpp
    surreal_hud.hpp
    surreal_model_loader.hpp
    surreal_editor.hpp
    surreal_image3d.hpp
    surreal_auto_rig.hpp
    surreal_aion_link.hpp
    surreal_game_master.hpp
    surreal_paths.hpp
    surreal_json.hpp
    cgltf.h
    stb_image.h
    DESTINATION include/surreal
)
