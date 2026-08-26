#!/usr/bin/env bash
#
# Everything that can be checked locally, including the parts the release job
# does -- a check that only ever runs in CI, after a tag, is a check that will
# catch you after the tag.
set -euo pipefail

cd "$( dirname "$0" )/.."
BUILD="${BUILD_DIR:-build}"

echo "== configure and build =="
cmake -B "$BUILD" -DCMAKE_BUILD_TYPE=Release > /dev/null
# Deliberately NOT piped into head/grep: a reader that exits early SIGPIPEs the
# build and leaves a stale bundle reporting success.
cmake --build "$BUILD" -j"$( getconf _NPROCESSORS_ONLN )"

echo
echo "== invariants =="
"./$BUILD/sptest"

echo
echo "== contact sheet =="
mkdir -p docs/sheet
"./$BUILD/sptest" --sheet docs/sheet 2

if [[ "$( uname -s )" == "Darwin" ]]; then
	echo
	echo "== universal binaries =="
	for bundle in "$BUILD"/*.bundle; do
		name="$( basename "$bundle" .bundle )"
		archs="$( lipo -archs "$bundle/Contents/MacOS/$name" )"
		printf '%-18s %s\n' "$name" "$archs"
		# lipo, never the build log: CMAKE_OSX_ARCHITECTURES set after the first
		# target exists is silently ignored and the log says nothing.
		[[ "$archs" == *arm64* && "$archs" == *x86_64* ]] \
			|| { echo "  NOT universal"; exit 1; }
	done

	echo
	echo "== CFBundleExecutable matches the binary on disk =="
	for bundle in "$BUILD"/*.bundle; do
		name="$( basename "$bundle" .bundle )"
		declared="$( /usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' \
			"$bundle/Contents/Info.plist" )"
		[[ -f "$bundle/Contents/MacOS/$declared" ]] \
			|| { echo "  $name declares $declared, which is not there"; exit 1; }
		printf '%-18s %s\n' "$name" "$declared"
	done

	SWEEP=../resolume-ofx-bridge/build/ffgltest
	if [[ -x "$SWEEP" ]]; then
		echo
		echo "== instantiate sweep =="
		for bundle in "$BUILD"/*.bundle; do
			printf '%-18s ' "$( basename "$bundle" .bundle )"
			"$SWEEP" "$bundle" 2>&1 | tail -1
		done
	fi
fi

echo
echo "all good"
