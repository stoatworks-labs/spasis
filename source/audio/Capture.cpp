#include "Capture.h"

// miniaudio is compiled here and only here. The implementation macro must
// precede the include, and this is the single translation unit that defines it.
//
// MA_NO_DEVICE_IO would defeat the purpose; what we can drop is everything to
// do with playback decoding and resource management, none of which a capture-
// only plugin touches. Every megabyte saved here is a megabyte off four
// bundles, because each plugin links its own copy.
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#include "miniaudio.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace spasis
{
namespace
{
/// Seconds of ring to keep. Generous: the longest analysis window is 8192
/// frames, and a host that stalls for a quarter of a second must not leave a
/// gap the analyser can see.
constexpr double kRingSeconds = 1.0;

/// How long digital silence must persist before it is reported as Silent
/// rather than as a quiet passage. Long enough to survive a track change,
/// short enough that a missing permission is obvious while the user is still
/// looking at the plugin.
constexpr double kSilenceTimeout = 3.0;

std::string hexOf( const ma_device_id& id )
{
	const unsigned char* p = reinterpret_cast< const unsigned char* >( &id );
	char buf[ 2 * 16 + 1 ]                                              = { 0 };
	for( size_t i = 0; i < 16; ++i )
		std::snprintf( buf + i * 2, 3, "%02x", p[ i ] );
	return std::string( buf );
}
} // namespace

struct Capture::Impl
{
	ma_context context{};
	ma_device  device{};
	bool       contextReady = false;
	bool       deviceReady  = false;

	Ring ring;

	std::atomic< int >  state{ (int)CaptureState::Closed };
	std::atomic< int >  channels{ 0 };
	std::atomic< int >  sampleRate{ 48000 };
	std::atomic< int >  nativeChannels{ 0 };

	// Silence tracking. Written by the audio thread, read by the render thread;
	// a double is not atomic on every platform we build for, so this is kept as
	// a frame count and converted on read.
	std::atomic< long long > silentFrames{ 0 };

	std::mutex  errorLock;
	std::string error;
	std::string openedName;

	void setError( std::string message )
	{
		std::lock_guard< std::mutex > guard( errorLock );
		error = std::move( message );
		state.store( (int)CaptureState::Failed );
	}

	static void dataCallback( ma_device* device, void* /*output*/, const void* input, ma_uint32 frameCount )
	{
		Impl* self = static_cast< Impl* >( device->pUserData );
		if( self == nullptr || input == nullptr || frameCount == 0 )
			return;

		const float* src    = static_cast< const float* >( input );
		const int    native = (int)device->capture.channels;
		const int    want   = self->ring.channels();
		if( native <= 0 || want <= 0 )
			return;

		// Silence detection runs on the raw device buffer, before any slicing,
		// so a layout that happens to exclude the only live channel does not
		// get reported as a dead device.
		bool anySignal = false;
		for( ma_uint32 i = 0, n = frameCount * (ma_uint32)native; i < n; ++i )
		{
			if( src[ i ] != 0.0f )
			{
				anySignal = true;
				break;
			}
		}
		if( anySignal )
			self->silentFrames.store( 0, std::memory_order_relaxed );
		else
			self->silentFrames.fetch_add( (long long)frameCount, std::memory_order_relaxed );

		if( native == want )
		{
			self->ring.write( src, frameCount );
			return;
		}

		// Take the first `want` channels rather than letting miniaudio convert.
		// Conversion would MIX a 64-channel Dante feed down to two, which is a
		// different signal from the one the user asked to look at -- and it
		// would look plausible, which is worse.
		//
		// A stack buffer keeps the audio thread allocation-free; miniaudio's
		// period is well under this even at 10 ms and 48 kHz.
		constexpr ma_uint32 kChunk = 1024;
		float               scratch[ kChunk * 8 ];
		const int           take = std::min( want, 8 );

		for( ma_uint32 done = 0; done < frameCount; done += kChunk )
		{
			const ma_uint32 n = std::min( kChunk, frameCount - done );
			for( ma_uint32 f = 0; f < n; ++f )
				for( int c = 0; c < take; ++c )
					scratch[ f * (ma_uint32)take + c ] = src[ ( (size_t)( done + f ) * native ) + c ];
			self->ring.write( scratch, n );
		}
	}
};

Capture::Capture() : impl_( new Impl )
{
}

Capture::~Capture()
{
	close();
}

std::vector< DeviceInfo > Capture::enumerate()
{
	std::vector< DeviceInfo > out;

	ma_context context;
	if( ma_context_init( nullptr, 0, nullptr, &context ) != MA_SUCCESS )
		return out;

	ma_device_info* playback  = nullptr;
	ma_uint32       playbackN = 0;
	ma_device_info* capture   = nullptr;
	ma_uint32       captureN  = 0;
	if( ma_context_get_devices( &context, &playback, &playbackN, &capture, &captureN ) == MA_SUCCESS )
	{
		out.reserve( captureN );
		for( ma_uint32 i = 0; i < captureN; ++i )
		{
			DeviceInfo info;
			info.name      = capture[ i ].name;
			info.id        = hexOf( capture[ i ].id );
			info.isDefault = capture[ i ].isDefault != 0;

			// nativeDataFormatCount is only populated after ma_context_get_device_info
			// on some backends, and the channel count is the one field worth the
			// extra round trip -- it is what tells the user whether the device can
			// carry their surround feed at all.
			ma_device_info detail;
			if( ma_context_get_device_info( &context, ma_device_type_capture, &capture[ i ].id, &detail ) == MA_SUCCESS )
			{
				ma_uint32 best = 0;
				for( ma_uint32 f = 0; f < detail.nativeDataFormatCount; ++f )
					best = std::max( best, detail.nativeDataFormats[ f ].channels );
				info.channels = (int)best;
			}
			out.push_back( std::move( info ) );
		}
	}

	ma_context_uninit( &context );
	return out;
}

bool Capture::open( const std::string& deviceName, int maxChannels )
{
	close();

	Impl& impl = *impl_;
	impl.state.store( (int)CaptureState::Opening );

	if( ma_context_init( nullptr, 0, nullptr, &impl.context ) != MA_SUCCESS )
	{
		impl.setError( "Could not start the audio backend" );
		return false;
	}
	impl.contextReady = true;

	// Resolve the name to a device id. An empty name means the default, which
	// miniaudio expresses as a null pointer rather than a distinguished id.
	ma_device_id  chosenId{};
	ma_device_id* chosenPtr = nullptr;
	std::string   chosenName;
	int           deviceChannels = 0;

	ma_device_info* playback  = nullptr;
	ma_uint32       playbackN = 0;
	ma_device_info* capture   = nullptr;
	ma_uint32       captureN  = 0;
	if( ma_context_get_devices( &impl.context, &playback, &playbackN, &capture, &captureN ) != MA_SUCCESS )
	{
		impl.setError( "Could not list audio inputs" );
		return false;
	}

	for( ma_uint32 i = 0; i < captureN; ++i )
	{
		const bool wanted = deviceName.empty() ? ( capture[ i ].isDefault != 0 )
											   : ( deviceName == capture[ i ].name );
		if( !wanted )
			continue;
		chosenId   = capture[ i ].id;
		chosenPtr  = &chosenId;
		chosenName = capture[ i ].name;
		break;
	}

	if( chosenPtr == nullptr && !deviceName.empty() )
	{
		// Named device is gone. Failing here rather than falling back to the
		// default is the point: a composition saved against a Dante feed that
		// silently reopens on the laptop microphone is worse than one that says
		// the device is missing.
		impl.setError( "Audio input \"" + deviceName + "\" is not connected" );
		return false;
	}

	ma_device_config config      = ma_device_config_init( ma_device_type_capture );
	config.capture.pDeviceID     = chosenPtr;
	config.capture.format        = ma_format_f32;
	config.capture.channels      = 0;    // native: we slice, we never let it mix
	config.capture.shareMode     = ma_share_mode_shared;
	config.sampleRate            = 0;    // native
	config.dataCallback          = &Impl::dataCallback;
	config.pUserData             = &impl;

	if( ma_device_init( &impl.context, &config, &impl.device ) != MA_SUCCESS )
	{
		impl.setError( chosenName.empty() ? "No audio input available"
										  : ( "Could not open \"" + chosenName + "\"" ) );
		return false;
	}
	impl.deviceReady = true;

	const int native = (int)impl.device.capture.channels;
	const int rate   = (int)impl.device.sampleRate;
	deviceChannels   = std::max( 1, std::min( native, std::max( 1, maxChannels ) ) );

	// Eight is the slice ceiling in the audio callback's stack scratch buffer.
	deviceChannels = std::min( deviceChannels, 8 );

	impl.nativeChannels.store( native );
	impl.channels.store( deviceChannels );
	impl.sampleRate.store( rate );
	impl.ring.resize( deviceChannels, (size_t)( kRingSeconds * rate ) );
	impl.silentFrames.store( 0 );

	if( ma_device_start( &impl.device ) != MA_SUCCESS )
	{
		impl.setError( "Could not start \"" + chosenName + "\"" );
		return false;
	}

	if( chosenName.empty() )
		chosenName = "Default input";
	{
		std::lock_guard< std::mutex > guard( impl.errorLock );
		impl.openedName = chosenName;
		impl.error.clear();
	}
	impl.state.store( (int)CaptureState::Running );
	return true;
}

void Capture::close()
{
	Impl& impl = *impl_;
	if( impl.deviceReady )
	{
		ma_device_uninit( &impl.device );
		impl.deviceReady = false;
	}
	if( impl.contextReady )
	{
		ma_context_uninit( &impl.context );
		impl.contextReady = false;
	}
	impl.state.store( (int)CaptureState::Closed );
	impl.channels.store( 0 );
	impl.silentFrames.store( 0 );
	std::lock_guard< std::mutex > guard( impl.errorLock );
	impl.openedName.clear();
}

CaptureState Capture::state() const
{
	const CaptureState s = (CaptureState)impl_->state.load();
	if( s == CaptureState::Running && silentFor() >= kSilenceTimeout )
		return CaptureState::Silent;
	return s;
}

std::string Capture::error() const
{
	std::lock_guard< std::mutex > guard( impl_->errorLock );
	return impl_->error;
}

std::string Capture::openedName() const
{
	std::lock_guard< std::mutex > guard( impl_->errorLock );
	return impl_->openedName;
}

int Capture::channels() const
{
	return impl_->channels.load();
}

int Capture::sampleRate() const
{
	return impl_->sampleRate.load();
}

Ring& Capture::ring()
{
	return impl_->ring;
}

const Ring& Capture::ring() const
{
	return impl_->ring;
}

double Capture::silentFor() const
{
	const long long frames = impl_->silentFrames.load( std::memory_order_relaxed );
	const int       rate   = impl_->sampleRate.load();
	if( rate <= 0 )
		return 0.0;
	return (double)frames / (double)rate;
}

} // namespace spasis
