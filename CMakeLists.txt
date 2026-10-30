cmake_minimum_required(VERSION 3.16)
project(SurrealEngine VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

# 1. Surreal 3D Studio & Live Game Editor
add_executable(surreal-editor surreal_live_dojo.cpp)
target_link_libraries(surreal-editor
    PRIVATE
    ${OPENGL_LIBRARIES}
    ${SDL2_LIBRARIES}
    Threads::Threads
    OpenMP::OpenMP_CXX
    ${CMAKE_DL_LIBS}
    m
)

# 2. Surreal Automated Benchmark
add_executable(surreal-bench test_surreal_benchmark.cpp)
target_link_libraries(surreal-bench
    PRIVATE
    ${OPENGL_LIBRARIES}
    ${SDL2_LIBRARIES}
    Threads::Threads
    OpenMP::OpenMP_CXX
    m
)

# Install rules
install(TARGETS surreal-editor surreal-bench DESTINATION bin)
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
    cgltf.h
    stb_image.h
    DESTINATION include/surreal
)

