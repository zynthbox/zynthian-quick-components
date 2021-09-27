# - Try to find the ZynthiLoops library
# Once done this will define
#  LIBZL_FOUND - System has LibZL
#  LIBZL_INCLUDE_DIRS - The LibZL include directories
#  LIBZL_LIBRARIES - The libraries needed to use LibZL

find_path(LIBZL_INCLUDE_DIR libzl.h)

find_library(LIBZL_LIBRARY NAMES zl libzl)

include(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set LIBZL_FOUND to TRUE
# if all listed variables are TRUE
find_package_handle_standard_args(LibZL  DEFAULT_MSG
                                  LIBZL_LIBRARY LIBZL_INCLUDE_DIR)

mark_as_advanced(LIBZL_INCLUDE_DIR LIBZL_LIBRARY )

set(LIBZL_LIBRARIES ${LIBZL_LIBRARY} )
set(LIBZL_INCLUDE_DIRS
    ${LIBZL_INCLUDE_DIR} # The main ZynthiLoops include location (just libzl.h)
    ${LIBZL_INCLUDE_DIR}/libzl # Home of the other ZynthiLoops include files (like SyncTimer.h and ClipAudioSource.h)
    ${LIBZL_INCLUDE_DIR}/JUCE-6.0.8 # Home of the JUCE headers
    ${LIBZL_INCLUDE_DIR}/JUCE-6.0.8/modules # Home of the JUCE modules
    ${LIBZL_INCLUDE_DIR}/JUCE- # Home of the tracktion engine
    ${LIBZL_INCLUDE_DIR}/JUCE-/modules # Home of the tracktion engine modules
)

message("-- Found The ZynthiLoops Library: ${LIBZL_LIBRARIES} ${LIBZL_INCLUDE_DIRS}")
