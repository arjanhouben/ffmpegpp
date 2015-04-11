project( externals )

set( prefix ${externals_SOURCE_DIR}/install )

file( MAKE_DIRECTORY ${prefix} )

add_custom_command(
	OUTPUT
		ffmpeg/Makefile
	COMMAND
		./configure --prefix=${externals_SOURCE_DIR}/install
	WORKING_DIRECTORY
		ffmpeg
)

add_custom_target( ffmpeg
	COMMAND
		make install
	DEPENDS
		ffmpeg/Makefile
)
