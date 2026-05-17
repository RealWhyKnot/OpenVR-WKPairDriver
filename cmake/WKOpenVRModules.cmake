# Feature-module gating for WKOpenVR.
#
# Each feature plugin lives at modules/<slug>/ with up to four subtrees:
# src/overlay, src/driver, src/host (sidecar), and tests/. A module that is
# considered too unstable for the public release ships an empty marker file
# at modules/<slug>/disabled-in-release.flag. When the umbrella build is
# invoked with -DWKOPENVR_RELEASE_BUILD=ON, the helpers in this file skip
# add_subdirectory for any marked module across all four subtrees, so the
# disabled feature's code never links into the release binary. Dev builds
# (the default) ignore the marker and link everything.

function(wkopenvr_module_disabled slug out_var)
	set(${out_var} FALSE PARENT_SCOPE)
	if(WKOPENVR_RELEASE_BUILD AND EXISTS
			"${CMAKE_SOURCE_DIR}/modules/${slug}/disabled-in-release.flag")
		set(${out_var} TRUE PARENT_SCOPE)
	endif()
endfunction()

function(wkopenvr_add_overlay_module slug)
	wkopenvr_module_disabled(${slug} _disabled)
	if(_disabled)
		message(STATUS "Skipping overlay module '${slug}' (disabled-in-release)")
		return()
	endif()
	string(TOUPPER ${slug} _u)
	add_subdirectory(
		"${CMAKE_SOURCE_DIR}/modules/${slug}/src/overlay"
		"${CMAKE_BINARY_DIR}/modules/${slug}-overlay")
	set(OPENVR_PAIR_OVERLAY_FEATURE_LIBS
		${OPENVR_PAIR_OVERLAY_FEATURE_LIBS}
		openvr_pair_feature_${slug}_overlay PARENT_SCOPE)
	set(OPENVR_PAIR_OVERLAY_DEFINES
		${OPENVR_PAIR_OVERLAY_DEFINES}
		OPENVR_PAIR_HAS_${_u}_OVERLAY=1 PARENT_SCOPE)
endfunction()

function(wkopenvr_add_driver_module slug)
	wkopenvr_module_disabled(${slug} _disabled)
	if(_disabled)
		message(STATUS "Skipping driver module '${slug}' (disabled-in-release)")
		return()
	endif()
	string(TOUPPER ${slug} _u)
	add_subdirectory(
		"${CMAKE_SOURCE_DIR}/modules/${slug}/src/driver"
		"${CMAKE_BINARY_DIR}/modules/${slug}-driver")
	set(OPENVR_PAIR_FEATURE_LIBS
		${OPENVR_PAIR_FEATURE_LIBS}
		openvr_pair_feature_${slug}_driver PARENT_SCOPE)
	set(OPENVR_PAIR_FEATURE_DEFINES
		${OPENVR_PAIR_FEATURE_DEFINES}
		OPENVR_PAIR_HAS_${_u}_DRIVER=1 PARENT_SCOPE)
endfunction()

function(wkopenvr_add_host_module slug)
	wkopenvr_module_disabled(${slug} _disabled)
	if(_disabled)
		message(STATUS "Skipping host module '${slug}' (disabled-in-release)")
		return()
	endif()
	add_subdirectory("${CMAKE_SOURCE_DIR}/modules/${slug}/src/host")
endfunction()

function(wkopenvr_add_module_tests slug)
	wkopenvr_module_disabled(${slug} _disabled)
	if(_disabled)
		message(STATUS "Skipping tests for module '${slug}' (disabled-in-release)")
		return()
	endif()
	add_subdirectory(
		"${CMAKE_SOURCE_DIR}/modules/${slug}/tests"
		"${CMAKE_BINARY_DIR}/tests/${slug}")
endfunction()
