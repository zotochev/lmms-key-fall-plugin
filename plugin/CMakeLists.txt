INCLUDE(BuildPlugin)

BUILD_PLUGIN(keyfall
	KeyFall.cpp KeyFallView.cpp KeyboardWidget.cpp LayoutRepository.cpp
	core/Json.cpp core/KeyboardLayout.cpp core/InputResolver.cpp
	MOCFILES KeyFall.h KeyFallView.h
	EMBEDDED_RESOURCES logo.png layouts/*.kflayout.json)
