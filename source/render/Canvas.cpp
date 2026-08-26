#include "Canvas.h"

#include "GLState.h"
#include "Shaders.h"

#include "../Diag.h"

#include <algorithm>
#include <vector>

namespace spasis
{
namespace
{
} // namespace

bool Canvas::initGL()
{
	// Each step reports itself. "InitGL failed" on its own costs an hour every
	// time, because half a dozen unrelated things can cause it and the host
	// shows none of them.
	if( !buildShaders() )
	{
		diag::error( "shader programs would not compile or link" );
		return false;
	}

	if( !quad_.Initialise() )
	{
		diag::error( "screen quad would not initialise" );
		return false;
	}

	glGenVertexArrays( 1, &vao_ );
	glGenBuffers( 1, &vbo_ );
	if( vao_ == 0 || vbo_ == 0 )
	{
		diag::error( "could not create the vertex array or buffer" );
		return false;
	}

	glBindVertexArray( vao_ );
	glBindBuffer( GL_ARRAY_BUFFER, vbo_ );
	glEnableVertexAttribArray( 0 );
	glVertexAttribPointer( 0, 2, GL_FLOAT, GL_FALSE, sizeof( Vertex ), (void*)offsetof( Vertex, x ) );
	glEnableVertexAttribArray( 1 );
	glVertexAttribPointer( 1, 1, GL_FLOAT, GL_FALSE, sizeof( Vertex ), (void*)offsetof( Vertex, intensity ) );
	glEnableVertexAttribArray( 2 );
	glVertexAttribPointer( 2, 1, GL_FLOAT, GL_FALSE, sizeof( Vertex ), (void*)offsetof( Vertex, size ) );
	glBindVertexArray( 0 );
	glBindBuffer( GL_ARRAY_BUFFER, 0 );

	clearPending_ = true;
	return true;
}

bool Canvas::buildShaders()
{
	if( !traceShader_.Compile( shaders::kTraceVert, shaders::kTraceFrag ) )
	{
		diag::error( "trace shader failed -- run sptest --shaders for the driver's message" );
		return false;
	}
	if( !compositeShader_.Compile( shaders::kCompositeVert, shaders::kCompositeFrag ) )
	{
		diag::error( "composite shader failed -- run sptest --shaders" );
		return false;
	}
	if( !decayShader_.Compile( shaders::kCompositeVert, shaders::kDecayFrag ) )
	{
		diag::error( "decay shader failed -- run sptest --shaders" );
		return false;
	}
	return true;
}

void Canvas::deInitGL()
{
	traceShader_.FreeGLResources();
	compositeShader_.FreeGLResources();
	decayShader_.FreeGLResources();
	quad_.Release();
	accum_[ 0 ].Destroy();
	accum_[ 1 ].Destroy();
	if( vbo_ != 0 )
		glDeleteBuffers( 1, &vbo_ );
	if( vao_ != 0 )
		glDeleteVertexArrays( 1, &vao_ );
	vbo_         = 0;
	vao_         = 0;
	vboCapacity_ = 0;
}

void Canvas::render( const Mesh& mesh, const ViewParams& view, GLuint destFBO,
					 GLsizei width, GLsizei height, float decay,
					 const float foreground[ 4 ], const float background[ 4 ] )
{
	if( width <= 0 || height <= 0 )
		return;

	// The accumulator is single-channel float. RGB would be three times the
	// bandwidth for three copies of the same number: colour is applied once, in
	// the composite, so that changing it does not mean waiting for the
	// persistence to refill.
	const bool resized = !accum_[ 0 ].Ensure( width, height, GL_R16F ) ||
						 !accum_[ 1 ].Ensure( width, height, GL_R16F );
	if( !accum_[ 0 ].IsValid() || !accum_[ 1 ].IsValid() )
		return;
	if( resized )
		clearPending_ = true;

	const int src = current_;
	const int dst = 1 - current_;

	//-- Decay the history into the other buffer ----------------------------
	glBindFramebuffer( GL_FRAMEBUFFER, accum_[ dst ].GetGLID() );
	glViewport( 0, 0, width, height );

	if( clearPending_ )
	{
		glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
		glClear( GL_COLOR_BUFFER_BIT );
		clearPending_ = false;
	}
	else
	{
		glDisable( GL_BLEND );
		ffglex::ScopedShaderBinding shaderBinding( decayShader_.GetGLID() );
		ffglex::ScopedSamplerActivation samplerActivation( 0 );
		ffglex::Scoped2DTextureBinding textureBinding( accum_[ src ].TextureID() );
		decayShader_.Set( "accumTexture", 0 );
		decayShader_.Set( "decay", decay );
		quad_.Draw();
	}

	//-- Draw the mesh additively on top ------------------------------------
	if( !mesh.vertices.empty() )
	{
		glEnable( GL_BLEND );
		glBlendFunc( GL_ONE, GL_ONE );
		glEnable( GL_PROGRAM_POINT_SIZE );

		glBindVertexArray( vao_ );
		glBindBuffer( GL_ARRAY_BUFFER, vbo_ );

		const size_t bytes = mesh.vertices.size() * sizeof( Vertex );
		if( bytes > vboCapacity_ )
		{
			// Grow only. The vertex count swings with the style (a particle
			// build is twenty times a line build) and reallocating both ways
			// would orphan a buffer every time the operator touched the
			// control.
			glBufferData( GL_ARRAY_BUFFER, bytes, mesh.vertices.data(), GL_STREAM_DRAW );
			vboCapacity_ = bytes;
		}
		else
		{
			glBufferSubData( GL_ARRAY_BUFFER, 0, bytes, mesh.vertices.data() );
		}

		ffglex::ScopedShaderBinding shaderBinding( traceShader_.GetGLID() );
		GLenum mode = GL_POINTS;
		if( mesh.primitive == Primitive::LineStrip )
			mode = GL_LINE_STRIP;
		else if( mesh.primitive == Primitive::Triangles )
			mode = GL_TRIANGLES;
		traceShader_.Set( "pointMode", ( mode == GL_POINTS ) ? 1.0f : 0.0f );
		glDrawArrays( mode, 0, (GLsizei)mesh.vertices.size() );

		glBindVertexArray( 0 );
		glBindBuffer( GL_ARRAY_BUFFER, 0 );
		glDisable( GL_PROGRAM_POINT_SIZE );
	}

	current_ = dst;

	//-- Composite to the output --------------------------------------------
	glBindFramebuffer( GL_FRAMEBUFFER, destFBO );
	glViewport( 0, 0, width, height );
	glDisable( GL_BLEND );

	ffglex::ScopedShaderBinding shaderBinding( compositeShader_.GetGLID() );
	ffglex::ScopedSamplerActivation samplerActivation( 0 );
	ffglex::Scoped2DTextureBinding textureBinding( accum_[ current_ ].TextureID() );
	compositeShader_.Set( "accumTexture", 0 );
	compositeShader_.Set( "foreground", foreground[ 0 ], foreground[ 1 ], foreground[ 2 ], foreground[ 3 ] );
	compositeShader_.Set( "background", background[ 0 ], background[ 1 ], background[ 2 ], background[ 3 ] );
	quad_.Draw();
}

} // namespace spasis
