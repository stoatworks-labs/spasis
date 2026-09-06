/**
	Offline rendering: drive the shipping render path in a headless GL context
	and write PNGs.

	The invariants in Invariants.cpp check the numbers. This checks the picture,
	which is the other half and cannot be asserted about -- so it produces files
	a human looks at, and it produces them from the same Analyser, the same
	Builder and the same Canvas the four bundles link.

	The PNG writer and the CGL context setup are lifted from vectrix's `vxtest`,
	which lifted the context from resolume-scopes. zlib ships with the OS, so a
	PNG is a few chunk headers and a CRC rather than a dependency.
*/
#include "../../source/analysis/Analyser.h"
#include "../../source/audio/Ring.h"
#include "../../source/render/Builder.h"
#include "../../source/render/Canvas.h"

#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <zlib.h>

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

using namespace spasis;

namespace
{
constexpr float kPi = 3.14159265358979323846f;

void putU32( std::vector< unsigned char >& out, uint32_t value )
{
	out.push_back( (unsigned char)( value >> 24 ) );
	out.push_back( (unsigned char)( value >> 16 ) );
	out.push_back( (unsigned char)( value >> 8 ) );
	out.push_back( (unsigned char)value );
}

void putChunk( std::vector< unsigned char >& out, const char* type, const std::vector< unsigned char >& data )
{
	putU32( out, (uint32_t)data.size() );
	const size_t start = out.size();
	out.insert( out.end(), type, type + 4 );
	out.insert( out.end(), data.begin(), data.end() );
	uLong crc = crc32( 0L, Z_NULL, 0 );
	crc       = crc32( crc, out.data() + start, (uInt)( 4 + data.size() ) );
	putU32( out, (uint32_t)crc );
}

bool writePng( const std::string& path, int width, int height, const std::vector< unsigned char >& rgba )
{
	std::vector< unsigned char > raw;
	for( int y = 0; y < height; ++y )
	{
		raw.push_back( 0 );   // filter: none
		const unsigned char* row = rgba.data() + (size_t)y * width * 4;
		raw.insert( raw.end(), row, row + (size_t)width * 4 );
	}

	uLongf                       size = compressBound( (uLong)raw.size() );
	std::vector< unsigned char > compressed( size );
	if( compress2( compressed.data(), &size, raw.data(), (uLong)raw.size(), 6 ) != Z_OK )
		return false;
	compressed.resize( size );

	std::vector< unsigned char > png = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
	std::vector< unsigned char > header;
	putU32( header, (uint32_t)width );
	putU32( header, (uint32_t)height );
	header.push_back( 8 );
	header.push_back( 6 );
	header.push_back( 0 );
	header.push_back( 0 );
	header.push_back( 0 );
	putChunk( png, "IHDR", header );
	putChunk( png, "IDAT", compressed );
	putChunk( png, "IEND", {} );

	std::FILE* file = std::fopen( path.c_str(), "wb" );
	if( file == nullptr )
		return false;
	const size_t written = std::fwrite( png.data(), 1, png.size(), file );
	std::fclose( file );
	return written == png.size();
}

CGLContextObj createContext()
{
	const CGLPixelFormatAttribute attributes[] = {
		kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core,
		kCGLPFAAccelerated,
		kCGLPFAColorSize, (CGLPixelFormatAttribute)24,
		kCGLPFAAlphaSize, (CGLPixelFormatAttribute)8,
		(CGLPixelFormatAttribute)0
	};
	CGLPixelFormatObj format = nullptr;
	GLint             count  = 0;
	if( CGLChoosePixelFormat( attributes, &format, &count ) != kCGLNoError || format == nullptr )
		return nullptr;
	CGLContextObj context = nullptr;
	const CGLError error  = CGLCreateContext( format, nullptr, &context );
	CGLDestroyPixelFormat( format );
	if( error != kCGLNoError )
		return nullptr;
	CGLSetCurrentContext( context );
	return context;
}

/**
	A stereo programme that exercises every display at once.

	Bass centred and correlated, a pair of mid tones panned apart, and a top end
	that is deliberately DE-CORRELATED -- which is what a real mix does and what
	makes the width plot show a rising curve rather than a flat line. A test
	signal of one correlated tone would draw four perfectly plausible pictures
	and prove nothing about any of them.
*/
void makeProgramme( std::vector< float >& out, int frames, int channels, int sampleRate, double t0 )
{
	out.assign( (size_t)frames * channels, 0.0f );
	for( int i = 0; i < frames; ++i )
	{
		const double t = t0 + (double)i / sampleRate;

		const float bass = 0.55f * (float)std::sin( 2.0 * kPi * 70.0 * t );
		const float midL = 0.25f * (float)std::sin( 2.0 * kPi * 640.0 * t );
		const float midR = 0.25f * (float)std::sin( 2.0 * kPi * 970.0 * t + 1.1 );
		// Two high tones a hair apart in frequency drift through every phase
		// relationship, which is a decorrelated top end without needing noise.
		const float airL = 0.16f * (float)std::sin( 2.0 * kPi * 6300.0 * t );
		const float airR = 0.16f * (float)std::sin( 2.0 * kPi * 6480.0 * t );

		const float l = bass + midL * 1.2f + midR * 0.4f + airL;
		const float r = bass + midL * 0.4f + midR * 1.2f + airR;

		for( int c = 0; c < channels; ++c )
		{
			// Beyond stereo, wrap the two signals round the ring at decreasing
			// level so a surround layout has something in every speaker.
			const float base = ( c % 2 == 0 ) ? l : r;
			out[ (size_t)i * channels + c ] = base * ( 1.0f / ( 1.0f + 0.6f * ( c / 2 ) ) );
		}
	}
}

/**
	The shot list, shared by the contact sheet and the movie renderer.

	One table, so a shot name given to --movie is guaranteed to name a display
	configuration the sheet also renders and checks. A second copy would let the
	video drift onto a combination nothing verifies.
*/
struct Job
{
	const char* name;
	Display     display;
	Geometry    geometry;
	Style       style;
};

const Job kJobs[] = {
	{ "field-circle-scope", Display::Field, Geometry::Circular, Style::Oscilloscope },
	{ "field-half-particle", Display::Field, Geometry::SemiCircular, Style::Particle },
	{ "field-raster-scope", Display::Field, Geometry::Raster, Style::Oscilloscope },
	{ "rose-circle-scope", Display::Rose, Geometry::Circular, Style::Oscilloscope },
	{ "rose-circle-bars", Display::Rose, Geometry::Circular, Style::Bargraph },
	{ "rose-half-bars", Display::Rose, Geometry::SemiCircular, Style::Bargraph },
	{ "width-raster-scope", Display::Width, Geometry::Raster, Style::Oscilloscope },
	{ "width-circle-bars", Display::Width, Geometry::Circular, Style::Bargraph },
	{ "balance-raster-bars", Display::Balance, Geometry::Raster, Style::Bargraph },
	{ "balance-circle-particle", Display::Balance, Geometry::Circular, Style::Particle },
	{ "balance-half-scope", Display::Balance, Geometry::SemiCircular, Style::Oscilloscope },
	{ "speakers-circle", Display::Rose, Geometry::Circular, Style::Bargraph },
};

/// Build the mesh this job asks for. Shared so the sheet and the movie cannot
/// disagree about what "rose-circle-bars" draws.
void buildFor( const Job& job, const Frame& frame, const ViewParams& view, Mesh& mesh )
{
	switch( job.display )
	{
	case Display::Field: buildField( frame, view, mesh ); break;
	case Display::Rose:
		if( std::string( job.name ).rfind( "speakers", 0 ) == 0 )
			buildSpeakers( frame, view, mesh );
		else
			buildRose( frame, view, mesh );
		break;
	case Display::Width:   buildWidth( frame, view, mesh ); break;
	case Display::Balance: buildBalance( frame, view, mesh ); break;
	}
}

struct Target
{
	GLuint texture = 0;
	GLuint fbo     = 0;
};

Target makeTarget( int width, int height )
{
	Target target;
	glGenTextures( 1, &target.texture );
	glBindTexture( GL_TEXTURE_2D, target.texture );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glGenFramebuffers( 1, &target.fbo );
	glBindFramebuffer( GL_FRAMEBUFFER, target.fbo );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.texture, 0 );
	glBindTexture( GL_TEXTURE_2D, 0 );
	return target;
}
} // namespace

