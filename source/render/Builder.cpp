#include "Builder.h"

#include <algorithm>
#include <cmath>

namespace spasis
{
namespace
{
constexpr float kPi = 3.14159265358979323846f;

/// Deterministic scatter for the particle style.
///
/// A real RNG would make the display fizz differently every frame and the
/// picture would never settle, which reads as noise rather than as a signal
/// that happens to be noisy. This is seeded from the vertex index and the
/// value, so a steady signal produces a steady cloud that only moves when the
/// audio does -- and `sptest` can assert about it.
float hash( unsigned int n )
{
	n = ( n << 13u ) ^ n;
	n = n * ( n * n * 15731u + 789221u ) + 1376312589u;
	return (float)( n & 0x7fffffffu ) / (float)0x7fffffff;
}

void appendQuad( Mesh& mesh, float x0, float y0, float x1, float y1, float intensity )
{
	const Vertex a{ x0, y0, intensity, 1.0f };
	const Vertex b{ x1, y0, intensity, 1.0f };
	const Vertex c{ x1, y1, intensity, 1.0f };
	const Vertex d{ x0, y1, intensity, 1.0f };
	mesh.vertices.insert( mesh.vertices.end(), { a, b, c, a, c, d } );
}

/// The four corners of a quad given as arbitrary points, for the radial bars
/// the circular geometries need -- a radial bar is a wedge, not a rectangle.
void appendQuad( Mesh& mesh, Vertex a, Vertex b, Vertex c, Vertex d )
{
	mesh.vertices.insert( mesh.vertices.end(), { a, b, c, a, c, d } );
}
} // namespace

Vertex place( float t, float v, const ViewParams& view )
{
	v = std::max( 0.0f, std::min( 1.0f, v * view.gain ) );

	Vertex out;
	out.intensity = v;

	if( view.geometry == Geometry::Raster )
	{
		// Full frame, bottom-anchored. No aspect correction: a raster plot is
		// not a circle and has nothing to keep round.
		out.x = t * 2.0f - 1.0f;
		out.y = v * 2.0f - 1.0f;
		return out;
	}

	// Circular forms. Angle runs clockwise from straight up, matching the
	// audio convention used everywhere in analysis/ -- 0 ahead, positive right.
	const float sweep = ( view.geometry == Geometry::Circular ) ? ( 2.0f * kPi ) : kPi;
	const float start = ( view.geometry == Geometry::Circular ) ? 0.0f : ( -kPi * 0.5f );
	const float angle = start + t * sweep + view.rotation;

	const float radius = view.innerRadius + v * ( 1.0f - view.innerRadius );

	out.x = std::sin( angle ) * radius;
	out.y = std::cos( angle ) * radius;

	// Aspect correction shrinks the wide axis rather than stretching the narrow
	// one, so the figure always fits.
	if( view.aspect > 1.0f )
		out.x /= view.aspect;
	else if( view.aspect > 0.0f )
		out.y *= view.aspect;

	return out;
}

Vertex placeField( Vec2 point, const ViewParams& view )
{
	// The field is not normalised by the analyser (see Frame::field), so the
	// scaling happens here where it is a display choice. 1/sqrt2 puts a
	// full-scale correlated stereo signal exactly on the rim, which is the
	// convention every goniometer uses and the reason an over reads as a trace
	// that leaves the circle instead of one that flattens against it.
	constexpr float kFullScale = 0.70710678f;

	float x = point.x * kFullScale * view.gain;
	float y = point.y * kFullScale * view.gain;

	if( view.rotation != 0.0f )
	{
		const float c = std::cos( view.rotation );
		const float s = std::sin( view.rotation );
		const float rx = x * c - y * s;
		y              = x * s + y * c;
		x              = rx;
	}

	Vertex out;
	out.intensity = std::min( 1.0f, std::sqrt( x * x + y * y ) );

	if( view.geometry == Geometry::SemiCircular )
	{
		// Fold the lower half up. A goniometer trace is symmetric through the
		// origin, so this discards nothing -- it doubles the density of the
		// half that is drawn, which is exactly what Ozone's polar sample shows.
		if( y < 0.0f )
		{
			x = -x;
			y = -y;
		}

		// Sit the flat edge on the bottom of the frame and make the half disc
		// as large as fits WITHOUT distorting it. Simply mapping y from 0..1
		// onto -1..1 is the obvious thing and it is wrong: it stretches the
		// disc vertically by the output aspect, so an out-of-phase signal no
		// longer lies at 90 degrees to a mono one and the instrument stops
		// being an instrument.
		//
		// The half disc needs height r and width 2r on screen. In NDC that is
		// r vertically and r/aspect horizontally either side of centre, so the
		// largest r that fits is min( aspect, 2 ).
		const float a = ( view.aspect > 0.0f ) ? view.aspect : 1.0f;
		const float r = std::min( a, 2.0f );
		out.x         = x * r / a;
		out.y         = -1.0f + y * r;
		return out;
	}

	if( view.geometry == Geometry::Raster )
	{
		// Fill the output rather than inscribing a circle in it. The trace is
		// no longer angle-faithful and that is the trade the mode exists to
		// make: it is for filling a screen, not for measuring.
		out.x = x;
		out.y = y;
		return out;
	}

	if( view.aspect > 1.0f )
		x /= view.aspect;
	else if( view.aspect > 0.0f )
		y *= view.aspect;

	out.x = x;
	out.y = y;
	return out;
}

//---------------------------------------------------------------------------

void buildField( const Frame& frame, const ViewParams& view, Mesh& out )
{
	out.clear();
	const int n = (int)frame.field.size();
	if( n == 0 )
		return;

	if( view.style == Style::Bargraph )
	{
		// A bargraph of a 2D cloud is a density histogram: bin the points by
		// angle and draw the extent. Anything else would be drawing a bar chart
		// of x against sample index, which is a waveform, not a field.
		constexpr int kBins = 64;
		float         extent[ kBins ] = { 0.0f };
		for( const Vec2& p : frame.field )
		{
			float a = std::atan2( p.x, p.y );
			if( a < 0.0f )
				a += 2.0f * kPi;
			const int b = std::min( kBins - 1, (int)( a / ( 2.0f * kPi ) * kBins ) );
			extent[ b ]  = std::max( extent[ b ], std::sqrt( p.x * p.x + p.y * p.y ) * 0.70710678f );
		}
		const float width = ( 1.0f / kBins ) * 0.8f * view.thickness;
		for( int b = 0; b < kBins; ++b )
		{
			const float t = ( b + 0.5f ) / kBins;
			appendQuad( out,
						place( t - width * 0.5f, 0.0f, view ), place( t + width * 0.5f, 0.0f, view ),
						place( t + width * 0.5f, extent[ b ], view ), place( t - width * 0.5f, extent[ b ], view ) );
		}
		out.primitive = Primitive::Triangles;
		return;
	}

	out.vertices.reserve( n );
	for( int i = 0; i < n; ++i )
	{
		Vertex v = placeField( frame.field[ i ], view );
		if( view.style == Style::Particle )
		{
			// Scatter proportional to magnitude: a loud passage becomes a cloud,
			// a quiet one stays a thread. Scattering by a constant would make
			// silence look like noise.
			const float spread = 0.02f * view.thickness * v.intensity;
			v.x += ( hash( (unsigned)i * 2u ) - 0.5f ) * spread;
			v.y += ( hash( (unsigned)i * 2u + 1u ) - 0.5f ) * spread;
			v.size = 1.0f + 3.0f * view.thickness;
		}
		else
		{
			v.size = 1.0f + view.thickness;
		}
		out.vertices.push_back( v );
	}
	out.primitive = ( view.style == Style::Oscilloscope ) ? Primitive::LineStrip : Primitive::Points;
}

//---------------------------------------------------------------------------

namespace
{
/// Shared by every (t, v) display. `values[i]` is plotted at t = (i+0.5)/n for
/// the discrete styles and at t = i/(n-1) for the continuous one -- bin centres
/// for bars, endpoints for a curve, which is the difference between a bar chart
/// that tiles the frame and a curve that reaches both edges.
void buildPlot( const float* values, int n, const ViewParams& view, Mesh& out )
{
	out.clear();
	if( values == nullptr || n <= 0 )
		return;

	switch( view.style )
	{
	case Style::Bargraph:
	{
		const float width = ( 1.0f / n ) * 0.8f * std::max( 0.1f, view.thickness );
		for( int i = 0; i < n; ++i )
		{
			const float t = ( i + 0.5f ) / (float)n;
			appendQuad( out,
						place( t - width * 0.5f, 0.0f, view ), place( t + width * 0.5f, 0.0f, view ),
						place( t + width * 0.5f, values[ i ], view ), place( t - width * 0.5f, values[ i ], view ) );
		}
		out.primitive = Primitive::Triangles;
		break;
	}
	case Style::Particle:
	{
		// Points scattered up the value axis, denser where the value is high --
		// a column of particles whose height is the reading and whose density
		// carries it a second time, which survives being seen at a distance.
		constexpr int kPerBand = 24;
		for( int i = 0; i < n; ++i )
		{
			const float t     = ( i + 0.5f ) / (float)n;
			const int   count = (int)( values[ i ] * kPerBand );
			for( int k = 0; k < count; ++k )
			{
				const unsigned seed = (unsigned)( i * 977 + k );
				const float    v    = values[ i ] * hash( seed );
				Vertex         q    = place( t + ( hash( seed * 3u ) - 0.5f ) / (float)n, v, view );
				q.size              = 1.0f + 3.0f * view.thickness;
				q.intensity         = 1.0f - v * 0.4f;
				out.vertices.push_back( q );
			}
		}
		out.primitive = Primitive::Points;
		break;
	}
	case Style::Oscilloscope:
	default:
	{
		for( int i = 0; i < n; ++i )
		{
			const float t = ( n > 1 ) ? ( (float)i / (float)( n - 1 ) ) : 0.5f;
			Vertex      q = place( t, values[ i ], view );
			q.size        = 1.0f + view.thickness;
			out.vertices.push_back( q );
		}
		// Close the loop in the full circle, or the curve has a seam at 0.
		if( view.geometry == Geometry::Circular && !out.vertices.empty() )
			out.vertices.push_back( out.vertices.front() );
		out.primitive = Primitive::LineStrip;
		break;
	}
	}
}
} // namespace

void buildRose( const Frame& frame, const ViewParams& view, Mesh& out )
{
	buildPlot( frame.rose.data(), frame.roseCount(), view, out );
}

void buildWidth( const Frame& frame, const ViewParams& view, Mesh& out )
{
	buildPlot( frame.bandWidth.data(), frame.bandCount(), view, out );
}

void buildBalance( const Frame& frame, const ViewParams& view, Mesh& out )
{
	buildPlot( frame.bandMag.data(), frame.bandCount(), view, out );
}

//---------------------------------------------------------------------------

void buildSpeakers( const Frame& frame, const ViewParams& view, Mesh& out )
{
	out.clear();
	const int n = std::min( (int)frame.chRms.size(), frame.layout.count() );
	if( n <= 0 )
		return;

	// A lobe per channel, centred on where that speaker actually is. Unlike the
	// rose this makes no attempt to be a distribution: it is N meters arranged
	// in a circle, which is what a surround level display has always been.
	constexpr int kLobeSteps = 12;
	for( int c = 0; c < n; ++c )
	{
		const ChannelPlacement& p = frame.layout.channels[ c ];
		if( p.lfe )
			continue;   // no direction; drawing it somewhere would invent one

		const float level = frame.chRms[ c ];
		if( level <= 0.0f )
			continue;

		// Height channels are shaded rather than moved -- a flat picture cannot
		// show elevation and pretending otherwise puts a ceiling speaker
		// somewhere on the horizontal plane where a floor speaker could be.
		const float shade = 1.0f - 0.45f * std::min( 1.0f, std::fabs( p.elevation ) / ( kPi * 0.5f ) );

		const float halfWidth = ( kPi / (float)std::max( 3, n ) ) * 0.45f * std::max( 0.2f, view.thickness );
		const float centre    = p.azimuth;

		for( int s = 0; s < kLobeSteps; ++s )
		{
			const float a0 = centre - halfWidth + ( 2.0f * halfWidth ) * ( (float)s / kLobeSteps );
			const float a1 = centre - halfWidth + ( 2.0f * halfWidth ) * ( (float)( s + 1 ) / kLobeSteps );

			// A cosine taper across the lobe, so adjacent speakers at similar
			// levels read as two lobes rather than one wide block.
			const float f0 = std::cos( ( a0 - centre ) / halfWidth * kPi * 0.5f );
			const float f1 = std::cos( ( a1 - centre ) / halfWidth * kPi * 0.5f );

			// place() takes t in 0..1 over the geometry's sweep; convert the
			// azimuth back into that parameter rather than duplicating the
			// polar maths here.
			const float sweep = ( view.geometry == Geometry::Circular ) ? ( 2.0f * kPi ) : kPi;
			const float start = ( view.geometry == Geometry::Circular ) ? 0.0f : ( -kPi * 0.5f );
			const float t0    = ( a0 - start ) / sweep;
			const float t1    = ( a1 - start ) / sweep;

			Vertex q0 = place( t0, 0.0f, view );
			Vertex q1 = place( t1, 0.0f, view );
			Vertex q2 = place( t1, level * f1, view );
			Vertex q3 = place( t0, level * f0, view );
			q2.intensity *= shade;
			q3.intensity *= shade;
			appendQuad( out, q0, q1, q2, q3 );
		}
	}
	out.primitive = Primitive::Triangles;
}


//---------------------------------------------------------------------------

namespace
{
void segment( Mesh& mesh, Vertex a, Vertex b, float intensity )
{
	a.intensity = intensity;
	b.intensity = intensity;
	a.size = b.size = 1.0f;
	mesh.vertices.push_back( a );
	mesh.vertices.push_back( b );
}

/// A ring at radius `v`, as disconnected segments so it can share one draw with
/// the straight markings.
void ring( Mesh& mesh, float v, const ViewParams& view, float intensity, int steps = 96 )
{
	ViewParams unit = view;
	unit.gain       = 1.0f;   // the graticule is fixed, never scaled by display gain
	for( int i = 0; i < steps; ++i )
	{
		const float t0 = (float)i / (float)steps;
		const float t1 = (float)( i + 1 ) / (float)steps;
		segment( mesh, place( t0, v, unit ), place( t1, v, unit ), intensity );
	}
}
} // namespace

void buildGraticule( const Frame& frame, const ViewParams& view, Mesh& out, float dim )
{
	out.clear();
	out.primitive = Primitive::Lines;
	if( dim <= 0.0f )
		return;

	ViewParams unit = view;
	unit.gain       = 1.0f;

	if( view.geometry == Geometry::Raster )
	{
		// A baseline and a few horizontal rules. Nothing circular to draw, and
		// a grid dense enough to be a grid would compete with the trace.
		for( int i = 0; i <= 4; ++i )
		{
			const float v = (float)i / 4.0f;
			segment( out, place( 0.0f, v, unit ), place( 1.0f, v, unit ),
					 dim * ( ( i == 0 ) ? 1.0f : 0.4f ) );
		}
		return;
	}

	const int steps = ( view.geometry == Geometry::Circular ) ? 96 : 48;
	ring( out, 1.0f, view, dim, steps );
	if( view.innerRadius > 0.001f )
		ring( out, 0.0f, view, dim * 0.7f, steps / 2 );

	// The radial markings. For the two circular geometries these are the angles
	// an engineer reads the instrument against: dead centre, the two speaker
	// positions, and hard left/right. They are taken from the LAYOUT rather than
	// drawn at fixed angles, so a 5.1 rose gets marks where its speakers are.
	const float sweep = ( view.geometry == Geometry::Circular ) ? ( 2.0f * kPi ) : kPi;
	const float start = ( view.geometry == Geometry::Circular ) ? 0.0f : ( -kPi * 0.5f );

	std::vector< float > angles;
	angles.push_back( 0.0f );
	for( const ChannelPlacement& p : frame.layout.channels )
		if( !p.lfe )
			angles.push_back( p.azimuth );
	if( frame.layout.count() <= 2 )
	{
		// Stereo also gets the horizontal, which is where an out-of-phase
		// signal lies and the one line anybody actually looks for.
		angles.push_back( kPi * 0.5f );
		angles.push_back( -kPi * 0.5f );
	}

	for( float a : angles )
	{
		float t = ( a - start ) / sweep;

		// Azimuths are signed -- 0 ahead, NEGATIVE to the left -- so every
		// speaker on the left half gives a negative t. In the full circle that
		// is a legal position and has to wrap; in a half circle it is genuinely
		// off the display. Testing the range without wrapping first silently
		// dropped every mark on the left, which on a 5.1 layout is half of them
		// and reads as a lopsided graticule rather than as a bug.
		if( view.geometry == Geometry::Circular )
			t -= std::floor( t );
		else if( t < -0.001f || t > 1.001f )
			continue;

		segment( out, place( t, 0.0f, unit ), place( t, 1.0f, unit ),
				 dim * ( ( std::fabs( a ) < 0.001f ) ? 0.9f : 0.45f ) );
	}
}

} // namespace spasis
