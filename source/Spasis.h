#pragma once

#include <FFGLSDK.h>

#include "StoatworksAboutParams.h"

#include "analysis/Analyser.h"
#include "audio/Capture.h"
#include "render/Builder.h"
#include "render/Canvas.h"

#include <string>
#include <vector>

namespace spasis
{
// `Display` comes from render/Builder.h. One class, four registrations: the
// analysis and every control are identical across the suite, and only that
// enum picks which of the Frame's fields reaches the builder.

enum ParamID : FFUInt32
{
	PT_INPUT = 0,
	PT_RESCAN,
	PT_LAYOUT,
	PT_CHANNELS,
	PT_SHAPE,
	PT_STYLE,
	PT_GAIN,
	PT_THICKNESS,
	PT_INNER,
	PT_ROTATION,
	PT_PERSIST,
	PT_HUE,
	PT_SATURATION,
	PT_BRIGHTNESS,
	PT_BACKGROUND,
	PT_LOBES,
	PT_FFT,

	PT_ABOUT_FIRST,
	PT_COUNT = PT_ABOUT_FIRST + stoatworks::about::kParamCount
};

/// Bins in the host-FFT fallback buffer. The fleet's existing figure, and the
/// host decides the mapping -- asking for more does not buy more resolution.
constexpr int kHostFFTBins = 64;

class SpasisPlugin : public CFFGLPlugin
{
public:
	explicit SpasisPlugin( Display display );

	FFResult InitGL( const FFGLViewportStruct* viewport ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* gl ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float    GetFloatParameter( unsigned int index ) override;
	char*    GetTextParameter( unsigned int index ) override;
	FFResult SetTextParameter( unsigned int index, const char* value ) override;

	FFResult SetTime( double time ) override;

private:
	void        refreshDevices();
	void        applyInput();
	Layout      chosenLayout( int channels ) const;
	ViewParams  viewParams( float aspect ) const;
	float       frameInterval();

	Display display_;

	float params_[ PT_COUNT ] = { 0.0f };

	Capture   capture_;
	Analyser  analyser_;
	Canvas    canvas_;
	Mesh      mesh_;
	Mesh      graticule_;

	std::vector< std::string > deviceNames_;   ///< index 0 is the host-FFT entry
	int                        openedIndex_ = -1;
	bool                       inputDirty_  = true;

	// Host time. Resolume sends MILLISECONDS and the FFGL header never says so;
	// harnesses send seconds. Consuming it raw runs a thousand times fast in
	// one of the two and no offline test can catch it, so the unit is detected
	// from the first plausible frame delta.
	double hostTime_     = 0.0;
	double lastHostTime_ = -1.0;
	double timeScale_    = 0.0;   ///< 0 until decided; then 1.0 or 0.001
	double elapsed_      = 0.0;
	float  interval_     = 1.0f / 60.0f;

	int lastChannels_ = 0;
	int lastShape_    = -1;
};

} // namespace spasis
