#include "Analyser.h"

#include <algorithm>
#include <cmath>

namespace spasis
{
namespace
{
constexpr float kPi     = 3.14159265358979323846f;
constexpr float kMinMag = 1.0e-9f;

/// dB above `floorDb`, mapped to 0..1 and clamped. The clamp at the top is not
/// a limiter -- overs are shown by the field, which is not normalised -- it is
/// just where the display runs out of room.
float toDisplay( float magnitude, float floorDb )
{
	const float db = 20.0f * std::log10( std::max( magnitude, kMinMag ) );
	return std::min( 1.0f, std::max( 0.0f, ( db - floorDb ) / -floorDb ) );
}
} // namespace

void Analyser::configure( const Config& config, int channels, int sampleRate, const Layout& layout )
{
	config_ = config;

	// Power of two, and never smaller than the number of points we publish --
	// the field trace is the tail of the same block the transform sees, so that
	// what the goniometer draws and what the spectrum measures are the same
	// audio. Two displays of the same instant that disagree is the failure this
	// avoids.
	int n = 2;
	while( n < config_.fftSize )
		n <<= 1;
	config_.fftSize     = n;
	config_.fieldPoints = std::min( config_.fieldPoints, n );

	fft_ = FFT( n );
	hannWindow( window_, n );

	// Coherent gain of the window, so a full-scale sine reads full scale rather
	// than 6 dB down. Hann's is 0.5 analytically; computing it keeps the two in
	// step if the window is ever changed.
	float sum = 0.0f;
	for( float w : window_ )
		sum += w;
	windowGain_ = ( sum > 0.0f ) ? ( sum / (float)n ) : 1.0f;

	frame_            = Frame();
	frame_.channels   = channels;
	frame_.sampleRate = sampleRate;
	frame_.layout     = layout.count() == channels ? layout : Layout::forChannelCount( channels );

	buildBands( sampleRate );

	const int bands = (int)bandLo_.size();
	frame_.bandMag.assign( bands, 0.0f );
	frame_.bandField.assign( bands, Vec2{} );
	frame_.bandWidth.assign( bands, 0.0f );
	frame_.bandCorr.assign( bands, 0.0f );
	frame_.rose.assign( config_.roseBins, 0.0f );
	frame_.field.assign( config_.fieldPoints, Vec2{} );
	frame_.chPeak.assign( std::max( 0, channels ), 0.0f );
	frame_.chRms.assign( std::max( 0, channels ), 0.0f );

	heldBandMag_.assign( bands, 0.0f );
	heldRose_.assign( config_.roseBins, 0.0f );
	heldRms_.assign( std::max( 0, channels ), 0.0f );

	scratch_.assign( (size_t)n * (size_t)std::max( 1, channels ), 0.0f );
	spectra_.assign( (size_t)n * (size_t)std::max( 1, channels ), std::complex< float >() );
}

void Analyser::buildBands( int sampleRate )
{
	bandLo_.clear();
	bandHi_.clear();

	const int   n      = config_.fftSize;
	const int   nyq    = n / 2;
	const float binHz  = (float)sampleRate / (float)n;
	const float fLow   = 20.0f;
	const float fHigh  = std::min( 20000.0f, (float)sampleRate * 0.45f );
	const int   bands  = std::max( 1, config_.bands );

	frame_.bandFreq.assign( bands, 0.0f );

	for( int b = 0; b < bands; ++b )
	{
		// Log spacing. Band edges, not centres, so adjacent bands tile the
		// spectrum exactly once -- overlapping bands double-count energy and
		// make a pink-noise display sag in the middle.
		const float t0 = (float)b / (float)bands;
		const float t1 = (float)( b + 1 ) / (float)bands;
		const float f0 = fLow * std::pow( fHigh / fLow, t0 );
		const float f1 = fLow * std::pow( fHigh / fLow, t1 );

		int lo = (int)std::floor( f0 / binHz );
		int hi = (int)std::ceil( f1 / binHz );
		lo     = std::max( 1, std::min( lo, nyq - 1 ) );   // bin 0 is DC, never wanted
		hi     = std::max( lo + 1, std::min( hi, nyq ) );  // at least one bin per band

		bandLo_.push_back( lo );
		bandHi_.push_back( hi );
		frame_.bandFreq[ b ] = std::sqrt( f0 * f1 );
	}
}

float Analyser::release( float previous, float target, float dt ) const
{
	if( target >= previous )
		return target;   // instant attack
	if( config_.releaseSeconds <= 0.0f || dt <= 0.0f )
		return target;
	const float k = std::exp( -dt / config_.releaseSeconds );
	return target + ( previous - target ) * k;
}

const Frame& Analyser::analyse( const Ring& ring, double time, float dt )
{
	frame_.time = time;
	frame_.live = true;

	const int n  = config_.fftSize;
	const int ch = frame_.channels;
	if( ch <= 0 || n < 2 )
		return frame_;

	if( !ring.peekLatest( scratch_.data(), (size_t)n ) )
	{
		// Not enough audio yet. Let the ballistics fall rather than freezing the
		// last picture: a frozen display during a dropout is indistinguishable
		// from a working one.
		for( size_t i = 0; i < heldBandMag_.size(); ++i )
			frame_.bandMag[ i ] = heldBandMag_[ i ] = release( heldBandMag_[ i ], 0.0f, dt );
		for( size_t i = 0; i < heldRose_.size(); ++i )
			frame_.rose[ i ] = heldRose_[ i ] = release( heldRose_[ i ], 0.0f, dt );
		for( size_t i = 0; i < heldRms_.size(); ++i )
			frame_.chRms[ i ] = heldRms_[ i ] = release( heldRms_[ i ], 0.0f, dt );
		return frame_;
	}

	//-- Speaker unit vectors ------------------------------------------------
	// Recomputed per frame because the layout is a live parameter. It is 2N
	// trig calls; the alternative is a cache that can go stale, which for a
	// user-facing layout dropdown it certainly would.
	std::vector< Vec2 > u( ch );
	std::vector< bool > usable( ch, false );
	for( int c = 0; c < ch; ++c )
	{
		if( c >= frame_.layout.count() )
			continue;
		const ChannelPlacement& p = frame_.layout.channels[ c ];
		if( p.lfe )
			continue;   // no direction to contribute; see Frame.h
		u[ c ]      = Vec2{ std::sin( p.azimuth ), std::cos( p.azimuth ) };
		usable[ c ] = true;
	}

	//-- Sample domain: the field trace and per-channel levels ---------------
	const int   points = config_.fieldPoints;
	const int   first  = n - points;
	std::vector< float > sumSq( ch, 0.0f );
	std::vector< float > peak( ch, 0.0f );

	for( int i = 0; i < points; ++i )
	{
		Vec2 p{};
		for( int c = 0; c < ch; ++c )
		{
			const float s = scratch_[ (size_t)( first + i ) * ch + c ];
			if( !usable[ c ] )
				continue;
			// The whole law, and the only place it is written: amplitude-weighted
			// sum of the unit vectors to each speaker. At a stereo ±45 layout
			// this is (side, mid) and therefore the classic goniometer exactly.
			p.x += s * u[ c ].x;
			p.y += s * u[ c ].y;
		}
		frame_.field[ i ] = p;
	}

	for( int i = 0; i < n; ++i )
	{
		for( int c = 0; c < ch; ++c )
		{
			const float s = scratch_[ (size_t)i * ch + c ];
			sumSq[ c ] += s * s;
			peak[ c ] = std::max( peak[ c ], std::fabs( s ) );
		}
	}

	for( int c = 0; c < ch; ++c )
	{
		frame_.chPeak[ c ] = toDisplay( peak[ c ], config_.floorDb );
		const float rms    = std::sqrt( sumSq[ c ] / (float)n );
		const float target = toDisplay( rms, config_.floorDb );
		frame_.chRms[ c ] = heldRms_[ c ] = release( heldRms_[ c ], target, dt );
	}

	//-- The rose: field energy by angle -------------------------------------
	// This is the distribution of where the *resultant* points, not per-speaker
	// level. A discrete surround feed and a wide stereo mix produce genuinely
	// different pictures here, which is the point. A renderer that wants clean
	// per-speaker lobes draws them from chRms and the layout instead.
	std::vector< float > rose( config_.roseBins, 0.0f );
	const float          binsPerTurn = (float)config_.roseBins / ( 2.0f * kPi );
	for( int i = 0; i < points; ++i )
	{
		const Vec2  p   = frame_.field[ i ];
		const float mag = std::sqrt( p.x * p.x + p.y * p.y );
		if( mag < kMinMag )
			continue;
		float a = std::atan2( p.x, p.y );   // audio convention: 0 ahead, +ve right
		if( a < 0.0f )
			a += 2.0f * kPi;
		int b = (int)( a * binsPerTurn );
		b     = std::max( 0, std::min( config_.roseBins - 1, b ) );
		rose[ b ] += mag * mag;
	}

	float roseMax = 0.0f;
	for( float v : rose )
		roseMax = std::max( roseMax, v );
	for( int i = 0; i < config_.roseBins; ++i )
	{
		const float target = ( roseMax > 0.0f ) ? std::sqrt( rose[ i ] / roseMax ) : 0.0f;
		frame_.rose[ i ] = heldRose_[ i ] = release( heldRose_[ i ], target, dt );
	}

	//-- Frequency domain ----------------------------------------------------
	for( int c = 0; c < ch; ++c )
	{
		std::complex< float >* spec = &spectra_[ (size_t)c * n ];
		for( int i = 0; i < n; ++i )
			spec[ i ] = std::complex< float >( scratch_[ (size_t)i * ch + c ] * window_[ i ], 0.0f );
		fft_.forward( spec );
	}

	const float scale = 2.0f / ( (float)n * windowGain_ );
	const int   bands = (int)bandLo_.size();

	for( int b = 0; b < bands; ++b )
	{
		// One complex value per channel for this band. Summing the complex bins
		// (not the magnitudes) is what keeps the phase relationship between
		// channels alive -- and phase between channels is the entire subject of
		// three of the four plugins.
		std::complex< float > perChannel[ 8 ];
		const int             use = std::min( ch, 8 );
		for( int c = 0; c < use; ++c )
		{
			std::complex< float > acc;
			const std::complex< float >* spec = &spectra_[ (size_t)c * n ];
			for( int k = bandLo_[ b ]; k < bandHi_[ b ]; ++k )
				acc += spec[ k ];
			perChannel[ c ] = acc * scale;
		}

		float total   = 0.0f;   // sum of magnitudes
		float power   = 0.0f;   // sum of |X|^2
		Vec2  fieldXY{};
		std::complex< float > coherent;

		for( int c = 0; c < use; ++c )
		{
			const float mag = std::abs( perChannel[ c ] );
			total += mag;
			power += mag * mag;
			coherent += perChannel[ c ];
			if( usable[ c ] )
			{
				fieldXY.x += mag * u[ c ].x;
				fieldXY.y += mag * u[ c ].y;
			}
		}

		frame_.bandField[ b ] = fieldXY;

		/**
			Width, as one minus the coherent fraction of the band's power:

			    width = 1 - |sum X_c|^2 / ( N * sum |X_c|^2 )

			N identical correlated channels give 0; an anti-phase pair gives 1;
			a hard-panned stereo signal gives 0.5, which is exactly what the
			|S|/(|M|+|S|) meter every mastering engineer already owns reads for
			the same signal. So the generalisation is not a new quantity with a
			familiar name -- it agrees with the familiar one at every landmark
			and keeps working past two channels.
		*/
		const float cohP = std::norm( coherent );
		frame_.bandWidth[ b ] =
			( power > kMinMag * kMinMag )
				? std::min( 1.0f, std::max( 0.0f, 1.0f - cohP / ( (float)use * power ) ) )
				: 0.0f;

		// Correlation is a two-channel idea and is left at zero above two, not
		// generalised. There is no agreed N-channel correlation, and inventing
		// one to fill the field would be putting a number nobody can interpret
		// next to three that they can.
		if( use == 2 )
		{
			const float dA = std::abs( perChannel[ 0 ] );
			const float dB = std::abs( perChannel[ 1 ] );
			frame_.bandCorr[ b ] =
				( dA > kMinMag && dB > kMinMag )
					? std::real( perChannel[ 0 ] * std::conj( perChannel[ 1 ] ) ) / ( dA * dB )
					: 0.0f;
		}
		else
		{
			frame_.bandCorr[ b ] = 0.0f;
		}

		const float target = toDisplay( total / std::max( 1, use ), config_.floorDb );
		frame_.bandMag[ b ] = heldBandMag_[ b ] = release( heldBandMag_[ b ], target, dt );
	}

	return frame_;
}

const Frame& Analyser::analyseHostFFT( const float* bins, int count, double time, float dt )
{
	frame_.time = time;
	frame_.live = false;

	const int bands = (int)frame_.bandMag.size();
	if( bands == 0 || bins == nullptr || count <= 0 )
		return frame_;

	// The host's bins are already magnitudes of a mono sum on a linear frequency
	// axis, with no documented scaling. sqrt is the fleet's existing convention
	// for making them usable, and it is a display choice, not a measurement --
	// nothing here may be compared against the live path's dB figures.
	for( int b = 0; b < bands; ++b )
	{
		const float t   = ( bands > 1 ) ? ( (float)b / (float)( bands - 1 ) ) : 0.0f;
		const int   src = std::min( count - 1, (int)( t * (float)( count - 1 ) + 0.5f ) );
		const float target = std::min( 1.0f, std::sqrt( std::max( 0.0f, bins[ src ] ) ) );
		frame_.bandMag[ b ] = heldBandMag_[ b ] = release( heldBandMag_[ b ], target, dt );

		// Everything directional stays zero. There is nothing in a mono
		// magnitude spectrum to derive it from, and a plausible-looking guess
		// is the one outcome worse than an empty display.
		frame_.bandField[ b ] = Vec2{};
		frame_.bandWidth[ b ] = 0.0f;
		frame_.bandCorr[ b ]  = 0.0f;
	}

	std::fill( frame_.rose.begin(), frame_.rose.end(), 0.0f );
	std::fill( frame_.field.begin(), frame_.field.end(), Vec2{} );
	return frame_;
}

} // namespace spasis
