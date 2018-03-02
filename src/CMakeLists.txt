#project (cef_osr)	

set(ALL_SRCS
	composition.h
	composition.cpp	
	d3d11.h
	d3d11.cpp
	image_layer.cpp
	html_layer.cpp
	main.cpp
	platform.h
	util.cpp
	util.h
)
	
# Logical target used to link the libcef library.
ADD_LOGICAL_TARGET("libcef_lib" "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")

# Determine the target output directory.
SET_CEF_TARGET_OUT_DIR()
	
#include the subdirectory containing our libs
include_directories(${_CEF_ROOT})

#indicate the entry point for the executable
add_executable (cef_osr WIN32 ${ALL_SRCS})
SET_EXECUTABLE_TARGET_PROPERTIES(cef_osr)

source_group("src" FILES ${ALL_SRCS})

add_dependencies(cef_osr libcef_dll_wrapper)

# Indicate which libraries to include during the link process.
target_link_libraries (cef_osr d3d11.lib libcef_lib libcef_dll_wrapper)

COPY_FILES("cef_osr" "${CEF_BINARY_FILES}" "${CEF_BINARY_DIR}" "${TARGET_OUTPUT_DIRECTORY}")
COPY_FILES("cef_osr" "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}" "${TARGET_OUTPUT_DIRECTORY}")
  
