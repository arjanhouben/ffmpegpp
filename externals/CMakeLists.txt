project( externals )

cmake_minimum_required( VERSION 2.8 )

set( prefix ${externals_SOURCE_DIR}/install )

file( MAKE_DIRECTORY ${prefix} )

add_custom_command(
	OUTPUT
		${externals_SOURCE_DIR}/ffmpeg/config.mak
	COMMAND
		./configure --prefix=${externals_SOURCE_DIR}/install
	WORKING_DIRECTORY
		${externals_SOURCE_DIR}/ffmpeg
)

add_custom_target( ffmpeg
	COMMAND
		make install
	DEPENDS
		${externals_SOURCE_DIR}/ffmpeg/config.mak
	WORKING_DIRECTORY
		${externals_SOURCE_DIR}/ffmpeg
)
