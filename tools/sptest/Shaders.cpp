/**
	Compile every shader in a headless context and print what the driver says.

	This exists because FFGLShader::Compile returns a bool and sends the driver's
	log to the host, where the operator never sees it -- so a one-character typo
	in GLSL reaches them as "the plugin draws nothing" and reaches the developer
	as a bundle that loads, exports plugMain, and fails to instantiate with no
	further explanation.
*/
#include "../../source/render/Shaders.h"

#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>

#include <cstdio>
#include <string>
#include <vector>

namespace
{
int failures = 0;

bool compileOne( const char* label, GLenum stage, const char* source )
{
	const GLuint id = glCreateShader( stage );
	glShaderSource( id, 1, &source, nullptr );
	glCompileShader( id );

	GLint ok = GL_FALSE;
	glGetShaderiv( id, GL_COMPILE_STATUS, &ok );
	if( ok != GL_TRUE )
	{
		GLint length = 0;
		glGetShaderiv( id, GL_INFO_LOG_LENGTH, &length );
		std::vector< char > log( ( length > 1 ) ? length : 1, '\0' );
		glGetShaderInfoLog( id, (GLsizei)log.size(), nullptr, log.data() );
		std::printf( "  FAIL  %s\n%s\n", label, log.data() );
		++failures;
	}
	else
	{
		std::printf( "  ok    %s\n", label );
	}
	glDeleteShader( id );
	return ok == GL_TRUE;
}
} // namespace

int checkShaders()
{
	CGLPixelFormatAttribute attributes[] = {
		kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core,
		kCGLPFAAccelerated,
		(CGLPixelFormatAttribute)0
	};
	CGLPixelFormatObj pixelFormat = nullptr;
	GLint             count       = 0;
	if( CGLChoosePixelFormat( attributes, &pixelFormat, &count ) != kCGLNoError || pixelFormat == nullptr )
	{
		std::puts( "  could not create a 4.1 core context" );
		return 1;
	}
	CGLContextObj context = nullptr;
	CGLCreateContext( pixelFormat, nullptr, &context );
	CGLDestroyPixelFormat( pixelFormat );
	if( context == nullptr )
	{
		std::puts( "  could not create a 4.1 core context" );
		return 1;
	}
	CGLSetCurrentContext( context );

	std::printf( "GL %s\n", (const char*)glGetString( GL_VERSION ) );

	using namespace spasis::shaders;
	compileOne( "trace vertex", GL_VERTEX_SHADER, kTraceVert );
	compileOne( "trace fragment", GL_FRAGMENT_SHADER, kTraceFrag );
	compileOne( "composite vertex", GL_VERTEX_SHADER, kCompositeVert );
	compileOne( "composite fragment", GL_FRAGMENT_SHADER, kCompositeFrag );
	compileOne( "decay fragment", GL_FRAGMENT_SHADER, kDecayFrag );

	CGLSetCurrentContext( nullptr );
	CGLDestroyContext( context );
	return failures;
}
