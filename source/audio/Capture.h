#pragma once

#include "Ring.h"

#include <memory>
#include <string>
#include <vector>

/**
	Live audio capture.

	miniaudio is deliberately not visible in this header. It is a four-megabyte
	single-header library, and putting it in the interface would drag it into
	every translation unit that wants to ask whether audio is running. It is
	compiled in exactly one place, `Capture.cpp`.

	**Devices are remembered by name, never by index.** A device index is a
	position in a list that changes whenever anything is plugged in, and a saved
	Resolume composition that reopens on index 3 will happily start metering
	somebody's webcam microphone. Names collide only between identical units,
	which is a far better failure than silently metering the wrong thing.
*/
namespace spasis
{
struct DeviceInfo
{
	std::string id;        ///< opaque backend id, valid only until the next enumerate()
	std::string name;      ///< what the user sees, and what gets saved
	int         channels = 0;
	bool        isDefault = false;
};

/**
	Why capture can be running and still show nothing.

	`Silent` is the one that matters and it is the reason this enum exists at
	all. On macOS a host without microphone permission does not get an error
	when it opens an input device -- it gets a working device that returns
	digital zero for ever. The same is true of an unpatched Dante receiver and
	of a BlackHole device nothing is routed to. All three look identical to the
	code and all three look like "the plugin is broken" to the user, so spasis
	detects the condition (open, running, and bit-exactly zero for several
	seconds) and says so on the display rather than drawing an empty circle.
*/
enum class CaptureState
{
	Closed,
	Opening,
	Running,
	Silent,
	Failed,
};

class Capture
{
public:
	Capture();
	~Capture();

	Capture( const Capture& )            = delete;
	Capture& operator=( const Capture& ) = delete;

	/// Capture devices currently present. Safe to call from the render thread,
	/// but it talks to the OS -- call it when the user opens the menu, not
	/// every frame.
	static std::vector< DeviceInfo > enumerate();

	/**
		Open a device by name. An empty name opens the system default.

		`maxChannels` caps what is requested; a Dante virtual soundcard can
		offer 64, and analysing 64 channels to draw one rose is waste. Asking
		for fewer than the device has takes the FIRST n channels, which is the
		only defensible guess -- there is no way to know which of 64 Dante
		channels the user meant.
	*/
	bool open( const std::string& deviceName, int maxChannels );
	void close();

	CaptureState state() const;
	std::string  error() const;      ///< human-readable, empty unless Failed
	std::string  openedName() const;
	int          channels() const;
	int          sampleRate() const;

	/// The audio thread writes here; the render thread reads it.
	Ring&       ring();
	const Ring& ring() const;

	/// Seconds of continuous digital silence, or 0 while signal is present.
	double silentFor() const;

private:
	struct Impl;
	std::unique_ptr< Impl > impl_;
};

} // namespace spasis
