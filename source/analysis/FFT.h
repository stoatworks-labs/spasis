#pragma once

#include <complex>
#include <cstddef>
#include <vector>

/**
	A radix-2 FFT, vendored rather than depended on.

	Four bundles each link their own copy of everything, so a dependency here is
	paid for four times. This is sixty lines, it is exact to within float
	rounding, and `sptest --fft` checks it against a direct DFT -- which is the
	only reason a hand-written transform is defensible at all.
*/
namespace spasis
{
class FFT
{
public:
	/// `size` must be a power of two.
	explicit FFT( int size );

	int size() const { return size_; }

	/// In-place, decimation-in-time, not normalised. The inverse is not
	/// implemented because nothing in spasis needs one -- everything downstream
	/// works on magnitudes and cross-products in the frequency domain.
	void forward( std::complex< float >* data ) const;

private:
	int                                 size_ = 0;
	std::vector< int >                  reversed_;
	std::vector< std::complex< float > > twiddles_;
};

/// Hann window, periodic (not symmetric). The periodic form is the correct one
/// for spectral analysis of a continuous signal: the symmetric form repeats its
/// endpoint and biases every bin by a fraction of a sample.
void hannWindow( std::vector< float >& out, int size );

} // namespace spasis
