#!/usr/bin/env bash
#
# Every shader, through a real GLSL compiler, before a host has to find out.
#
# A shader that will not compile presents to an operator as "the effect does
# nothing", with the real message buried in the diagnostics log -- so without
# this it is caught at run time, in a host, or not at all.
#
# This is the STATIC half of the shader checking and it needs no GPU. The other
# half is `sptest --shaders`, which compiles the same sources through whatever
# driver is actually present. Neither replaces the other: glslc catches the
# syntax and the linkage on any machine, and only the driver can tell you that
# Apple's Metal GL disagrees with it. A GitHub macOS runner cannot create an
# accelerated 4.1 core context at all, which is precisely why this half exists
# as its own script rather than living inside the harness.
#
#   tools/check-shaders.sh              skip (0) when glslc is not installed
#   tools/check-shaders.sh --require    fail (1) when glslc is not installed
#
# --target-env=opengl4.5 with -fauto-map-locations: glslc targets SPIR-V, which
# demands an explicit layout( location ) on every uniform and varying. Those are
# Vulkan rules and not GLSL ones, and without the flag every shader "fails" for
# reasons that have nothing to do with the code.
set -euo pipefail

cd "$( dirname "$0" )/.."

require=0
[ "${1:-}" = "--require" ] && require=1

if ! command -v glslc >/dev/null 2>&1; then
	if [ "$require" -eq 1 ]; then
		printf '   glslc is not installed and --require was given\n'
		printf '   brew install shaderc\n'
		exit 1
	fi
	printf '   skipped: glslc not installed (brew install shaderc)\n'
	exit 0
fi

dir="$( mktemp -d )"
trap 'rm -rf "$dir"' EXIT

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

bad=0
n=0
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
	# No shaders at all is a FAILURE, not a pass. It means the extraction above
	# has lost track of where this repo keeps its GLSL, and a check that
	# silently looks at nothing is worse than no check.
	printf '   no shaders were extracted -- the extraction has gone stale\n'
	exit 1
fi

if [ "$bad" -eq 0 ]; then
	printf '   %d shaders, all compile\n' "$n"
fi
exit "$bad"
