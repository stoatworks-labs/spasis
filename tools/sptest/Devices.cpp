/**
	Capture against real hardware.

	Everything else in the harness feeds the analyser synthetically. This is the
	only part that opens a device the operating system actually owns, and it
	exists because the three ways capture fails in the field -- a device that is
	gone, a device that opens and returns digital zero, and a device whose
	channel count is not what the layout assumes -- are all invisible to a test
	that writes its own samples into the ring.

	It is not part of `--all`: it depends on what is plugged into this machine,
	so it can only ever report, never assert.
*/
#include "../../source/audio/Capture.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using namespace spasis;

namespace
{
const char* stateName( CaptureState state )
{
	switch( state )
	{
	case CaptureState::Closed:  return "closed";
	case CaptureState::Opening: return "opening";
	case CaptureState::Running: return "running";
	case CaptureState::Silent:  return "SILENT";
	case CaptureState::Failed:  return "FAILED";
	}
	return "?";
}
} // namespace

int listDevices( const std::string& openName, double seconds )
{
	std::puts( "Capture devices this machine offers" );
	const std::vector< DeviceInfo > devices = Capture::enumerate();
	if( devices.empty() )
		std::puts( "  (none -- this machine has no audio input at all)" );
	for( const DeviceInfo& d : devices )
		std::printf( "  %-38s %2d ch%s\n", d.name.c_str(), d.channels, d.isDefault ? "  [default]" : "" );

	if( openName.empty() )
		return 0;

	std::printf( "\nOpening \"%s\" for %.1f s\n", openName.c_str(), seconds );
	Capture capture;
	if( !capture.open( openName == "default" ? "" : openName, 8 ) )
	{
		std::printf( "  FAILED: %s\n", capture.error().c_str() );
		return 1;
	}

	std::printf( "  opened %s: %d channel(s) at %d Hz\n",
				 capture.openedName().c_str(), capture.channels(), capture.sampleRate() );

	const int channels = capture.channels();
	const int frames   = 4096;
	std::vector< float > block( (size_t)frames * channels );
	std::vector< double > peak( channels, 0.0 ), energy( channels, 0.0 );
	long long counted = 0;

	const auto until = std::chrono::steady_clock::now() +
					   std::chrono::milliseconds( (long long)( seconds * 1000 ) );
	while( std::chrono::steady_clock::now() < until )
	{
		std::this_thread::sleep_for( std::chrono::milliseconds( 40 ) );
		if( !capture.ring().peekLatest( block.data(), frames ) )
			continue;
		for( int i = 0; i < frames; ++i )
			for( int c = 0; c < channels; ++c )
			{
				const double s = block[ (size_t)i * channels + c ];
				peak[ c ]      = std::max( peak[ c ], std::fabs( s ) );
				energy[ c ] += s * s;
			}
		counted += frames;
	}

	std::printf( "  state: %s", stateName( capture.state() ) );
	if( capture.silentFor() > 0.0 )
		std::printf( " (silent for %.1f s)", capture.silentFor() );
	// NOT reported as "dropped": see Ring::overwritten. The consumer never
	// advances the read cursor, so this counts history scrolling past, which on
	// a healthy device is most of it.
	std::printf( ", %lld frames read, %zu frames of history scrolled past\n",
				 counted, capture.ring().overwritten() );

	for( int c = 0; c < channels; ++c )
	{
		const double rms = ( counted > 0 ) ? std::sqrt( energy[ c ] / counted ) : 0.0;
		const double db  = ( rms > 1e-9 ) ? 20.0 * std::log10( rms ) : -999.0;
		std::printf( "   ch %d  peak %.4f  rms %7.1f dBFS%s\n",
					 c + 1, peak[ c ], db, ( peak[ c ] == 0.0 ) ? "   <-- digital zero" : "" );
	}

	// A device that opens, runs, and returns bit-exact zero is the failure this
	// whole path is designed around: no permission, an unpatched receiver, or a
	// loopback nothing is routed to all look exactly like this.
	if( capture.state() == CaptureState::Silent )
	{
		// Three different causes, identical from here, and naming only the
		// first sends anyone with a loopback device down the wrong path.
		std::puts( "  This device is running and delivering digital silence. That is one of:" );
		std::puts( "    - the calling app has no microphone permission (macOS)" );
		std::puts( "    - it is a loopback or virtual device with nothing routed to it" );
		std::puts( "    - it is a network receiver (Dante, NDI) that is not subscribed" );
		return 1;
	}
	return 0;
}
