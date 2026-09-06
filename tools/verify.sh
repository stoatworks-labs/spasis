#!/usr/bin/env bash
#
# Everything that can be checked locally, including the parts the release job
# does -- a check that only ever runs in CI, after a tag, is a check that will
# catch you after the tag.
set -euo pipefail

cd "$( dirname "$0" )/.."
BUILD="${BUILD_DIR:-build}"

echo
echo "== shaders =="
# The static half, glslc, which needs no GPU. The driver half is part of sptest
# below. Both run here; CI can only run this one.
tools/check-shaders.sh

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

echo
echo "== movie mode =="
# The video pipeline's only entry point, smoke-tested by byte count rather than
# by eye. A --movie that silently emits nothing still lets ffmpeg "succeed" with
# a zero-length clip, and the first sign would be a black shot in a cut video.
if [[ "$( uname -s )" == "Darwin" ]]; then
	movie_bytes="$( "./$BUILD/sptest" --movie field-circle-scope 0.2 2 320 180 0.5 \
		2>/dev/null | wc -c | tr -d ' ' )"
	expect=$(( 12 * 320 * 180 * 4 ))
	if [[ "$movie_bytes" == "$expect" ]]; then
		printf '   12 frames, %s bytes, exact\n' "$movie_bytes"
	else
		printf '   expected %s bytes, got %s\n' "$expect" "$movie_bytes"
		exit 1
	fi
	# An unknown shot must FAIL rather than emit a default, or a typo in a shot
	# name in render.py becomes a video of the wrong display.
	if "./$BUILD/sptest" --movie not-a-shot 0.1 >/dev/null 2>&1; then
		echo "   an unknown shot was accepted"
		exit 1
	fi
	printf '   an unknown shot is refused\n'
fi

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