int renderSheet( const std::string& directory, int channels )
{
	CGLContextObj context = createContext();
	if( context == nullptr )
	{
		std::puts( "  could not create a 4.1 core context" );
		return 1;
	}

	constexpr int kWidth      = 640;
	constexpr int kHeight     = 360;
	constexpr int kSampleRate = 48000;
	constexpr int kFrames     = 90;   // 1.5 s at 60 fps, so the persistence fills

	Target target = makeTarget( kWidth, kHeight );

	Canvas canvas;
	if( !canvas.initGL() )
	{
		std::puts( "  canvas would not initialise" );
		return 1;
	}

	Ring ring;
	ring.resize( channels, 8192 );

	Analyser         analyser;
	Analyser::Config config;
	analyser.configure( config, channels, kSampleRate, Layout::forChannelCount( channels ) );

	// Fixed cyan: twelve stills in one contact sheet should differ by display and
	// by nothing else. The movie renderer takes a hue, because a video wants to
	// move.
	const float foreground[ 4 ] = { 0.25f, 0.85f, 1.0f, 1.0f };
	const float background[ 4 ] = { 0.0f, 0.0f, 0.0f, 1.0f };

	int written = 0;
	for( const Job& job : kJobs )
	{
		canvas.clearHistory();

		ViewParams view;
		view.geometry  = job.geometry;
		view.style     = job.style;
		view.gain      = 0.75f;
		view.thickness = 0.8f;
		view.aspect    = (float)kWidth / (float)kHeight;

		Mesh                 mesh;
		Mesh                 graticule;
		std::vector< float > block;
		const int            perFrame = kSampleRate / 60;

		for( int f = 0; f < kFrames; ++f )
		{
			makeProgramme( block, perFrame, channels, kSampleRate, (double)f * perFrame / kSampleRate );
			ring.write( block.data(), perFrame );

			const Frame& frame = analyser.analyse( ring, (double)f / 60.0, 1.0f / 60.0f );

			buildFor( job, frame, view, mesh );

			buildGraticule( frame, view, graticule, 0.14f );
			canvas.render( mesh, graticule, view, target.fbo, kWidth, kHeight, 0.86f, foreground, background );
		}

		std::vector< unsigned char > pixels( (size_t)kWidth * kHeight * 4 );
		glBindFramebuffer( GL_FRAMEBUFFER, target.fbo );
		glReadPixels( 0, 0, kWidth, kHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data() );

		// GL reads bottom-up; PNG is top-down.
		std::vector< unsigned char > flipped( pixels.size() );
		for( int y = 0; y < kHeight; ++y )
			std::copy( pixels.begin() + (size_t)( kHeight - 1 - y ) * kWidth * 4,
					   pixels.begin() + (size_t)( kHeight - y ) * kWidth * 4,
					   flipped.begin() + (size_t)y * kWidth * 4 );

		const std::string path = directory + "/" + job.name + ".png";
		if( writePng( path, kWidth, kHeight, flipped ) )
		{
			// How much of the frame is lit. A picture that is entirely black or
			// entirely white is a failure the eye would catch and an automated
			// run would not.
			long long lit = 0;
			for( size_t i = 0; i < flipped.size(); i += 4 )
				if( flipped[ i ] > 8 || flipped[ i + 1 ] > 8 || flipped[ i + 2 ] > 8 )
					++lit;
			const double coverage = 100.0 * (double)lit / ( (double)kWidth * kHeight );
			std::printf( "  %-26s %5.1f%% lit  %s\n", job.name, coverage, path.c_str() );
			if( coverage < 0.05 || coverage > 95.0 )
			{
				std::puts( "     ^ suspicious: a blank or a solid block is not a display" );
				++written;   // counted as a failure below
			}
		}
		else
		{
			std::printf( "  %-26s could not be written\n", job.name );
			++written;
		}
	}

	canvas.deInitGL();
	CGLSetCurrentContext( nullptr );
	CGLDestroyContext( context );
	return written;
}

