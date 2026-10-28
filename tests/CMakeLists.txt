# Standalone build of the KeyFall core (no Qt, no LMMS): unit tests and kflayout-lint.
# Usage: scripts\lmms.cmd test   (or: cmake -S tests -B build-tests && cmake --build build-tests && ctest --test-dir build-tests)
cmake_minimum_required(VERSION 3.16)
project(keyfall_core_tests CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
	add_compile_options(/W4 /permissive- /utf-8)
else()
	add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(KEYFALL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(CORE_DIR "${KEYFALL_ROOT}/plugin/core")

add_library(keyfall_core STATIC "${CORE_DIR}/Json.cpp" "${CORE_DIR}/KeyboardLayout.cpp" "${CORE_DIR}/InputResolver.cpp"
	"${CORE_DIR}/Chart.cpp" "${CORE_DIR}/PlayheadClock.cpp")
target_include_directories(keyfall_core PUBLIC "${CORE_DIR}")

add_executable(core_tests core_tests.cpp)
target_link_libraries(core_tests PRIVATE keyfall_core)

add_executable(kflayout-lint "${KEYFALL_ROOT}/tools/kflayout-lint.cpp")
target_link_libraries(kflayout-lint PRIVATE keyfall_core)

enable_testing()
add_test(NAME core_tests COMMAND core_tests)
add_test(NAME layout_corpus COMMAND kflayout-lint --corpus "${CMAKE_CURRENT_SOURCE_DIR}/layouts")
file(GLOB BUILTIN_LAYOUTS "${KEYFALL_ROOT}/plugin/layouts/*.kflayout.json")
add_test(NAME builtin_layouts COMMAND kflayout-lint --strict ${BUILTIN_LAYOUTS})

# The Python validator for layout authors must agree with the C++ loader on the same corpus
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
	add_test(NAME layout_corpus_python
		COMMAND Python3::Interpreter -I "${KEYFALL_ROOT}/scripts/validate_layouts.py" --corpus "${CMAKE_CURRENT_SOURCE_DIR}/layouts")
	add_test(NAME builtin_layouts_python
		COMMAND Python3::Interpreter -I "${KEYFALL_ROOT}/scripts/validate_layouts.py" --strict ${BUILTIN_LAYOUTS})
endif()
