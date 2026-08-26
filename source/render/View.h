#pragma once

#include "../analysis/Frame.h"

#include <vector>

/**
	The two axes every plugin in the suite shares.

	`Geometry` is where the picture is laid out and `Style` is what it is drawn
	with, and they are independent on purpose: four displays times three
	geometries times three styles is thirty-six pictures out of one set of
	controls, and an operator who learns the controls on one plugin already
	knows the others. That is what makes this a suite rather than four plugins
	that happen to ship together.
*/
namespace spasis
{
enum class Geometry
{
	Circular,      ///< the full turn -- surround, and anything with a back
	SemiCircular,  ///< the front half only, L to R: the goniometer's own frame
	Raster,        ///< Cartesian, filling the output. No circle at all.
};

enum class Style
{
	Oscilloscope,  ///< a continuous trace, drawn as a line and left to decay
	Bargraph,      ///< discrete quads, one per band or bin
	Particle,      ///< points, scattered by magnitude
};

/**
	One vertex, in **normalised device coordinates already**.

	The geometry mapping happens on the CPU, in `Builder`, and not in a vertex
	shader. That is a deliberate inversion of the usual advice, and the reason
	is that there are at most a few thousand vertices in any frame here -- the
	whole build costs less than the uniform uploads a GPU-side mapping would
	need -- while a CPU mapping is something `sptest` can assert about without
	a GL context. A geometry that is wrong in a shader can only be caught by
	looking at it.
*/
struct Vertex
{
	float x = 0.0f;
	float y = 0.0f;
	float intensity = 0.0f;   ///< 0..1, multiplied into the colour
	float size = 1.0f;        ///< point size in pixels; ignored by line and quad draws
};

/// What the builder produced and how the canvas should draw it.
enum class Primitive
{
	Points,
	LineStrip,
	Lines,       ///< disconnected segments -- the graticule, and only that
	Triangles,
};

struct Mesh
{
	std::vector< Vertex > vertices;
	Primitive             primitive = Primitive::Points;

	void clear()
	{
		vertices.clear();
	}
};

/// Display-side knobs shared by every plugin.
struct ViewParams
{
	Geometry geometry = Geometry::Circular;
	Style    style    = Style::Oscilloscope;

	float gain       = 1.0f;   ///< display gain, applied after analysis
	float thickness  = 1.0f;   ///< point size / bar width scale
	float innerRadius = 0.12f; ///< circular and semi-circular: the hole in the middle
	float aspect     = 1.0f;   ///< output width / height, so circles stay circular
	float rotation   = 0.0f;   ///< radians, applied to the circular geometries
};

} // namespace spasis
