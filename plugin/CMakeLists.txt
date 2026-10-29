# KeyFall for LMMS 1.2.x (branch lmms-1.2), built against installed LMMS headers (Linux):
#   cmake -S plugin -B build -DLMMS_INCLUDE_DIR=/usr/include/lmms && cmake --build build
# then copy build/libkeyfall.so into LMMS's plug-in folder (or point LMMS_PLUGIN_DIR at build/).
# The headers must come from the LMMS the plug-in is loaded into: LMMS has no stable ABI.
cmake_minimum_required(VERSION 3.16)
project(keyfall LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Qt 5, which LMMS 1.2 is built with; the plug-in shares LMMS's Qt at run time.
#   Widgets - the KeyFall window; Xml - LMMS's plug-in base classes take QDomElement in their API
find_package(Qt5 REQUIRED COMPONENTS Widgets Xml)
# LMMS: headers only. Its symbols (ToolPlugin, Song, InstrumentTrack, ...) are resolved
# from the running lmms binary when the plug-in is loaded, so nothing is linked here.
find_path(LMMS_INCLUDE_DIR lmmsconfig.h PATH_SUFFIXES lmms REQUIRED)

# Resources (logo, built-in layouts) under :/artwork/keyfall/, read by LayoutRepository and KeyFall.cpp
file(GLOB KEYFALL_LAYOUTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/layouts/*.kflayout.json")
set(KEYFALL_QRC "${CMAKE_CURRENT_BINARY_DIR}/keyfall.qrc")
set(QRC_BODY "<RCC><qresource prefix=\"/artwork/keyfall\">\n")
foreach(file "${CMAKE_CURRENT_SOURCE_DIR}/logo.png" ${KEYFALL_LAYOUTS})
	get_filename_component(name "${file}" NAME)
	string(APPEND QRC_BODY "<file alias=\"${name}\">${file}</file>\n")
endforeach()
file(WRITE "${KEYFALL_QRC}" "${QRC_BODY}</qresource></RCC>\n")

add_library(keyfall MODULE
	KeyFall.cpp KeyFallView.cpp KeyboardWidget.cpp FallCanvas.cpp CalibrationDialog.cpp LayoutRepository.cpp
	core/Json.cpp core/KeyboardLayout.cpp core/InputResolver.cpp core/Chart.cpp core/PlayheadClock.cpp core/Scorer.cpp core/Practice.cpp
	"${KEYFALL_QRC}")
# SYSTEM: LMMS 1.2 headers are full of Qt deprecation warnings that are not ours to fix
target_include_directories(keyfall SYSTEM PRIVATE "${LMMS_INCLUDE_DIR}")
target_compile_definitions(keyfall PRIVATE PLUGIN_NAME=keyfall)
target_link_libraries(keyfall PRIVATE Qt5::Widgets Qt5::Xml)

include(GNUInstallDirs)
install(TARGETS keyfall LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/lmms")