/**
	Render one shot as raw RGBA frames on stdout, for ffmpeg to encode.

	The video is a RENDER and not a screen recording, and that is a deliberate
	choice with the same reasoning tinsel, old-cathode and resolume-scopes
	reached: an FFGL plugin has no window and no UI of its own, its control
	surface is Resolume's inspector, and driving Arena means clicking a clip grid
	drawn with nothing in the accessibility tree to address. What comes out of
	here is genuinely the plugin's output -- the same Analyser, the same Builder,
	the same Canvas and the same shaders the four bundles link -- but it is not a
	capture of Resolume, and the video's end card says so.

	Frames go to stdout as tightly packed RGBA, top-down, so the caller can pipe
	straight into `ffmpeg -f rawvideo -pix_fmt rgba`. Nothing else may be printed
	to stdout in this mode; progress goes to stderr.
*/
int renderMovie( const std::string& shot, double seconds, int channels,
                 int width, int height, float hue )
{
	const Job* job = nullptr;
	for( const Job& candidate : kJobs )
		if( shot == candidate.name )
			job = &candidate;
	if( job == nullptr )
	{
		std::fprintf( stderr, "unknown shot '%s'. Known shots:\n", shot.c_str() );
		for( const Job& candidate : kJobs )
			std::fprintf( stderr, "  %s\n", candidate.name );
		return 1;
	}

	CGLContextObj context = createContext();
	if( context == nullptr )
	{
		std::fprintf( stderr, "could not create a 4.1 core context\n" );
		return 1;
	}

	constexpr int kSampleRate = 48000;
	constexpr int kFps        = 60;
	const int     frameCount  = (int)( seconds * kFps + 0.5 );

	Target target = makeTarget( width, height );

	Canvas canvas;
	if( !canvas.initGL() )
	{
		std::fprintf( stderr, "canvas would not initialise\n" );
		return 1;
	}

	Ring ring;
	ring.resize( channels, 8192 );

	Analyser         analyser;
	Analyser::Config config;
	analyser.configure( config, channels, kSampleRate, Layout::forChannelCount( channels ) );

	ViewParams view;
	view.geometry  = job->geometry;
	view.style     = job->style;
	view.gain      = 0.75f;
	view.thickness = 0.8f;
	view.aspect    = (float)width / (float)height;

	// Hue as a full-saturation, full-value colour. The sheet is fixed cyan
	// because twelve stills in one contact sheet should differ by display and
	// nothing else; a video wants to move.
	const float h = hue * 6.0f;
	const int   sector = (int)h % 6;
	const float f = h - (float)( (int)h );
	const float rgb[ 6 ][ 3 ] = {
		{ 1.0f, f, 0.0f }, { 1.0f - f, 1.0f, 0.0f }, { 0.0f, 1.0f, f },
		{ 0.0f, 1.0f - f, 1.0f }, { f, 0.0f, 1.0f }, { 1.0f, 0.0f, 1.0f - f }
	};
	// Lifted off full saturation so the trace keeps some body on a projector.
	const float foreground[ 4 ] = { 0.25f + 0.75f * rgb[ sector ][ 0 ],
	                                0.25f + 0.75f * rgb[ sector ][ 1 ],
	                                0.25f + 0.75f * rgb[ sector ][ 2 ], 1.0f };
	const float background[ 4 ] = { 0.0f, 0.0f, 0.0f, 1.0f };

	Mesh                 mesh;
	Mesh                 graticule;
	std::vector< float > block;
	const int            perFrame = kSampleRate / kFps;

	std::vector< unsigned char > pixels( (size_t)width * height * 4 );
	std::vector< unsigned char > flipped( pixels.size() );

	for( int f2 = 0; f2 < frameCount; ++f2 )
	{
		makeProgramme( block, perFrame, channels, kSampleRate,
		               (double)f2 * perFrame / kSampleRate );
		ring.write( block.data(), perFrame );

		const Frame& frame = analyser.analyse( ring, (double)f2 / kFps, 1.0f / kFps );
		buildFor( *job, frame, view, mesh );
		buildGraticule( frame, view, graticule, 0.14f );
		canvas.render( mesh, graticule, view, target.fbo, width, height, 0.86f,
		               foreground, background );

		glBindFramebuffer( GL_FRAMEBUFFER, target.fbo );
		glReadPixels( 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data() );

		// GL reads bottom-up; ffmpeg's rawvideo is top-down.
		for( int y = 0; y < height; ++y )
			std::copy( pixels.begin() + (size_t)( height - 1 - y ) * width * 4,
			           pixels.begin() + (size_t)( height - y ) * width * 4,
			           flipped.begin() + (size_t)y * width * 4 );

		if( std::fwrite( flipped.data(), 1, flipped.size(), stdout ) != flipped.size() )
		{
			std::fprintf( stderr, "short write on stdout at frame %d\n", f2 );
			return 1;
		}
	}
	std::fflush( stdout );
	std::fprintf( stderr, "  %s: %d frames %dx%d, %d channels\n",
	              shot.c_str(), frameCount, width, height, channels );

	canvas.deInitGL();
	CGLSetCurrentContext( nullptr );
	CGLDestroyContext( context );
	return 0;
}
