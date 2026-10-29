#!/bin/sh
# Build helper for LMMS + KeyFall on Linux (GCC + Qt 5 from the distro).
# Usage: scripts/lmms.sh deps | fetch | configure | build | run | test
#   deps       install build dependencies (Ubuntu/Debian, asks for sudo)
#   fetch      clone LMMS at the pinned commit into $LMMS_SRC (once)
#   configure  link the plugin into LMMS and run CMake
#   build      build LMMS + KeyFall
#   run        start the built LMMS with a separate dev config
#   test       KeyFall core tests (no LMMS, no Qt)
set -e

# LMMS commit KeyFall is developed against (see docs/BUILD.md)
LMMS_COMMIT=bed447baab187ea6292449048e0b1aab16082e3b

ROOT=$(cd "$(dirname "$0")/.." && pwd)
LMMS_SRC=${LMMS_SRC:-$HOME/Projects/lmms}
LMMS_BUILD=${LMMS_BUILD:-$LMMS_SRC/build}
JOBS=${JOBS:-$(nproc)}
# Not the Windows dev config: under WSL both scripts share this dev/ folder
DEV_CONFIG=$ROOT/dev/lmmsrc-dev-linux.xml

generator() {
	if command -v ninja >/dev/null 2>&1; then echo "-G Ninja"; fi
}

case "$1" in
deps)
	sudo sh "$ROOT/scripts/linux-deps.sh"
	;;
fetch)
	if [ -d "$LMMS_SRC/.git" ]; then
		echo "LMMS is already at $LMMS_SRC"
		exit 0
	fi
	git clone https://github.com/LMMS/lmms.git "$LMMS_SRC"
	git -C "$LMMS_SRC" checkout -b keyfall "$LMMS_COMMIT"
	git -C "$LMMS_SRC" submodule update --init --recursive
	;;
configure)
	[ -d "$LMMS_SRC/.git" ] || { echo "No LMMS at $LMMS_SRC: run '$0 fetch' or set LMMS_SRC"; exit 1; }
	# The plugin source stays in this repository; LMMS sees it as plugins/KeyFall
	if [ ! -e "$LMMS_SRC/plugins/KeyFall" ]; then
		ln -s "$ROOT/plugin" "$LMMS_SRC/plugins/KeyFall"
		grep -qx "plugins/KeyFall" "$LMMS_SRC/.git/info/exclude" 2>/dev/null \
			|| echo "plugins/KeyFall" >> "$LMMS_SRC/.git/info/exclude"
	fi
	# Minimal plug-in set keeps rebuilds fast; drop LMMS_MINIMAL/PLUGIN_LIST for a full build
	# shellcheck disable=SC2046
	cmake -S "$LMMS_SRC" -B "$LMMS_BUILD" $(generator) \
		-DCMAKE_BUILD_TYPE=RelWithDebInfo \
		-DLMMS_MINIMAL=ON \
		-DPLUGIN_LIST="KeyFall TapTempo MidiImport"
	;;
build)
	cmake --build "$LMMS_BUILD" -j"$JOBS"
	;;
run)
	# Separate config so the dev build never touches an installed LMMS's ~/.lmmsrc.xml
	if [ ! -f "$DEV_CONFIG" ]; then
		cat > "$DEV_CONFIG" <<EOF
<?xml version="1.0"?>
<!DOCTYPE lmms-config-file>
<lmms configversion="3" version="1.3.0">
  <app configured="1" nomsgaftersetup="1" openlastproject="0" language="en"/>
  <audioengine audiodev="SDL (Simple DirectMedia Layer)" mididev="ALSA-Sequencer (Advanced Linux Sound Architecture)" framesperaudiobuffer="256" hqaudio="0"/>
  <paths workingdir="$HOME/Documents/lmms/" stkdir="data:/stk/rawwaves/" artwork="data:/themes/default/" theme="data:/themes/default/"/>
  <ui enableautosave="0" disableautoquit="1"/>
</lmms>
EOF
	fi
	shift
	cd "$LMMS_BUILD" && exec ./lmms --config "$DEV_CONFIG" "$@"
	;;
test)
	# shellcheck disable=SC2046
	cmake -S "$ROOT/tests" -B "$ROOT/build-tests-linux" $(generator) -DCMAKE_BUILD_TYPE=Debug
	cmake --build "$ROOT/build-tests-linux" -j"$JOBS"
	ctest --test-dir "$ROOT/build-tests-linux" --output-on-failure
	;;
*)
	echo "Usage: $0 deps | fetch | configure | build | run [project.mmp] | test"
	exit 1
	;;
esac
