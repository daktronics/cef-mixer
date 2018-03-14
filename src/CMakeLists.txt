
set(ALL_SRCS
	app.rc
	composition.h
	composition.cpp	
	d3d11.h
	d3d11.cpp
	image_layer.cpp
	html_layer.cpp
	main.cpp
	platform.h
	resource.h
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
add_executable (cefmixer WIN32 ${ALL_SRCS})
SET_EXECUTABLE_TARGET_PROPERTIES(cefmixer)

source_group("src" FILES ${ALL_SRCS})

add_dependencies(cefmixer libcef_dll_wrapper)

# Indicate which libraries to include during the link process.
target_link_libraries (cefmixer d3d11.lib Shlwapi.lib libcef_lib libcef_dll_wrapper)

COPY_FILES(cefmixer "${CEF_BINARY_FILES}" "${CEF_BINARY_DIR}" "${TARGET_OUTPUT_DIRECTORY}")
COPY_FILES(cefmixer "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}" "${TARGET_OUTPUT_DIRECTORY}")

# copy our test overlay image to the output directory
add_custom_command(
        TARGET cefmixer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_SOURCE_DIR}/resource/overlay.png
                ${TARGET_OUTPUT_DIRECTORY}/overlay.png)
  
