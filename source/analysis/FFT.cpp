#include "FFT.h"

#include <cmath>

namespace spasis
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
}

FFT::FFT( int size ) : size_( size )
{
	if( size_ < 2 )
		return;

	reversed_.resize( size_ );
	int bits = 0;
	while( ( 1 << bits ) < size_ )
		++bits;

	for( int i = 0; i < size_; ++i )
	{
		int r = 0;
		for( int b = 0; b < bits; ++b )
			if( i & ( 1 << b ) )
				r |= 1 << ( bits - 1 - b );
		reversed_[ i ] = r;
	}

	// One twiddle per half-spectrum position, shared by every stage: stage s
	// strides through this table by size_/(2*halfSize). Precomputing in double
	// and storing float keeps the accumulated angle error out of the table --
	// computing cos/sin from a running angle instead costs about 20 dB of
	// dynamic range at 8192 points.
	twiddles_.resize( size_ / 2 );
	for( int i = 0; i < size_ / 2; ++i )
	{
		const double a  = -2.0 * kPi * (double)i / (double)size_;
		twiddles_[ i ]  = std::complex< float >( (float)std::cos( a ), (float)std::sin( a ) );
	}
}

void FFT::forward( std::complex< float >* data ) const
{
	if( size_ < 2 )
		return;

	for( int i = 0; i < size_; ++i )
	{
		const int r = reversed_[ i ];
		if( i < r )
			std::swap( data[ i ], data[ r ] );
	}

	for( int half = 1; half < size_; half <<= 1 )
	{
		const int stride = size_ / ( half * 2 );
		for( int start = 0; start < size_; start += half * 2 )
		{
			for( int k = 0; k < half; ++k )
			{
				const std::complex< float > w = twiddles_[ k * stride ];
				const std::complex< float > a = data[ start + k ];
				const std::complex< float > b = data[ start + k + half ] * w;
				data[ start + k ]             = a + b;
				data[ start + k + half ]      = a - b;
			}
		}
	}
}

void hannWindow( std::vector< float >& out, int size )
{
	out.resize( size );
	if( size <= 0 )
		return;
	for( int i = 0; i < size; ++i )
		out[ i ] = 0.5f * ( 1.0f - (float)std::cos( 2.0 * kPi * (double)i / (double)size ) );
}

} // namespace spasis
