cmake_minimum_required(VERSION 3.24)
project(MediaPlayer LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)

set(SOURCE_FILES
    src/VideoState.cpp
    src/main.cpp)

add_executable(${PROJECT_NAME} ${SOURCE_FILES})

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL3 REQUIRED sdl3)
pkg_check_modules(AVCODEC REQUIRED libavcodec)
pkg_check_modules(AVFORMAT REQUIRED libavformat)
pkg_check_modules(AVSWSCALE REQUIRED libswscale)
pkg_check_modules(AVSWRESAMPLE REQUIRED libswresample)
pkg_check_modules(AVUTIL REQUIRED libavutil)

target_include_directories(${PROJECT_NAME} PRIVATE
                           include
                           ${SDL3_INCLUDE_DIRS}
                           ${AVFORMAT_INCLUDE_DIRS}
                           ${AVCODEC_INCLUDE_DIRS}
                           ${AVSWSCALE_INCLUDE_DIRS}
                           ${AVSWRESAMPLE_INCLUDE_DIRS}
                           ${AVUTIL_INCLUDE_DIRS})

target_link_libraries(${PROJECT_NAME} PRIVATE
                      ${SDL3_LIBRARIES}
                      ${AVFORMAT_LIBRARIES}
                      ${AVCODEC_LIBRARIES}
                      ${AVSWSCALE_LIBRARIES}
                      ${AVSWRESAMPLE_LIBRARIES}
                      ${AVUTIL_LIBRARIES})
