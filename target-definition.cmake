# Lowercase project name for binaries and packaging
string(TOLOWER ${PROJECT_NAME} PROJECT_NAME_LOWER)

if(NOT AMIGA)
        message(SEND_ERROR "This project only compiles for Amiga")
endif()

if(NOT DEFINED SOURCES)
       message(SEND_ERROR "The the cmake file must define the target's SOURCES variable before including this file")
endif()

set(BARTMAN_DEFAULT_NAME "a.mingw")
if(DEFINED ELF2HUNK)
	set(PROJECT_EXECUTABLE ${PROJECT_NAME_LOWER}.elf)
	set(PROJECT_OUTPUT_EXECUTABLE ${PROJECT_NAME_LOWER}.exe)
	add_executable(${PROJECT_EXECUTABLE} ${SOURCES})
	target_link_libraries(${PROJECT_EXECUTABLE} ace -Wl,-Map=${PROJECT_NAME_LOWER}.map)
	add_custom_command(
		TARGET ${PROJECT_EXECUTABLE} POST_BUILD
		COMMAND ${ELF2HUNK} ${PROJECT_EXECUTABLE} ${PROJECT_OUTPUT_EXECUTABLE}
	)
	# output redirection does not always work on all variants of invoking cmake on windows, so
	# we use this workaround instead
	FILE(WRITE ${PROJECT_BINARY_DIR}/disassemble.cmake
		"EXECUTE_PROCESS(COMMAND ${OBJDUMP} --disassemble -S ${PROJECT_EXECUTABLE} OUTPUT_FILE ${PROJECT_NAME_LOWER}.s)")
	add_custom_command(
		TARGET ${PROJECT_EXECUTABLE} POST_BUILD
		COMMAND ${CMAKE_COMMAND} ARGS -P ${PROJECT_BINARY_DIR}/disassemble.cmake
	)

	# for simpler usage with the default vscode settings for the bartman plugin, we copy the outputs
	add_custom_command(
		TARGET ${PROJECT_EXECUTABLE} POST_BUILD
		COMMAND ${CMAKE_COMMAND} ARGS -E copy ${PROJECT_EXECUTABLE} ${BARTMAN_DEFAULT_NAME}.elf
		COMMAND ${CMAKE_COMMAND} ARGS -E copy ${PROJECT_OUTPUT_EXECUTABLE} ${BARTMAN_DEFAULT_NAME}.exe
		COMMAND ${CMAKE_COMMAND} ARGS -E copy ${PROJECT_NAME_LOWER}.s ${BARTMAN_DEFAULT_NAME}.s
		COMMAND ${CMAKE_COMMAND} ARGS -E copy ${PROJECT_NAME_LOWER}.map ${BARTMAN_DEFAULT_NAME}.map
	)
else()
	SET(PROJECT_EXECUTABLE ${PROJECT_NAME_LOWER})
	SET(PROJECT_OUTPUT_EXECUTABLE ${PROJECT_NAME_LOWER})
	add_executable(${PROJECT_EXECUTABLE} ${SOURCES})
        target_link_libraries(${PROJECT_EXECUTABLE} ace)

        # for simpler usage with the default fs-uae config, we copy the binary
        add_custom_command(
		TARGET ${PROJECT_EXECUTABLE} POST_BUILD
		COMMAND ${CMAKE_COMMAND} ARGS -E copy ${PROJECT_EXECUTABLE} ${BARTMAN_DEFAULT_NAME}.exe
	)
endif()

add_custom_command(
        TARGET ${PROJECT_EXECUTABLE} POST_BUILD
	COMMAND ${CMAKE_COMMAND} ARGS -E copy ${PROJECT_SOURCE_DIR}/config.fs-uae ${PROJECT_BINARY_DIR}/
	COMMAND ${CMAKE_COMMAND} ARGS -E make_directory ${PROJECT_BINARY_DIR}/S
        COMMAND ${CMAKE_COMMAND} ARGS -E copy ${PROJECT_SOURCE_DIR}/S/Startup-Sequence ${PROJECT_BINARY_DIR}/S
        COMMAND ${CMAKE_COMMAND} ARGS -E echo "You can launch the project with: fs-uae ${PROJECT_BINARY_DIR}/config.fs-uae"
)

set(AMIGA_ROMS amiga-os-130.rom amiga-os-204.rom amiga-os-314.rom rom.key)
foreach(rom ${AMIGA_ROMS})
        if(EXISTS ${PROJECT_SOURCE_DIR}/${rom})
            add_custom_command(TARGET ${PROJECT_EXECUTABLE} POST_BUILD
	         COMMAND ${CMAKE_COMMAND} ARGS -E copy ${PROJECT_SOURCE_DIR}/${rom} ${PROJECT_BINARY_DIR}/
            )
        endif()
endforeach()
