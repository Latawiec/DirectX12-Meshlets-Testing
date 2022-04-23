cmake_minimum_required(VERSION 3.9)

project(MeshShaderSample)

add_subdirectory(DirectXMesh)

add_executable(${PROJECT_NAME} WIN32 src/main.cpp)
add_dependencies(${PROJECT_NAME}
    DirectXMesh
    Utilities
)
add_definitions(
    -DUNICODE
    -DSOURCE_PATH=L"${PROJECT_SOURCE_DIR}/src/"
    -DDEBUG
    -DASSETS_PATH=L"${PROJECT_SOURCE_DIR}/assets/"
)

target_link_libraries(${PROJECT_NAME}
PRIVATE
    d3d12.lib
    dxgi.lib
    d3dcompiler.lib
    dxcompiler.lib
    DirectXMesh
    Utilities
)

