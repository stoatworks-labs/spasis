#pragma once

#include "View.h"

namespace spasis
{
/// Which measurement is being drawn. One value per plugin in the suite.
enum class Display
{
	Field,     ///< goniometer / polar sample
	Rose,      ///< polar level, and the surround lobes
	Width,     ///< stereo width against frequency
	Balance,   ///< tonal balance
};

/**
	Turns a Frame into vertices.

	One free function per display, all sharing `place()` -- the single point
	where a plot coordinate becomes a screen coordinate. Every geometry rule in
	the suite lives in that one function, so "the circular mode is inside out"
	is one fix and not four.
*/

/**
	The geometry mapping, and the only one.

	`t` runs 0..1 along the display's own axis: band index for the spectra,
	angle bin for the rose, position along the sweep for anything swept.
	`v` runs 0..1 and is the value.

	For the circular geometries `t` becomes an angle and `v` a radius; for
	Raster, `t` is x and `v` is y. Aspect correction is applied to the circular
	forms only -- stretching a raster plot to a square would waste most of a
	16:9 output for no reason, while an ellipse instead of a circle in a
	goniometer is a measurement error.
*/
Vertex place( float t, float v, const ViewParams& view );

/// The 2D field, mapped without going through `place()` -- it is already a
/// position rather than a value on an axis.
Vertex placeField( Vec2 point, const ViewParams& view );

//---------------------------------------------------------------------------

/// Goniometer / polar sample. The field trace itself.
void buildField( const Frame& frame, const ViewParams& view, Mesh& out );

/// Polar level: field energy against angle.
void buildRose( const Frame& frame, const ViewParams& view, Mesh& out );

/// Per-speaker lobes, drawn from the layout and the channel meters rather than
/// from the field. This is the surround level display, and it is a genuinely
/// different measurement from `buildRose` -- see the note on Frame::rose.
void buildSpeakers( const Frame& frame, const ViewParams& view, Mesh& out );

/// Stereo width against frequency.
void buildWidth( const Frame& frame, const ViewParams& view, Mesh& out );

/// Tonal balance.
void buildBalance( const Frame& frame, const ViewParams& view, Mesh& out );

/**
	The graticule: the instrument's own markings, drawn whether or not there is
	a signal.

	It is not decoration. Without it a plugin with no audio yet -- no device
	chosen, a device with nothing routed to it, or one of the three directional
	displays sitting on the host-FFT fallback that cannot feed them -- renders a
	frame of pure black, which is indistinguishable from a broken plugin, an
	unloaded plugin and a black clip. Every hardware meter these displays copy
	has markings for the same reason.

	`dim` is how bright it sits relative to the trace, and the caller drops it
	when the display IS being fed, so the markings recede once there is
	something to read.
*/
void buildGraticule( const Frame& frame, const ViewParams& view, Mesh& out, float dim );

} // namespace spasis
