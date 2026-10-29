set(KEYFALL_SOURCES
	KeyFall.cpp KeyFallView.cpp KeyboardWidget.cpp FallCanvas.cpp CalibrationDialog.cpp LayoutRepository.cpp
	core/Json.cpp core/KeyboardLayout.cpp core/InputResolver.cpp core/Chart.cpp core/PlayheadClock.cpp core/Scorer.cpp core/Practice.cpp)

# Inside the LMMS source tree (plugins/KeyFall): the usual LMMS plug-in build
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	include(BuildPlugin)
	build_plugin(keyfall ${KEYFALL_SOURCES}
		MOCFILES KeyFall.h KeyFallView.h
		EMBEDDED_RESOURCES logo.png layouts/*.kflayout.json)
	return()
endif()

# Standalone, against installed LMMS headers (Linux):
#   cmake -S plugin -B build -DLMMS_INCLUDE_DIR=/usr/include/lmms && cmake --build build
# then copy build/libkeyfall.so into LMMS's plug-in folder (or point LMMS_PLUGIN_DIR at build/).
# The headers must come from the same LMMS the plug-in is loaded into: LMMS has no stable ABI.
cmake_minimum_required(VERSION 3.16)
project(keyfall LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Qt: must be the major version LMMS itself uses, the plug-in shares its Qt at run time.
#   Widgets - the KeyFall window; Xml - LMMS's plug-in base classes take QDomElement in their API
set(QT_VERSION_MAJOR 5 CACHE STRING "Qt major version LMMS was built with (5 or 6)")
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Xml)
# LMMS: headers only. Its symbols (ToolPlugin, Song, InstrumentTrack, ...) are resolved
# from the running lmms binary when the plug-in is loaded, so nothing is linked here.
find_path(LMMS_INCLUDE_DIR lmmsconfig.h PATH_SUFFIXES lmms REQUIRED)

# Resources at the paths LMMS's embed loader and LayoutRepository expect
file(GLOB KEYFALL_LAYOUTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/layouts/*.kflayout.json")
set(KEYFALL_QRC "${CMAKE_CURRENT_BINARY_DIR}/keyfall.qrc")
set(QRC_BODY "<RCC><qresource prefix=\"/artwork/keyfall\">\n")
foreach(file "${CMAKE_CURRENT_SOURCE_DIR}/logo.png" ${KEYFALL_LAYOUTS})
	get_filename_component(name "${file}" NAME)
	string(APPEND QRC_BODY "<file alias=\"${name}\">${file}</file>\n")
endforeach()
file(WRITE "${KEYFALL_QRC}" "${QRC_BODY}</qresource></RCC>\n")

add_library(keyfall MODULE ${KEYFALL_SOURCES} "${KEYFALL_QRC}")
target_include_directories(keyfall PRIVATE "${LMMS_INCLUDE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(keyfall PRIVATE PLUGIN_NAME=keyfall)
target_link_libraries(keyfall PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Xml)

include(GenerateExportHeader)
generate_export_header(keyfall BASE_NAME PLUGIN) # plugin_export.h
# LMMS installs its headers but not the generated lmms_export.h; on Linux every LMMS
# symbol is visible anyway, so an empty import macro is all the headers need
if(NOT EXISTS "${LMMS_INCLUDE_DIR}/lmms_export.h")
	file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/lmms_export.h" "#pragma once\n#define LMMS_EXPORT\n#define LMMS_NO_EXPORT\n")
endif()

include(GNUInstallDirs)
install(TARGETS keyfall LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/lmms")
