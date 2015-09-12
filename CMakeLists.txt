cmake_minimum_required(VERSION 2.8.5)
project(rucksack C)
set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake ${CMAKE_MODULE_PATH})

if(CMAKE_VERSION VERSION_LESS 3.0.0)
    set(CMAKE_INSTALL_LIBDIR "lib" CACHE PATH "library install dir (lib)")
    set(CMAKE_INSTALL_INCLUDEDIR "include" CACHE PATH "header base install dir (include)")
    set(CMAKE_INSTALL_BINDIR "bin" CACHE PATH "user executables (bin)")
else()
    cmake_policy(SET CMP0042 NEW)
    cmake_policy(SET CMP0046 NEW)
    include(GNUInstallDirs)
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING
      "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)
endif()

set(VERSION_MAJOR 3)
set(VERSION_MINOR 1)
set(VERSION_PATCH 0)

set(VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")
message("Configuring rucksack version ${VERSION}")

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

# check for glob.h
find_path(RUCKSACK_HAVE_GLOB NAMES glob.h)

configure_file (
  "${PROJECT_SOURCE_DIR}/src/config.h.in"
  "${PROJECT_BINARY_DIR}/config.h"
  )
include_directories(${PROJECT_BINARY_DIR})

set(RUCKSACK_LIB_SOURCES
  ${PROJECT_SOURCE_DIR}/src/rucksack.c
  )
set(RUCKSACK_LIB_HEADERS
  ${PROJECT_SOURCE_DIR}/src/rucksack.h
  ${PROJECT_SOURCE_DIR}/src/util.h
  ${PROJECT_SOURCE_DIR}/src/shared.h
  )

set(RUCKSACK_SPRITESHEET_LIB_SOURCES
  ${PROJECT_SOURCE_DIR}/src/spritesheet.c
  )
set(RUCKSACK_SPRITESHEET_LIB_HEADERS
  ${PROJECT_SOURCE_DIR}/src/spritesheet.h
  ${PROJECT_SOURCE_DIR}/src/rucksack.h
  ${PROJECT_SOURCE_DIR}/src/shared.h
  )

set(EXE_SOURCES
  ${PROJECT_SOURCE_DIR}/src/main.c
  ${PROJECT_SOURCE_DIR}/src/path.c
  ${PROJECT_SOURCE_DIR}/src/spritesheet.c
  ${PROJECT_SOURCE_DIR}/src/stringlist.c
  )
set(EXE_HEADERS
  ${PROJECT_SOURCE_DIR}/src/rucksack.h
  ${PROJECT_SOURCE_DIR}/src/spritesheet.h
  ${PROJECT_SOURCE_DIR}/src/path.h
  ${PROJECT_SOURCE_DIR}/src/stringlist.h
  ${PROJECT_SOURCE_DIR}/src/util.h
  ${PROJECT_SOURCE_DIR}/src/mkdirp.h
  )


set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Werror -pedantic")

set(LIB_CFLAGS "-std=c99 -Wall -Werror=strict-prototypes -Werror=old-style-definition -Werror=missing-prototypes -D_POSIX_C_SOURCE=200809L -Wno-gnu-folding-constant")
set(EXE_CFLAGS ${LIB_CFLAGS})

add_library(rucksack_static STATIC ${RUCKSACK_LIB_SOURCES} ${RUCKSACK_LIB_HEADERS})
set_target_properties(rucksack_static PROPERTIES
  OUTPUT_NAME rucksack
  COMPILE_FLAGS ${LIB_CFLAGS})

add_library(rucksack_shared SHARED ${RUCKSACK_LIB_SOURCES} ${RUCKSACK_LIB_HEADERS})
set_target_properties(rucksack_shared PROPERTIES
  OUTPUT_NAME rucksack
  SOVERSION ${VERSION_MAJOR}
  VERSION ${VERSION}
  COMPILE_FLAGS ${LIB_CFLAGS})


include_directories(${FreeImage_INCLUDE_DIRS})
add_library(rucksackspritesheet_static STATIC
  ${RUCKSACK_SPRITESHEET_LIB_SOURCES} ${RUCKSACK_SPRITESHEET_LIB_HEADERS})
set_target_properties(rucksackspritesheet_static PROPERTIES
  OUTPUT_NAME rucksackspritesheet
  COMPILE_FLAGS ${LIB_CFLAGS})

add_library(rucksackspritesheet_shared SHARED
  ${RUCKSACK_SPRITESHEET_LIB_SOURCES} ${RUCKSACK_SPRITESHEET_LIB_HEADERS})
set_target_properties(rucksackspritesheet_shared PROPERTIES
  OUTPUT_NAME rucksackspritesheet
  SOVERSION ${VERSION_MAJOR}
  VERSION ${VERSION}
  COMPILE_FLAGS ${LIB_CFLAGS})
target_link_libraries(rucksackspritesheet_shared rucksack_shared ${FreeImage_LIBRARIES})



add_executable(rucksack ${EXE_SOURCES} ${EXE_HEADERS})
target_link_libraries(rucksack rucksack_shared rucksackspritesheet_shared ${LAXJSON_LIBRARY})
include_directories(${LAXJSON_INCLUDE_DIR})
set_target_properties(rucksack PROPERTIES
  COMPILE_FLAGS ${EXE_CFLAGS})

install(TARGETS rucksack DESTINATION bin)
install(TARGETS
  rucksack_static rucksack_shared
  rucksackspritesheet_static rucksackspritesheet_shared
  DESTINATION lib)
install(FILES "src/rucksack.h" DESTINATION include/rucksack)
install(FILES "src/spritesheet.h" DESTINATION include/rucksack)


enable_testing()
include_directories("${PROJECT_SOURCE_DIR}/src")
add_executable(test_library test/test_library.c)
set_target_properties(test_library PROPERTIES
  COMPILE_FLAGS ${EXE_CFLAGS})
target_link_libraries(test_library rucksack_shared rucksackspritesheet_shared)
add_test(LibraryTests test_library)

add_executable(test_path test/test_path.c src/path.c src/path.h)
set_target_properties(test_path PROPERTIES
  COMPILE_FLAGS ${EXE_CFLAGS})
add_test(PathUnitTests test_path)

add_executable(test_stringlist test/test_stringlist.c src/stringlist.c src/stringlist.h)
set_target_properties(test_stringlist PROPERTIES
  COMPILE_FLAGS ${EXE_CFLAGS})
add_test(StringListTests test_stringlist)

message("\n"
"Installation Summary\n"
"--------------------\n"
"* Install Directory            : ${CMAKE_INSTALL_PREFIX}\n"
"* Build Type                   : ${CMAKE_BUILD_TYPE}\n"
"* Build librucksack            : yes\n"
"* Build librucksackspritesheet : yes\n"
"* Build rucksack               : yes\n"
)

message(
"System Dependencies\n"
"-------------------\n"
"* C99 Compiler                 : ${STATUS_C99}\n"
"* freeimage                    : ${STATUS_FREEIMG}\n"
"* laxjson                      : ${STATUS_LAXJSON}\n"
)
