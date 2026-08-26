#pragma once

#include <cstddef>
#include <vector>

/**
	The contract between the analyser and the renderers.

	This header includes nothing but the standard library, on purpose. Every
	file in `audio/`, `analysis/`, `render/` and `sources/` includes it, and it
	is the one place the conventions all four plugins must agree about are
	written down.

	The suite's whole reason for existing is that Resolume cannot supply this.
	FFGL's only audio input is one buffer parameter carrying N magnitude bins of
	a **mono sum** -- no phase, no channels, no time domain. Every display in
	spasis except tonal balance needs at least two of those. So spasis captures
	its own audio, and this struct is what comes out the far end.
*/
namespace spasis
{
struct Vec2
{
	float x = 0.0f;
	float y = 0.0f;
};

//---------------------------------------------------------------------------
// Where the speakers are
//---------------------------------------------------------------------------

/**
	One channel's direction, as seen from the listener.

	`azimuth` is radians, **0 straight ahead, positive to the right**. That is
	the audio convention (and the one every surround layout table is written
	in), not the maths one -- screen space is derived from it in exactly one
	place, `render/Geometry.h`, and nowhere else may do the conversion.

	`elevation` is radians above the ear plane. Height channels carry it; spasis
	is a two-dimensional display, so it is used to *shade* a lobe rather than to
	move it, which is honest about the fact that a flat picture cannot show
	height and refuses to fake it.

	An `lfe` channel is excluded from every directional computation. It has no
	direction to contribute, and including it drags the whole field toward
	whatever angle the layout table happens to list it at -- a bug that looks
	like a mix problem.
*/
struct ChannelPlacement
{
	float azimuth   = 0.0f;
	float elevation = 0.0f;
	bool  lfe       = false;
};

/**
	A speaker layout.

	The stereo default is **±45°, not ±30°**. That is deliberate and it is the
	single most consequential number in the suite.

	The field point for one sample instant is the amplitude-weighted sum of the
	unit vectors to each speaker:

	    p = sum_i ( s_i * u_i )

	With two speakers at ±45°, u_L = (-r, r) and u_R = (+r, r) for r = 1/sqrt2,
	so

	    p = ( r(R - L), r(L + R) ) = ( side, mid ) * r

	which is exactly the classic goniometer: mono rides the vertical axis, out
	of phase lies flat on the horizontal. The generalised law and the instrument
	engineers already know are the same thing, and they are only the same thing
	at 45°. Set the stereo layout to the ITU ±30° and every goniometer in the
	suite quietly stops agreeing with every other goniometer on earth.

	For real surround layouts the listening angles *are* the right ones, because
	there the display is showing where the energy is pointing rather than
	reproducing a standard instrument.
*/
struct Layout
{
	std::vector< ChannelPlacement > channels;

	static Layout stereo();          ///< ±45°, the goniometer convention
	static Layout mono();
	static Layout quad();
	static Layout surround51();      ///< ITU-R BS.775, L R C LFE Ls Rs
	static Layout surround71();
	static Layout ring( int n );     ///< n channels spread evenly, first straight ahead

	/// Best guess for a channel count with no other information.
	static Layout forChannelCount( int n );

	int  count() const { return (int)channels.size(); }
	bool empty() const { return channels.empty(); }
};

//---------------------------------------------------------------------------
// What one analysed block looks like
//---------------------------------------------------------------------------

/**
	Everything the renderers are allowed to know.

	Sizes are fixed for the lifetime of an analyser so a renderer can size its
	buffers once. `channels` can change (a device is swapped) -- that
	invalidates the layout and every per-channel vector, and the analyser
	publishes a whole new Frame rather than resizing one in place.
*/
struct Frame
{
	double time     = 0.0;   ///< seconds since capture began, monotonic
	int    channels = 0;
	int    sampleRate = 48000;
	bool   live     = false; ///< false = the host-FFT fallback is driving this

	Layout layout;

	//-- Sample domain -------------------------------------------------------

	/**
		The field trace: one point per input sample, oldest first.

		This is the goniometer, and for a stereo layout it is the goniometer
		exactly (see Layout). Points are in **field space**: +Y ahead, +X right,
		unit-length per unit amplitude in a single speaker. A correlated stereo
		signal at full scale therefore reaches y = sqrt(2), not 1 -- the display
		normalises, the analysis does not, because clamping here would hide
		exactly the overload the instrument exists to show.
	*/
	std::vector< Vec2 > field;

	/**
		Field energy as a function of angle: `rose[i]` covers the angular slice
		centred on `roseAngle(i)`, normalised so the largest bin is 1.

		This is what makes the surround level rose and Ozone's polar level the
		same display with different inputs. Discrete speaker feeds concentrate
		into narrow lobes; a diffuse stereo mix spreads into a hump.
	*/
	std::vector< float > rose;

	//-- Frequency domain ----------------------------------------------------

	std::vector< float > bandFreq;   ///< centre frequency of each band, Hz
	std::vector< float > bandMag;    ///< 0..1, perceptually scaled. Tonal balance.
	std::vector< Vec2 >  bandField;  ///< per-band field position -- where that band sits
	std::vector< float > bandWidth;  ///< 0 = collapsed to a point, 1 = fully spread
	std::vector< float > bandCorr;   ///< -1..+1. Stereo only; NaN-free but meaningless above 2 channels

	//-- Per channel ---------------------------------------------------------

	std::vector< float > chPeak;     ///< 0..1+, sample peak since the last frame
	std::vector< float > chRms;      ///< 0..1+, ballistic-smoothed

	int   bandCount() const { return (int)bandMag.size(); }
	int   roseCount() const { return (int)rose.size(); }

	/// Centre angle of rose bin `i`, in the same convention as ChannelPlacement.
	float roseAngle( int i ) const;
};

} // namespace spasis
