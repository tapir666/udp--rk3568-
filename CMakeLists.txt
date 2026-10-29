cmake_minimum_required(VERSION 3.10)

# 设置交叉编译
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
# 这里指定交叉编译工具链路径，必须是绝对路径
set(CMAKE_C_COMPILER /usr/local/arm-linux/bin/arm-linux-gcc)
set(CMAKE_CXX_COMPILER /usr/local/arm-linux/bin/aarch64-linux-gnu-g++)

#指定工程名字
project(lvgl)

#设置C C++标准
set(CMAKE_C_STANDARD 99)#C99 # lvgl officially support C99 and above
set(CMAKE_CXX_STANDARD 17)#C17
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#设置工程可执行文件输出路径 
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

add_subdirectory(lvgl)
target_include_directories(lvgl PUBLIC ${PROJECT_SOURCE_DIR})

aux_source_directory(./mysrc/src SRCS)
include_directories(${PROJECT_SOURCE_DIR}/mysrc/inc)

add_executable(cmd_main main.c mouse_cursor_icon.c ${SRCS})

target_link_libraries(cmd_main lvgl lvgl::examples lvgl::demos lvgl::thorvg ${SDL2_LIBRARIES} m pthread)
add_custom_target (run COMMAND ${EXECUTABLE_OUTPUT_PATH}/cmd_main DEPENDS cmd_main)

