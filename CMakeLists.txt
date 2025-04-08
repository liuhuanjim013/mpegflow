cmake_minimum_required(VERSION 3.10)
project(MPEGFlow)

# Set the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Define compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -D__STDC_CONSTANT_MACROS")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")

find_package(FFmpeg REQUIRED)

if(FFMPEG_FOUND)
    message(STATUS "FFmpeg found: ${FFMPEG_LIBRARIES} ${FFMPEG_INCLUDE_DIRS}")
    include_directories(${FFMPEG_INCLUDE_DIRS})
else()
    message(FATAL_ERROR "FFmpeg not found!")
endif()

# Define source files for mpegflow
set(MPEGFLOW_SOURCES mpegflow.cpp)

# Add executable for mpegflow
add_executable(mpegflow ${MPEGFLOW_SOURCES})

# Link libraries for mpegflow
target_link_libraries(mpegflow 
    swscale 
    avdevice 
    avformat 
    avcodec 
    swresample 
    avutil 
    pthread 
    bz2 
    z 
    c
)

# Optional vis executable
option(BUILD_VIS "Build the vis executable" OFF)

if(BUILD_VIS)
    # Define source files for vis
    set(VIS_SOURCES vis.cpp)

    # Add executable for vis
    add_executable(vis ${VIS_SOURCES})

    # Include OpenCV only if vis is being built
    include_directories(${OPENCV_DIR})
    
    # Find OpenCV package
    find_package(OpenCV REQUIRED)

    # Link libraries for vis including OpenCV
    target_link_libraries(vis 
        ${OpenCV_LIBS} 
        png 
        swscale 
        avdevice 
        avformat 
        avcodec 
        swresample 
        avutil 
        pthread 
        bz2 
        z 
        c
    )
endif()

# Clean target (not directly supported in CMake, handled by build system)
