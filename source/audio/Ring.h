#pragma once

#include <atomic>
#include <cstddef>
#include <cstring>
#include <vector>

/**
	A single-producer single-consumer ring of interleaved audio frames.

	The producer is miniaudio's capture callback, which runs on a real-time
	thread owned by CoreAudio/WASAPI. The consumer is Resolume's render thread
	inside ProcessOpenGL. Neither may block the other and the producer may not
	allocate, so this is a fixed-size buffer written with two atomics and
	nothing else -- no mutex, no condition variable, no growth.

	Overrun is *expected*, not exceptional: the display only ever wants the most
	recent few thousand samples, and if the render thread stalls for a second
	there is nothing useful in the second-old audio. So a full ring drops the
	OLDEST frames rather than refusing the newest, and counts the drops. A
	design that dropped the newest would show a display that lags further behind
	the music the longer it runs and never catches up.
*/
namespace spasis
{
class Ring
{
public:
	/// `frames` is rounded up to a power of two so the wrap is a mask.
	void resize( int channels, size_t frames )
	{
		size_t cap = 1;
		while( cap < frames )
			cap <<= 1;

		channels_ = channels;
		capacity_ = cap;
		mask_     = cap - 1;
		data_.assign( cap * (size_t)channels, 0.0f );
		read_.store( 0, std::memory_order_relaxed );
		write_.store( 0, std::memory_order_relaxed );
		dropped_.store( 0, std::memory_order_relaxed );
	}

	int    channels() const { return channels_; }
	size_t capacity() const { return capacity_; }

	/// Audio thread. Never blocks, never allocates.
	void write( const float* interleaved, size_t frames )
	{
		if( channels_ <= 0 || capacity_ == 0 )
			return;

		// A block bigger than the whole ring can only end as the tail of itself.
		if( frames > capacity_ )
		{
			interleaved += ( frames - capacity_ ) * (size_t)channels_;
			dropped_.fetch_add( frames - capacity_, std::memory_order_relaxed );
			frames = capacity_;
		}

		const size_t w = write_.load( std::memory_order_relaxed );
		const size_t r = read_.load( std::memory_order_acquire );

		// Advance the reader out of the way first. This is the one place the
		// producer touches read_, and it is why this is SPSC-with-overwrite
		// rather than plain SPSC: the consumer must tolerate its read cursor
		// moving under it, which it does by re-reading it inside available().
		const size_t used = w - r;
		if( used + frames > capacity_ )
		{
			const size_t drop = used + frames - capacity_;
			read_.store( r + drop, std::memory_order_release );
			dropped_.fetch_add( drop, std::memory_order_relaxed );
		}

		const size_t start = w & mask_;
		const size_t first = ( start + frames <= capacity_ ) ? frames : capacity_ - start;
		std::memcpy( &data_[ start * (size_t)channels_ ], interleaved,
					 first * (size_t)channels_ * sizeof( float ) );
		if( first < frames )
			std::memcpy( &data_[ 0 ], interleaved + first * (size_t)channels_,
						 ( frames - first ) * (size_t)channels_ * sizeof( float ) );

		write_.store( w + frames, std::memory_order_release );
	}

	size_t available() const
	{
		const size_t w = write_.load( std::memory_order_acquire );
		const size_t r = read_.load( std::memory_order_relaxed );
		return w - r;
	}

	/**
		Render thread. Copies the most recent `frames` frames into `out` without
		consuming them, so consecutive display frames overlap rather than
		tearing the signal into disjoint blocks -- an FFT of disjoint blocks
		flickers, because each transform sees a different arbitrary phase.

		Returns false if the ring does not yet hold that many.
	*/
	bool peekLatest( float* out, size_t frames ) const
	{
		if( channels_ <= 0 || frames == 0 || frames > capacity_ )
			return false;

		const size_t w = write_.load( std::memory_order_acquire );
		const size_t r = read_.load( std::memory_order_relaxed );
		if( w - r < frames )
			return false;

		const size_t start = ( w - frames ) & mask_;
		const size_t first = ( start + frames <= capacity_ ) ? frames : capacity_ - start;
		std::memcpy( out, &data_[ start * (size_t)channels_ ],
					 first * (size_t)channels_ * sizeof( float ) );
		if( first < frames )
			std::memcpy( out + first * (size_t)channels_, &data_[ 0 ],
						 ( frames - first ) * (size_t)channels_ * sizeof( float ) );
		return true;
	}

	/// Render thread. Marks everything up to the write cursor as seen.
	void discardToLatest()
	{
		read_.store( write_.load( std::memory_order_acquire ), std::memory_order_release );
	}

	/**
		Frames overwritten before the reader advanced past them.

		**This is expected to grow continuously and is not an error count.** It
		reads like one, which is why it says so here: the consumer uses
		`peekLatest`, which deliberately does NOT advance the read cursor -- so
		that consecutive display frames overlap and an FFT does not see a
		different arbitrary phase each time. The read cursor therefore only ever
		moves when the producer shoves it, and every frame older than one ring
		is counted here whether or not anybody wanted it.

		The first version of this was reported as "dropped" in the device probe
		and looked alarming: a third of a second of audio "lost" every second on
		a perfectly healthy device. It is a measure of how much history has
		scrolled past, and nothing more.
	*/
	size_t overwritten() const { return dropped_.load( std::memory_order_relaxed ); }

private:
	std::vector< float >   data_;
	int                    channels_ = 0;
	size_t                 capacity_ = 0;
	size_t                 mask_     = 0;
	std::atomic< size_t >  read_{ 0 };
	std::atomic< size_t >  write_{ 0 };
	std::atomic< size_t >  dropped_{ 0 };
};

} // namespace spasis
