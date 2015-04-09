project( ffmpeg++ )

set( CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS}\ -Wall\ -std=c++11 )

include_directories(
	include
	/opt/local/include
)

link_directories(
	/opt/local/lib
)

add_executable( testapp
	src/main.cpp
	include/ffmpeg++.h
)

target_link_libraries( testapp
	avformat
	avutil
	avcodec
)