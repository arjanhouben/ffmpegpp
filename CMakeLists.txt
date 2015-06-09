project( ffmpeg++ )

cmake_minimum_required( VERSION 2.8 )

add_subdirectory( externals )

if( CMAKE_CXX_COMPILER MATCHES "clang|g\\+\\+" )
    set( CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS}\ -Wall\ -std=c++11 )
endif()

include_directories(
    ${ffmpeg++_SOURCE_DIR}/externals/install/include
    ${ffmpeg++_SOURCE_DIR}/include
    ${FFMPEG_INCLUDE_DIR}
)

if( MSVC )
    include_directories( ${ffmpeg++_SOURCE_DIR}/externals/inttypes/ )
endif()

link_directories(
	${ffmpeg++_SOURCE_DIR}/externals/install/lib
    ${FFMPEG_LINK_DIR}
)

add_executable( testapp
	src/main.cpp
	include/ffmpeg++.h
)

if(  TARGET ffmpeg )
    add_dependencies( testapp ffmpeg )
endif()

set( EXTRA_LIBS )

if( APPLE )
	foreach( lib VideoDecodeAcceleration CoreFoundation CoreVideo z bz2 iconv )
		find_library( ${lib}_LIBRARY ${lib} PATHS /opt/local/lib NO_DEFAULT_PATH )
		find_library( ${lib}_LIBRARY ${lib} PATHS /opt/local/lib )
		list( APPEND EXTRA_LIBS ${${lib}_LIBRARY} )
	endforeach()
endif()

target_link_libraries( testapp
	avformat
	avutil
	avcodec
	swscale
	swresample
	${EXTRA_LIBS}
)

install(
	FILES
		include/ffmpeg++.h
	DESTINATION
		include/ffmpeg++/
)
