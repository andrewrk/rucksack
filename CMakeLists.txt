cmake_minimum_required(VERSION 2.8)
project(rucksack C)

set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake ${CMAKE_MODULE_PATH})

set(VERSION_MAJOR 1)
set(VERSION_MINOR 0)
set(VERSION_PATCH 1)

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

# check for laxjson
find_package(LaxJson)
if(LAXJSON_FOUND)
  set(STATUS_LAXJSON "OK")
else()
  set(STATUS_LAXJSON "not found")
endif()

configure_file (
  "${PROJECT_SOURCE_DIR}/src/config.h.in"
  "${PROJECT_BINARY_DIR}/config.h"
  )
include_directories(${PROJECT_BINARY_DIR})

set(LIB_SOURCES
  ${PROJECT_SOURCE_DIR}/src/rucksack.c
  )
set(LIB_HEADERS
  ${PROJECT_SOURCE_DIR}/src/rucksack.h
  )
set(EXE_SOURCES
  ${PROJECT_SOURCE_DIR}/src/main.c
  ${PROJECT_SOURCE_DIR}/src/path.c
  )
set(EXE_HEADERS
  ${PROJECT_SOURCE_DIR}/src/rucksack.h
  ${PROJECT_SOURCE_DIR}/src/path.h
  )

set(LIB_CFLAGS "${C99_C_FLAGS} -pedantic -Werror -Wall -Werror=strict-prototypes -Werror=old-style-definition -Werror=missing-prototypes -D_POSIX_C_SOURCE=200809L")
set(EXE_CFLAGS ${LIB_CFLAGS})

include_directories(${FreeImage_INCLUDE_DIRS})
add_library(rucksack_static STATIC ${LIB_SOURCES} ${LIB_HEADERS})
set_target_properties(rucksack_static PROPERTIES
  OUTPUT_NAME rucksack
  COMPILE_FLAGS ${LIB_CFLAGS})

add_library(rucksack_shared SHARED ${LIB_SOURCES} ${LIB_HEADERS})
set_target_properties(rucksack_shared PROPERTIES
  OUTPUT_NAME rucksack
  SOVERSION ${VERSION_MAJOR}
  VERSION ${VERSION}
  COMPILE_FLAGS ${LIB_CFLAGS})
target_link_libraries(rucksack_shared ${FreeImage_LIBRARIES})

add_executable(rucksack ${EXE_SOURCES} ${EXE_HEADERS})
target_link_libraries(rucksack rucksack_shared ${LAXJSON_LIBRARY})
include_directories(${LAXJSON_INCLUDE_DIR})
set_target_properties(rucksack PROPERTIES
  COMPILE_FLAGS ${EXE_CFLAGS})

install(TARGETS rucksack DESTINATION bin)
install(TARGETS rucksack_static rucksack_shared DESTINATION lib)
install(FILES "src/rucksack.h" DESTINATION include)


enable_testing()
include_directories("${PROJECT_SOURCE_DIR}/src")
add_executable(test_library test/test_library.c)
set_target_properties(test_library PROPERTIES
  COMPILE_FLAGS ${EXE_CFLAGS})
target_link_libraries(test_library rucksack_shared)
add_test(LibraryTests test_library)

add_executable(test_path test/test_path.c src/path.c src/path.h)
set_target_properties(test_path PROPERTIES
  COMPILE_FLAGS ${EXE_CFLAGS})
add_test(PathUnitTests test_path)

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
"* laxjson                      : ${STATUS_LAXJSON}\n"
)

message(
"If everything is OK, proceed with\n"
"make\n"
)
