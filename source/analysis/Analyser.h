#pragma once

#include "../audio/Ring.h"
#include "FFT.h"
#include "Frame.h"

#include <complex>
#include <vector>

namespace spasis
{
/**
	Turns captured audio into a Frame.

	One analyser serves all four plugins, and it computes everything every time
	rather than letting each plugin ask for only its own display. That is a
	deliberate trade: the whole analysis is well under a millisecond, and the
	alternative -- four subsets that can each be subtly differently scaled -- is
	how a suite stops looking like a suite.

	Not thread-safe. It lives on the render thread and only ever *reads* the
	capture ring.
*/
class Analyser
{
public:
	struct Config
	{
		int   fftSize     = 2048;
		int   bands       = 48;
		int   roseBins    = 180;
		int   fieldPoints = 2048;

		/// Meter ballistics: instant attack, exponential release. 150 ms is the
		/// fleet's existing figure and it is a display constant, not a
		/// measurement one -- nothing downstream may treat a released value as
		/// the signal level.
		float releaseSeconds = 0.15f;

		/// Bottom of the displayed range. Everything is mapped 0..1 across
		/// this, in dB, because a linear magnitude display of music is a flat
		/// line with occasional spikes.
		float floorDb = -60.0f;
	};

	void configure( const Config& config, int channels, int sampleRate, const Layout& layout );

	bool configured() const { return frame_.channels > 0; }
	int  channels() const { return frame_.channels; }

	/// Read the newest audio out of `ring` and analyse it. `dt` is the display
	/// frame interval in seconds, used only for ballistics.
	const Frame& analyse( const Ring& ring, double time, float dt );

	/**
		The fallback path: Resolume's own FFT buffer.

		`bins` are magnitudes of a mono sum, so there is no field, no rose and
		no width -- those are left at zero and `Frame::live` is false, which is
		what tells a renderer to say so on screen instead of drawing an empty
		instrument and letting the user think the audio is dead.
	*/
	const Frame& analyseHostFFT( const float* bins, int count, double time, float dt );

	const Frame& frame() const { return frame_; }

private:
	void   buildBands( int sampleRate );
	float  release( float previous, float target, float dt ) const;

	Config config_;
	Frame  frame_;

	FFT                  fft_{ 2 };
	std::vector< float > window_;
	float                windowGain_ = 1.0f;

	std::vector< float >                  scratch_;   ///< interleaved pull from the ring
	std::vector< std::complex< float > >  spectra_;   ///< channels * fftSize/2+1, packed
	std::vector< int >                    bandLo_, bandHi_;

	// Ballistic state, held across frames.
	std::vector< float > heldBandMag_, heldRose_, heldRms_;
};

} // namespace spasis
