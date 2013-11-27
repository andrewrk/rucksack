# - Try to find LaxJson
# Once done this will define
#  LAXJSON_FOUND - System has LaxJson
#  LAXJSON_INCLUDE_DIRS - The LaxJson include directories
#  LAXJSON_LIBRARIES - The libraries needed to use LaxJson
#  LAXJSON_DEFINITIONS - Compiler switches required for using LaxJson

find_path(LAXJSON_INCLUDE_DIR laxjson.h)

find_library(LAXJSON_LIBRARY NAMES laxjson liblaxjson)

set(LAXJSON_LIBRARIES ${LAXJSON_LIBRARY} )
set(LAXJSON_INCLUDE_DIRS ${LAXJSON_INCLUDE_DIR} )

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(laxjson DEFAULT_MSG
    LAXJSON_LIBRARY LAXJSON_INCLUDE_DIR)

mark_as_advanced(LAXJSON_INCLUDE_DIR LAXJSON_LIBRARY)
