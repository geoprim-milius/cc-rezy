# qRezy - řezy mračnem bodů a kreslení 2D výkresu (MicroStation V7)
option( PLUGIN_STANDARD_QREZY "Plugin Řezy (qRezy)" OFF )

if ( PLUGIN_STANDARD_QREZY )
	project( QREZY_PLUGIN )

	AddPlugin( NAME ${PROJECT_NAME} )

	add_subdirectory( include )
	add_subdirectory( src )

	if ( MSVC )
		target_compile_options( ${PROJECT_NAME} PRIVATE /utf-8 )
	endif()
endif()
