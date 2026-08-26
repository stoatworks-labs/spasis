#pragma once

#include "ScopeBuffer.h"
#include "View.h"

#include <FFGLSDK.h>

namespace spasis
{
/**
	The GL side: a persistence buffer and two shaders.

	Every display in the suite is drawn the same way -- vertices in, additive
	accumulation, decay, then one colour ramp on the way out. The persistence is
	not decoration. A goniometer sampled at 60 fps and drawn without it shows
	one 40 ms slice of a signal and flickers; every hardware instrument the
	reference images come from has a phosphor or a deliberate release, and
	leaving it out is the difference between "instrument" and "strobe".
*/
class Canvas
{
public:
	bool initGL();
	void deInitGL();

	/// Draw one frame. Assumes the caller has saved GL state and will restore
	/// it; see `GLState.h` for why that is not done in here.
	/**
		`graticule` is drawn straight to the output AFTER the composite, not
		into the persistence buffer.

		That is the whole reason it is a separate mesh. The accumulator is
		additive with a decay, so anything static drawn into it converges on
		`value / (1 - decay)` -- markings that start as a hint and end up
		brighter than the trace, and brighter still the longer the persistence
		is set. Drawing them over the top instead keeps them at exactly the
		intensity asked for, whatever else is happening.
	*/
	void render( const Mesh& mesh, const Mesh& graticule, const ViewParams& view, GLuint destFBO,
				 GLsizei width, GLsizei height, float decay,
				 const float foreground[ 4 ], const float background[ 4 ] );

	/// Drop the persistence. Called when something changes that would make the
	/// history a lie -- a new device, a different channel count, a geometry
	/// change that moves every vertex somewhere else.
	void clearHistory() { clearPending_ = true; }

private:
	bool buildShaders();
	void drawMesh( const Mesh& mesh );

	ffglex::FFGLShader traceShader_;
	ffglex::FFGLShader compositeShader_;

	// A member, not a file-scope static. A static shader is shared by every
	// instance in the process and freed by whichever one is destroyed first,
	// leaving the others drawing with a deleted program.
	ffglex::FFGLShader decayShader_;
	ffglex::FFGLScreenQuad quad_;

	ScopeBuffer accum_[ 2 ];
	int         current_      = 0;
	bool        clearPending_ = true;
	GLuint      vao_          = 0;
	GLuint      vbo_          = 0;
	size_t      vboCapacity_  = 0;
};

} // namespace spasis
