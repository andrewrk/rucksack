cmake_minimum_required(VERSION 2.8)
project(rucksack C)

set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake ${CMAKE_MODULE_PATH})

set(VERSION_MAJOR 0)
set(VERSION_MINOR 0)
set(VERSION_PATCH 0)

set(VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")
message("Configuring rucksack version ${VERSION}")

# check for C99
find_package(C99)
if(C99_FLAG_DETECTED)
  set(STATUS_C99 "OK")
else(C99_FLAG_DETECTED)
  set(STATUS_C99 "not found")
endif(C99_FLAG_DETECTED)

# check for freeimage
find_package(FreeImage)
if(FreeImage_FOUND)
  set(STATUS_FREEIMG "OK")
else(FreeImage_FOUND)
  set(STATUS_FREEIMG "not found")
endif(FreeImage_FOUND)

configure_file (
  "${PROJECT_SOURCE_DIR}/src/config.h.in"
  "${PROJECT_BINARY_DIR}/config.h"
  )

file(GLOB_RECURSE SOURCES ${PROJECT_SOURCE_DIR}/src/*.c)
file(GLOB_RECURSE HEADERS ${PROJECT_SOURCE_DIR}/src/*.h)

set(EXE_CFLAGS "${C99_C_FLAGS} -pedantic -Werror -Wall -Werror=strict-prototypes -Werror=old-style-definition -Werror=missing-prototypes -g -O0")

add_executable(rucksack ${SOURCES} ${HEADERS})
target_link_libraries(rucksack ${FreeImage_LIBRARIES})
include_directories(${FreeImage_INCLUDE_DIRS})
set_target_properties(rucksack PROPERTIES
  COMPILE_FLAGS ${EXE_CFLAGS})


install(TARGETS rucksack DESTINATION lib)

message("\n"
"Installation Summary\n"
"--------------------\n"
"* Install Directory            : ${CMAKE_INSTALL_PREFIX}\n"
)

message(
"Required Dependencies\n"
"------------------\n"
"* C99 Compiler                 : ${STATUS_C99}\n"
"* freeimage                    : ${STATUS_FREEIMG}\n"
)

message(
"If everything is OK, proceed with\n"
"make\n"
)
