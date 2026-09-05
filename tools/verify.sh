#!/usr/bin/env bash
#
# Everything that can be checked locally, including the parts the release job
# does -- a check that only ever runs in CI, after a tag, is a check that will
# catch you after the tag.
set -euo pipefail

cd "$( dirname "$0" )/.."
BUILD="${BUILD_DIR:-build}"

#---------------------------------------------------------------------------
# Every shader, through a real GLSL compiler, before a host has to find out.
#
# A shader that will not compile presents to an operator as "the effect does
# nothing", with the real message buried in the diagnostics log -- so without
# this it is caught at run time, in a host, or not at all.
#
# --target-env=opengl4.5 with -fauto-map-locations: glslc targets SPIR-V, which
# demands an explicit layout( location ) on every uniform and varying. Those are
# Vulkan rules and not GLSL ones, and without the flag every shader "fails" for
# reasons that have nothing to do with the code.
#
# glslc is optional -- `brew install shaderc` -- so a machine without it skips
# rather than fails.
#---------------------------------------------------------------------------
shaders_compile() {
	local dir bad=0 n=0 shader

	if ! command -v glslc >/dev/null 2>&1; then
		printf '   skipped: glslc not installed (brew install shaderc)\n'
		return 0
	fi

	dir="$( mktemp -d )"

	python3 - "$dir" <<'SHADERS_PY'
import re, sys, pathlib
out = pathlib.Path( sys.argv[ 1 ] )

# Where this repo keeps its GLSL.
FILES = [
	"source/render/Shaders.h",
]

named, unnamed = {}, []
for f in FILES:
	text = pathlib.Path( f ).read_text()
	for m in re.finditer( r'(?:(\w+)\s*(?:\[\s*\])?\s*=\s*)?R"\((.*?)\)"', text, re.S ):
		if m.group( 1 ): named[ m.group( 1 ) ] = m.group( 2 )
		else:            unnamed.append( m.group( 2 ) )
	for m in re.finditer( r'(\w+)\s*=\s*((?:"(?:[^"\\\n]|\\.)*"\s*)+);', text ):
		named.setdefault( m.group( 1 ), "".join(
			s.encode().decode( "unicode_escape" )
			for s in re.findall( r'"((?:[^"\\\n]|\\.)*)"', m.group( 2 ) ) ) )

def emit( name, body ):
	# The vertex shader is the one that writes gl_Position; everything else is a
	# fragment shader. glslc takes the stage from the extension.
	ext = ".vert" if re.search( r"\bgl_Position\s*=", body ) else ".frag"
	( out / ( name + ext ) ).write_text( body )

for name, body in named.items():
	if body.lstrip().startswith( "#version" ) and "void main" in body:
		emit( name, body )
SHADERS_PY

	for shader in "$dir"/*.vert "$dir"/*.frag; do
		[ -e "$shader" ] || continue
		n=$(( n + 1 ))
		if ! glslc --target-env=opengl4.5 -fauto-map-locations \
			   "$shader" -o /dev/null 2>"$dir/err"; then
			printf '   %s does not compile\n' "$( basename "$shader" )"
			sed "s|$dir/||; s|^|      |" "$dir/err"
			bad=$(( bad + 1 ))
		fi
	done

	if [ "$n" -eq 0 ]; then
		# No shaders at all is a FAILURE, not a pass. It means the extraction
		# above has lost track of where this repo keeps its GLSL, and a check
		# that silently looks at nothing is worse than no check.
		printf '   no shaders were extracted -- the extraction has gone stale\n'
		rm -rf "$dir"
		return 1
	fi

	if [ "$bad" -eq 0 ]; then
		printf '   %d shaders, all compile\n' "$n"
	fi
	rm -rf "$dir"
	return "$bad"
}

echo
echo "== shaders =="
shaders_compile

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
