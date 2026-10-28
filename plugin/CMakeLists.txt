INCLUDE(BuildPlugin)

BUILD_PLUGIN(keyfall
	KeyFall.cpp KeyFallView.cpp KeyboardWidget.cpp FallCanvas.cpp CalibrationDialog.cpp LayoutRepository.cpp
	core/Json.cpp core/KeyboardLayout.cpp core/InputResolver.cpp core/Chart.cpp core/PlayheadClock.cpp core/Scorer.cpp
	MOCFILES KeyFall.h KeyFallView.h
	EMBEDDED_RESOURCES logo.png layouts/*.kflayout.json)
