cmake_minimum_required(VERSION 3.6)
project(AceProject C)

# Lowercase project name for binaries and packaging
string(TOLOWER ${PROJECT_NAME} PROJECT_NAME_LOWER)

if(NOT AMIGA)
	message(SEND_ERROR "This project only compiles for Amiga")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DAMIGA -DACE_NO_BOB_WRAP_Y -Wall -Wextra -fomit-frame-pointer")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -DACE_DEBUG -DACE_DEBUG_UAE")

add_subdirectory(deps/ACE ace)
include_directories(deps/ace/include)
include_directories(${PROJECT_BINARY_DIR}/include)

file(GLOB_RECURSE SOURCES src/*.c)
file(GLOB_RECURSE HEADERS src/*.h)

include_directories(${PROJECT_SOURCE_DIR}/src)

# Linux/other UNIX get a lower-case binary name
set(TARGET_NAME ${PROJECT_NAME_LOWER})

if(ELF2HUNK)
  # Add elf2hunk step for Bartman compiler
  set(GAME_LINKED ${TARGET_NAME}.elf) # Intermediate executable
  set(GAME_EXE ${TARGET_NAME}.exe) # Use this to launch the game
  add_executable(${GAME_LINKED} ${SOURCES} ${HEADERS})
  add_custom_command(
    TARGET ${GAME_LINKED} POST_BUILD
    COMMAND ${ELF2HUNK} ${GAME_LINKED} ${GAME_EXE}
  )
  FILE(
    WRITE ${PROJECT_BINARY_DIR}/disassemble.cmake
    "EXECUTE_PROCESS(COMMAND ${OBJDUMP} --disassemble -S ${GAME_LINKED} OUTPUT_FILE ${TARGET_NAME}.s)"
  )
  set(ADF_DIR "${CMAKE_CURRENT_BINARY_DIR}/adf")
  add_custom_command(
    TARGET ${GAME_LINKED} POST_BUILD
    COMMAND ${CMAKE_COMMAND} ARGS -P ${PROJECT_BINARY_DIR}/disassemble.cmake
    COMMAND ${CMAKE_COMMAND} -E make_directory "${ADF_DIR}/s"
    COMMAND ${CMAKE_COMMAND} -E copy "${GAME_EXE}" "${ADF_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "cdtvlauncher.config" "${ADF_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "DieSiedler.iff" "${ADF_DIR}"
    COMMAND ${CMAKE_COMMAND} -E echo "${GAME_EXE}" > "${ADF_DIR}/s/startup-sequence"
    COMMAND exe2adf -l ${CMAKE_PROJECT_NAME} -a "${CMAKE_PROJECT_NAME}.adf" -d "${ADF_DIR}"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${ADF_DIR}"
  )
else()
  # Just produce the executable with Bebbo compiler
  SET(GAME_LINKED ${TARGET_NAME})
  SET(GAME_EXE ${TARGET_NAME})
  add_executable(${GAME_LINKED} ${SOURCES} ${HEADERS})
endif()

target_link_libraries(${GAME_LINKED} ace)

convertFont(
       TARGET ${GAME_LINKED}
       FIRST_CHAR 31
       SOURCE ${PROJECT_SOURCE_DIR}/resources/font.png
       DESTINATION ${PROJECT_BINARY_DIR}/font.fnt
)
make_directory(${PROJECT_BINARY_DIR}/include)
include(bin2h.cmake)
bin2h(
  SOURCE_FILE ${PROJECT_BINARY_DIR}/font.fnt
  HEADER_FILE ${PROJECT_BINARY_DIR}/include/stfont.h
  VARIABLE_NAME S_PFONTDATA
)

file(
  WRITE ${PROJECT_BINARY_DIR}/cdtvlauncher.config
  "Die Siedler\ndh0:DieSiedler/DieSiedler\nApidyaAudios.iff\n"
  "Workbench\nloadwb\ndefault\n"
)

file(
  WRITE ${PROJECT_BINARY_DIR}/cdtvlauncher.script
  ";001\n"
  "loadwb\n"
)
