# FindFFmpeg.cmake
# This module finds the required FFmpeg components including swscale.

# Define possible paths for macOS
set(FFMPEG_INCLUDE_DIRS
    /usr/local/include
    /opt/homebrew/include  # For Apple Silicon (M1/M2) installations via Homebrew
    /usr/include
)

set(FFMPEG_LIBRARY_DIRS
    /usr/local/lib
    /opt/homebrew/lib      # For Apple Silicon (M1/M2) installations via Homebrew
    /usr/lib
)

# Set the required components
set(FFMPEG_FIND_COMPONENTS avcodec avformat avutil swscale)

# Find the necessary libraries
foreach(component ${FFMPEG_FIND_COMPONENTS})
    find_library(FFMPEG_${component}_LIBRARY NAMES ${component} PATHS ${FFMPEG_LIBRARY_DIRS})
    find_path(FFMPEG_${component}_INCLUDE_DIR NAMES lib${component}/${component}.h PATHS ${FFMPEG_INCLUDE_DIRS})
    set(FFMPEG_${component}_INCLUDE_DIR ${FFMPEG_${component}_INCLUDE_DIR}/lib${component})
    if(FFMPEG_${component}_LIBRARY AND FFMPEG_${component}_INCLUDE_DIR)
        set(FFMPEG_FOUND TRUE)
        set(FFMPEG_${component}_FOUND TRUE)
        message(STATUS "Found FFmpeg ${component}: ${FFMPEG_${component}_LIBRARY} ${FFMPEG_${component}_INCLUDE_DIR}")
    else()
        set(FFMPEG_${component}_FOUND FALSE)
    endif()
endforeach()

# Check if all required components were found
if(FFMPEG_FOUND)
    mark_as_advanced(FFMPEG_INCLUDE_DIRS FFMPEG_LIBRARIES FFMPEG_DEFINITIONS)
else()
    message(FATAL_ERROR "Could not find all required FFmpeg components.")
endif()

# Set the include directories and libraries for usage
set(FFMPEG_INCLUDE_DIRS
    ${FFMPEG_INCLUDE_DIRS}
    ${FFMPEG_avcodec_INCLUDE_DIR}
    ${FFMPEG_avformat_INCLUDE_DIR}
    ${FFMPEG_avutil_INCLUDE_DIR}
    ${FFMPEG_swscale_INCLUDE_DIR}
)
set(FFMPEG_INCLUDE_DIRS ${FFMPEG_INCLUDE_DIRS} PARENT_SCOPE)
set(FFMPEG_LIBRARIES
    ${FFMPEG_avcodec_LIBRARY}
    ${FFMPEG_avformat_LIBRARY}
    ${FFMPEG_avutil_LIBRARY}
    ${FFMPEG_swscale_LIBRARY}
    PARENT_SCOPE
)

# Provide user-friendly variables
set(FFmpeg_FOUND TRUE CACHE BOOL "Indicates if FFmpeg was found")
