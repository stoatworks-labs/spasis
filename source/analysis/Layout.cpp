#include "Frame.h"

#include <cmath>

/**
	Speaker layouts.

	Angles are in the audio convention: degrees, 0 straight ahead, positive to
	the right. The tables below are SMPTE/ITU channel *order*, which is what a
	CoreAudio or WASAPI device hands over for a named surround format -- it is
	NOT what a Dante or MADI feed gives you, where channel 3 is whatever the
	patch says it is. That is why the layout is a user-facing choice with
	`ring()` as the honest fallback, rather than something inferred from the
	channel count and then quietly trusted.
*/
namespace spasis
{
namespace
{
constexpr float kPi = 3.14159265358979323846f;

constexpr float deg( float d )
{
	return d * ( kPi / 180.0f );
}

ChannelPlacement at( float degrees )
{
	ChannelPlacement c;
	c.azimuth = deg( degrees );
	return c;
}

ChannelPlacement lfe()
{
	ChannelPlacement c;
	c.lfe = true;
	return c;
}
} // namespace

Layout Layout::mono()
{
	Layout l;
	l.channels = { at( 0.0f ) };
	return l;
}

Layout Layout::stereo()
{
	// ±45, not ±30. See the comment on Layout in Frame.h -- this is the number
	// that makes the generalised field law reproduce the classic goniometer.
	Layout l;
	l.channels = { at( -45.0f ), at( 45.0f ) };
	return l;
}

Layout Layout::quad()
{
	Layout l;
	l.channels = { at( -45.0f ), at( 45.0f ), at( -135.0f ), at( 135.0f ) };
	return l;
}

Layout Layout::surround51()
{
	// ITU-R BS.775 in SMPTE order: L R C LFE Ls Rs.
	Layout l;
	l.channels = { at( -30.0f ), at( 30.0f ), at( 0.0f ), lfe(), at( -110.0f ), at( 110.0f ) };
	return l;
}

Layout Layout::surround71()
{
	// L R C LFE Lss Rss Lsr Rsr -- the side pair before the rear pair, which is
	// the order everything except Dolby's own documentation uses.
	Layout l;
	l.channels = { at( -30.0f ), at( 30.0f ), at( 0.0f ), lfe(),
				   at( -90.0f ), at( 90.0f ), at( -150.0f ), at( 150.0f ) };
	return l;
}

Layout Layout::ring( int n )
{
	Layout l;
	if( n <= 0 )
		return l;

	// Evenly spaced, first channel straight ahead, going clockwise. No LFE:
	// a ring makes no claim about what any channel carries, and marking one
	// as LFE would be a claim.
	l.channels.reserve( n );
	for( int i = 0; i < n; ++i )
	{
		ChannelPlacement c;
		c.azimuth = ( 2.0f * kPi * (float)i ) / (float)n;
		if( c.azimuth > kPi )
			c.azimuth -= 2.0f * kPi;
		l.channels.push_back( c );
	}
	return l;
}

Layout Layout::forChannelCount( int n )
{
	switch( n )
	{
	case 0:  return Layout();
	case 1:  return mono();
	case 2:  return stereo();
	case 4:  return quad();
	case 6:  return surround51();
	case 8:  return surround71();
	default: return ring( n );
	}
}

float Frame::roseAngle( int i ) const
{
	const int n = roseCount();
	if( n <= 0 )
		return 0.0f;

	// Bin centres, spanning the full circle starting straight ahead. The
	// half-bin offset matters: without it bin 0 straddles 0° and a centre-panned
	// signal splits its energy between the first and last bin, which reads as
	// two lobes.
	const float step = ( 2.0f * kPi ) / (float)n;
	float a          = ( (float)i + 0.5f ) * step;
	if( a > kPi )
		a -= 2.0f * kPi;
	return a;
}

} // namespace spasis
