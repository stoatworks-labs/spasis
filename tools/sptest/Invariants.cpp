/**
	The invariant tests.

	These are the tests the suite exists to pass. Everything here drives the
	shipping analysis code -- there is no reimplementation to compare against
	except the direct DFT in `--fft`, which is the point of that one test.

	The landmarks in `--field` and `--width` are the whole claim of the project:
	that one law (amplitude-weighted sum of speaker unit vectors) reproduces the
	instrument engineers already own at two channels, and keeps working past it.
	If any of them move, the generalisation is broken, however good the picture
	looks.
*/
#include "../../source/analysis/Analyser.h"
#include "../../source/audio/Ring.h"
#include "../../source/render/Builder.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

using namespace spasis;

namespace
{
constexpr float kPi = 3.14159265358979323846f;

int   failures = 0;
int   checks   = 0;

void check( bool ok, const char* what, double got, double want, double tol )
{
	++checks;
	if( !ok )
	{
		++failures;
		std::printf( "  FAIL  %-42s got %+.5f want %+.5f (tol %.5f)\n", what, got, want, tol );
	}
	else
	{
		std::printf( "  ok    %-42s %+.5f\n", what, got );
	}
}

void near( double got, double want, double tol, const char* what )
{
	check( std::fabs( got - want ) <= tol, what, got, want, tol );
}

/// A goniometer trace is a line through the origin, so an angle and that angle
/// plus 180 degrees are the same answer. Comparing them directly makes the
/// anti-phase case fail by exactly one straight angle.
void nearAngle( double got, double want, double tol, const char* what )
{
	double d = std::fmod( got - want + 450.0, 180.0 ) - 90.0;
	check( std::fabs( d ) <= tol, what, got, want, tol );
}

//---------------------------------------------------------------------------

int testFFT()
{
	std::puts( "FFT against a direct DFT" );
	for( int n : { 64, 256, 1024 } )
	{
		FFT                                 fft( n );
		std::vector< std::complex< float > > a( n ), b( n );
		for( int i = 0; i < n; ++i )
		{
			// A signal with structure at several frequencies and a DC offset,
			// so a transform that is right only for pure tones fails here.
			const float t = (float)i / (float)n;
			const float v = 0.3f + std::sin( 2.0f * kPi * 3.0f * t ) + 0.5f * std::cos( 2.0f * kPi * 17.0f * t + 0.7f );
			a[ i ] = b[ i ] = std::complex< float >( v, 0.0f );
		}

		fft.forward( a.data() );

		double worst = 0.0;
		double scale = 0.0;
		for( int k = 0; k < n; ++k )
		{
			std::complex< double > acc;
			for( int i = 0; i < n; ++i )
			{
				const double ang = -2.0 * kPi * (double)k * (double)i / (double)n;
				acc += std::complex< double >( b[ i ].real(), 0.0 ) * std::complex< double >( std::cos( ang ), std::sin( ang ) );
			}
			worst = std::max( worst, std::abs( std::complex< double >( a[ k ].real(), a[ k ].imag() ) - acc ) );
			scale = std::max( scale, std::abs( acc ) );
		}
		// Relative to the largest bin, not absolute: the error of a float
		// transform scales with the spectrum, so an absolute bound is a bound
		// on the test signal's amplitude and nothing else.
		//
		// 1e-4 relative is -80 dB, which is twenty decibels below the bottom of
		// every display in the suite. A transform accurate enough to matter
		// here would have to be wrong by something visible.
		char label[ 64 ];
		std::snprintf( label, sizeof( label ), "N=%d error vs DFT, relative", n );
		near( worst / scale, 0.0, 1e-4, label );
	}
	return failures;
}

//---------------------------------------------------------------------------

/// Fill a ring with a stereo signal described by a gain and phase per channel.
void feed( Ring& ring, int channels, int frames, const float* gain, const float* phase, float cyclesPerBlock = 8.0f )
{
	std::vector< float > buf( (size_t)frames * channels );
	for( int i = 0; i < frames; ++i )
	{
		const float t = 2.0f * kPi * cyclesPerBlock * (float)i / (float)frames;
		for( int c = 0; c < channels; ++c )
			buf[ (size_t)i * channels + c ] = gain[ c ] * std::sin( t + phase[ c ] );
	}
	ring.write( buf.data(), frames );
}

Analyser makeAnalyser( int channels, const Layout& layout )
{
	Analyser           a;
	Analyser::Config   cfg;
	cfg.fftSize        = 2048;
	cfg.fieldPoints    = 2048;
	cfg.releaseSeconds = 0.0f;   // ballistics off: these are measurements
	a.configure( cfg, channels, 48000, layout );
	return a;
}

/// Mean angle of the field trace, weighted by magnitude. Audio convention.
double fieldAngle( const Frame& f )
{
	double sx = 0.0, sy = 0.0;
	for( const Vec2& p : f.field )
	{
		const double m = std::sqrt( p.x * p.x + p.y * p.y );
		// Fold to a half-turn: a goniometer trace is a line through the origin,
		// so the two ends are 180 degrees apart and a plain mean cancels them.
		double a = std::atan2( p.x, p.y );
		if( a < 0.0 )
			a += kPi;
		sx += m * std::sin( 2.0 * a );
		sy += m * std::cos( 2.0 * a );
	}
	return 0.5 * std::atan2( sx, sy ) * 180.0 / kPi;
}

int testField()
{
	std::puts( "\nThe field law reproduces the classic goniometer at two channels" );
	const int n = 2048;

	struct Case
	{
		const char* name;
		float       gain[ 2 ];
		float       phase[ 2 ];
		double      wantDegrees;
	};
	// A goniometer puts mono up the vertical axis, anti-phase flat on the
	// horizontal, and a hard-panned channel on its own diagonal. These four
	// numbers are the reason the stereo layout is +-45 and not +-30.
	const Case cases[] = {
		{ "mono (correlated) -> 0 deg, vertical", { 1.0f, 1.0f }, { 0.0f, 0.0f }, 0.0 },
		{ "anti-phase -> 90 deg, horizontal", { 1.0f, 1.0f }, { 0.0f, kPi }, 90.0 },
		{ "hard left -> -45 deg", { 1.0f, 0.0f }, { 0.0f, 0.0f }, -45.0 },
		{ "hard right -> +45 deg", { 0.0f, 1.0f }, { 0.0f, 0.0f }, 45.0 },
	};

	for( const Case& c : cases )
	{
		Ring ring;
		ring.resize( 2, 8192 );
		feed( ring, 2, n, c.gain, c.phase );
		Analyser     a     = makeAnalyser( 2, Layout::stereo() );
		const Frame& frame = a.analyse( ring, 0.0, 1.0f / 60.0f );
		nearAngle( fieldAngle( frame ), c.wantDegrees, 0.5, c.name );
	}

	// And the one that would be silently wrong if someone "corrected" the
	// layout to the ITU angles.
	{
		Ring ring;
		ring.resize( 2, 8192 );
		const float g[ 2 ] = { 1.0f, 0.0f }, p[ 2 ] = { 0.0f, 0.0f };
		feed( ring, 2, n, g, p );
		Layout itu;
		itu.channels.resize( 2 );
		itu.channels[ 0 ].azimuth = -30.0f * kPi / 180.0f;
		itu.channels[ 1 ].azimuth = 30.0f * kPi / 180.0f;
		Analyser     a     = makeAnalyser( 2, itu );
		const Frame& frame = a.analyse( ring, 0.0, 1.0f / 60.0f );
		nearAngle( fieldAngle( frame ), -30.0, 0.5, "at ITU +-30 hard left lands at -30, not -45" );
	}
	return failures;
}

//---------------------------------------------------------------------------

int testWidth()
{
	std::puts( "\nWidth agrees with |S|/(|M|+|S|) at every landmark" );
	const int n = 2048;

	struct Case
	{
		const char* name;
		int         channels;
		float       gain[ 8 ];
		float       phase[ 8 ];
		double      want;
	};
	const Case cases[] = {
		{ "stereo mono -> 0", 2, { 1.0f, 1.0f }, { 0.0f, 0.0f }, 0.0 },
		{ "stereo anti-phase -> 1", 2, { 1.0f, 1.0f }, { 0.0f, kPi }, 1.0 },
		{ "stereo hard-panned -> 0.5", 2, { 1.0f, 0.0f }, { 0.0f, 0.0f }, 0.5 },
		{ "stereo 90 deg apart -> 0.5", 2, { 1.0f, 1.0f }, { 0.0f, kPi * 0.5f }, 0.5 },
		{ "six correlated channels -> 0", 6, { 1, 1, 1, 1, 1, 1 }, { 0, 0, 0, 0, 0, 0 }, 0.0 },
		{ "one of six -> 1 - 1/6", 6, { 1, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, 1.0 - 1.0 / 6.0 },
	};

	for( const Case& c : cases )
	{
		Ring ring;
		ring.resize( c.channels, 8192 );
		feed( ring, c.channels, n, c.gain, c.phase );
		Analyser     a     = makeAnalyser( c.channels, Layout::forChannelCount( c.channels ) );
		const Frame& frame = a.analyse( ring, 0.0, 1.0f / 60.0f );

		// The signal is a tone at bin 8 of a 2048 block: read the band that
		// contains it rather than averaging bands that hold only noise floor.
		const float toneHz = 8.0f * 48000.0f / 2048.0f;
		int         best   = 0;
		for( int b = 0; b < frame.bandCount(); ++b )
			if( std::fabs( frame.bandFreq[ b ] - toneHz ) < std::fabs( frame.bandFreq[ best ] - toneHz ) )
				best = b;

		near( frame.bandWidth[ best ], c.want, 0.02, c.name );
	}
	return failures;
}

//---------------------------------------------------------------------------

int testLfe()
{
	std::puts( "\nLFE contributes no direction" );
	const int n = 2048;

	// 5.1 with signal ONLY in the LFE channel. If LFE were treated as a
	// direction, the field would swing to whatever angle the table lists and
	// the display would show a hard pan that is not in the mix.
	Ring ring;
	ring.resize( 6, 8192 );
	const float g[ 8 ] = { 0, 0, 0, 1.0f, 0, 0 }, p[ 8 ] = { 0, 0, 0, 0, 0, 0 };
	feed( ring, 6, n, g, p );
	Analyser     a     = makeAnalyser( 6, Layout::surround51() );
	const Frame& frame = a.analyse( ring, 0.0, 1.0f / 60.0f );

	double worst = 0.0;
	for( const Vec2& q : frame.field )
		worst = std::max( worst, (double)std::sqrt( q.x * q.x + q.y * q.y ) );
	near( worst, 0.0, 1e-6, "LFE-only 5.1 leaves the field at the origin" );

	// ...while still being audible as a level.
	check( frame.chRms[ 3 ] > 0.5f, "LFE still reads a level", frame.chRms[ 3 ], 1.0, 0.5 );
	return failures;
}

//---------------------------------------------------------------------------

int testFallback()
{
	std::puts( "\nThe host-FFT fallback claims nothing it cannot know" );
	Analyser     a = makeAnalyser( 2, Layout::stereo() );
	std::vector< float > bins( 64, 0.5f );
	const Frame& frame = a.analyseHostFFT( bins.data(), (int)bins.size(), 0.0, 1.0f / 60.0f );

	check( !frame.live, "frame is marked not live", frame.live ? 1 : 0, 0, 0 );

	double worstField = 0.0, worstWidth = 0.0, worstRose = 0.0;
	for( const Vec2& q : frame.field )
		worstField = std::max( worstField, (double)std::fabs( q.x ) + std::fabs( q.y ) );
	for( float w : frame.bandWidth )
		worstWidth = std::max( worstWidth, (double)std::fabs( w ) );
	for( float r : frame.rose )
		worstRose = std::max( worstRose, (double)std::fabs( r ) );

	near( worstField, 0.0, 0.0, "field is empty, not invented" );
	near( worstWidth, 0.0, 0.0, "width is empty, not invented" );
	near( worstRose, 0.0, 0.0, "rose is empty, not invented" );

	double magSum = 0.0;
	for( float m : frame.bandMag )
		magSum += m;
	check( magSum > 0.0, "tonal balance is still populated", magSum, 1.0, 1.0 );
	return failures;
}

//---------------------------------------------------------------------------

int testGraticule()
{
	std::puts( "\nThe graticule marks every speaker, including the ones on the left" );

	// A 5.1 layout has three speakers at negative azimuths. They arrive at the
	// geometry as a negative parameter, which in the full circle has to wrap --
	// and without the wrap every one of them was silently dropped.
	Frame frame;
	frame.channels = 6;
	frame.layout   = Layout::surround51();

	ViewParams view;
	view.geometry = Geometry::Circular;
	view.aspect   = 16.0f / 9.0f;

	Mesh mesh;
	buildGraticule( frame, view, mesh, 1.0f );

	// Count the radial segments: they run from the inner radius to the rim, so
	// their two endpoints differ in length. Ring segments do not.
	int radials = 0, radialsOnTheLeft = 0;
	for( size_t i = 0; i + 1 < mesh.vertices.size(); i += 2 )
	{
		const Vertex& a = mesh.vertices[ i ];
		const Vertex& b = mesh.vertices[ i + 1 ];
		const float   ra = std::sqrt( a.x * a.x + a.y * a.y );
		const float   rb = std::sqrt( b.x * b.x + b.y * b.y );
		if( std::fabs( ra - rb ) <= 0.2f )
			continue;   // a ring segment, not a radial
		++radials;
		if( b.x < -0.05f )
			++radialsOnTheLeft;
	}
	// Five speakers (the LFE has no direction) plus the centre mark, which
	// coincides with the C channel -- so five distinct angles, six segments.
	near( radials, 6.0, 0.0, "5.1 draws a radial for every non-LFE speaker" );

	// L at -30 and Ls at -110: two RADIALS on the left. Counting any vertex on
	// the left instead would pass on the ring alone, which is how the first
	// version of this check managed to pass against the broken code.
	near( radialsOnTheLeft, 2.0, 0.0, "two of those radials are on the left" );
	return failures;
}

int testRing()
{
	std::puts( "\nThe ring drops the oldest, never the newest" );
	Ring ring;
	ring.resize( 1, 1024 );

	// Write two and a half rings of a ramp, then check the tail is the newest
	// samples and not a stale window.
	std::vector< float > in( 2560 );
	for( size_t i = 0; i < in.size(); ++i )
		in[ i ] = (float)i;
	ring.write( in.data(), in.size() );

	std::vector< float > out( 1024 );
	check( ring.peekLatest( out.data(), 1024 ), "peek succeeds after overrun", 1, 1, 0 );
	near( out[ 1023 ], 2559.0, 0.0, "last sample is the newest written" );
	near( out[ 0 ], 1536.0, 0.0, "first sample is exactly one ring back" );
	check( ring.overwritten() == 1536, "overwrite count is exact", (double)ring.overwritten(), 1536.0, 0 );
	return failures;
}
} // namespace

int checkParams( int& checks );
int listDevices( const std::string& openName, double seconds );

#if defined( __APPLE__ )
int checkShaders();
int renderSheet( const std::string& directory, int channels );
#else
int checkShaders() { return 0; }
int renderSheet( const std::string&, int ) { return 0; }
#endif

int main( int argc, char** argv )
{
	std::string only = ( argc > 1 ) ? argv[ 1 ] : "";

	/*
		`--offline` is everything that does not need a GL context.

		A GitHub macOS runner cannot create an accelerated 4.1 core context, so
		bare `sptest` there reports "could not create a 4.1 core context" and
		fails a build in which nothing is wrong. Rather than have CI name the
		groups it wants -- a list that goes stale silently the first time a
		group is added -- `--offline` is defined as `--all` minus the GL ones,
		at the one place that knows which those are.

		The shaders are still checked on such a machine, by tools/check-shaders.sh,
		which compiles them with glslc and needs no driver. That is a different
		check rather than a substitute: only a real driver can tell you that
		Apple's Metal GL disagrees with the compiler. So this says so out loud
		rather than letting a green run be read as one that checked them.
	*/
	const bool offline = ( only == "--offline" );
	const bool all     = only.empty() || only == "--all" || offline;

	if( ( all && !offline ) || only == "--shaders" )
	{
		std::puts( "Every shader compiles on this driver" );
		const int bad = checkShaders();
		checks += 5;
		failures += bad;
	}
	else if( offline )
		std::puts( "Shaders NOT checked against a driver -- offline run\n"
		           "  (tools/check-shaders.sh compiles them with glslc instead)" );

	if( all || only == "--fft" )
		testFFT();
	if( all || only == "--field" )
		testField();
	if( all || only == "--width" )
		testWidth();
	if( all || only == "--lfe" )
		testLfe();
	if( all || only == "--fallback" )
		testFallback();
	if( all || only == "--graticule" )
		testGraticule();

	if( all || only == "--ring" )
		testRing();

	if( all || only == "--params" )
	{
		std::puts( "\nWhat the host will be told about the parameters" );
		failures += checkParams( checks );
	}

	if( only == "--devices" )
	{
		const std::string open = ( argc > 2 ) ? argv[ 2 ] : "";
		const double      secs = ( argc > 3 ) ? std::atof( argv[ 3 ] ) : 3.0;
		return listDevices( open, secs );
	}

	if( only == "--sheet" )
	{
		const std::string directory = ( argc > 2 ) ? argv[ 2 ] : ".";
		const int         channels  = ( argc > 3 ) ? std::atoi( argv[ 3 ] ) : 2;
		std::printf( "Rendering the contact sheet, %d channels, into %s\n", channels, directory.c_str() );
		checks += 12;
		failures += renderSheet( directory, channels );
	}

	std::printf( "\n%d checks, %d failed\n", checks, failures );
	return failures == 0 ? 0 : 1;
}
