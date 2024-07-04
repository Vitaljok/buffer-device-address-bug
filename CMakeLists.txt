cmake_minimum_required(VERSION 3.16)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project(BufferBug)

# custom find scripts (fetch from github)
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

find_package(glfw3 3.3 REQUIRED)
find_package(glm 1.0 REQUIRED)
find_package(Vulkan 1.3 REQUIRED)
find_package(vkBootstrap 1.3 REQUIRED)
include(CompileShaders)

add_executable(buffer-bug main.cpp)
target_compile_definitions(buffer-bug PRIVATE VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1)
# target_include_directories(buffer-bug PRIVATE ${PROJECT_SOURCE_DIR}) 
target_link_libraries(buffer-bug PUBLIC glfw Vulkan::Vulkan vk-bootstrap::vk-bootstrap glm::glm)

add_dependencies(buffer-bug shaders)
add_custom_target(shaders 
    SOURCES 
    simple.vert
    simple.frag    
)
compile_shaders(shaders)