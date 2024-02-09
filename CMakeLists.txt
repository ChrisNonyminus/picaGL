cmake_minimum_required(VERSION 3.22)

project(picaGL)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

file(GLOB_RECURSE PICAGL_SRC CONFIGURE_DEPENDS
        source/*.c)

add_library(picaGL STATIC ${PICAGL_SRC})

target_include_directories(picaGL PUBLIC include)

ctr_add_shader_library(clear source/shaders/clear.v.pica)
ctr_add_shader_library(vshader source/shaders/vshader.v.pica)

target_link_libraries(picaGL PUBLIC -lcitro3d)

dkp_add_embedded_binary_library(shaders_bin ${CMAKE_CURRENT_BINARY_DIR}/clear.shbin ${CMAKE_CURRENT_BINARY_DIR}/vshader.shbin)

target_link_libraries(picaGL PRIVATE shaders_bin)

install(DIRECTORY include/ DESTINATION /opt/devkitpro/libctru/include)
install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lib/ DESTINATION /opt/devkitpro/libctru/lib)


