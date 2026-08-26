#pragma once

/**
	Every shader in the suite, in one file.

	They live here rather than inside Canvas.cpp so that `sptest --shaders` can
	compile them in a headless context and print the driver's error. That
	matters more than it looks: FFGLShader::Compile sends its log to the host
	and returns a bool, so a shader that will not compile reaches the operator
	as "the plugin draws nothing" with the reason nowhere they can see it.

	`sample`, `input`, `output`, `filter`, `common` and `active` are GLSL
	reserved words, and every identifier below has been checked against that
	list.
*/
namespace spasis::shaders
{
inline const char* const kTraceVert = R"(#version 410 core
layout( location = 0 ) in vec2 vPosition;
layout( location = 1 ) in float vIntensity;
layout( location = 2 ) in float vSize;
out float fIntensity;
void main()
{
	fIntensity = vIntensity;
	gl_PointSize = vSize;
	gl_Position = vec4( vPosition, 0.0, 1.0 );
}
)";

inline const char* const kTraceFrag = R"(#version 410 core
uniform float pointMode;
in float fIntensity;
out vec4 fragColour;
void main()
{
	// Points are drawn as soft discs rather than squares, which needs
	// gl_PointCoord -- and gl_PointCoord is UNDEFINED for every other
	// primitive. It is tempting to use it unconditionally and let the fixed
	// value it takes for lines and triangles fall somewhere harmless; on Apple's
	// Metal GL that value is (0,0), which is the far corner of the disc, so the
	// falloff evaluates to zero and every line and every bar in the suite
	// renders pure black. The uniform is cheaper than that afternoon.
	float falloff = 1.0;
	if( pointMode > 0.5 )
	{
		vec2 d = gl_PointCoord - vec2( 0.5 );
		falloff = clamp( 1.0 - dot( d, d ) * 4.0, 0.0, 1.0 );
	}
	fragColour = vec4( vec3( fIntensity * falloff ), 1.0 );
}
)";

inline const char* const kCompositeVert = R"(#version 410 core
layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;
out vec2 uv;
void main()
{
	uv = vUV;
	gl_Position = vPosition;
}
)";

inline const char* const kCompositeFrag = R"(#version 410 core
uniform sampler2D accumTexture;
uniform vec4 foreground;
uniform vec4 background;
in vec2 uv;
out vec4 fragColour;
void main()
{
	float energy = texture( accumTexture, uv ).r;

	// The accumulator holds unbounded additive energy. A hard clamp turns every
	// busy passage into a flat white shape, which throws away exactly the
	// dynamic range the instrument exists to show, so this is a soft knee that
	// approaches 1 without reaching it.
	float level = 1.0 - exp( -energy * 2.0 );

	vec3 rgb = mix( background.rgb, foreground.rgb, level );
	float a = mix( background.a, foreground.a, level );

	// Premultiplied: Resolume composites premultiplied alpha, and returning
	// straight alpha makes every edge in the picture darker than it should be
	// against anything but black.
	fragColour = vec4( rgb * a, a );
}
)";

inline const char* const kDecayFrag = R"(#version 410 core
uniform sampler2D accumTexture;
uniform float decay;
in vec2 uv;
out vec4 fragColour;
void main()
{
	fragColour = texture( accumTexture, uv ) * decay;
}
)";

} // namespace spasis::shaders
