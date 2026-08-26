#include "Spasis.h"

#include "Diag.h"
#include "render/GLState.h"

#include <algorithm>
#include <cmath>
#include <string>

using namespace ffglex;

namespace spasis
{
namespace
{
constexpr float kPi = 3.14159265358979323846f;

/// The largest device list the option parameter will show. FFGL option
/// elements are declared once with a fixed count, and a machine with a Dante
/// card can present dozens of inputs -- the list is capped rather than grown
/// because a parameter whose element count changes is a parameter Resolume has
/// to be told about, and saved compositions index into it.
constexpr int kMaxDevices = 24;

void hsbToRgb( float h, float s, float b, float out[ 3 ] )
{
	h = h - std::floor( h );
	const float i = std::floor( h * 6.0f );
	const float f = h * 6.0f - i;
	const float p = b * ( 1.0f - s );
	const float q = b * ( 1.0f - f * s );
	const float t = b * ( 1.0f - ( 1.0f - f ) * s );
	switch( (int)i % 6 )
	{
	case 0: out[ 0 ] = b; out[ 1 ] = t; out[ 2 ] = p; break;
	case 1: out[ 0 ] = q; out[ 1 ] = b; out[ 2 ] = p; break;
	case 2: out[ 0 ] = p; out[ 1 ] = b; out[ 2 ] = t; break;
	case 3: out[ 0 ] = p; out[ 1 ] = q; out[ 2 ] = b; break;
	case 4: out[ 0 ] = t; out[ 1 ] = p; out[ 2 ] = b; break;
	default: out[ 0 ] = b; out[ 1 ] = p; out[ 2 ] = q; break;
	}
}
} // namespace

SpasisPlugin::SpasisPlugin( Display display ) : display_( display )
{
	// A source: no inputs at all. Resolume decides where a plugin appears in
	// its browser from this and nothing else.
	SetMinInputs( 0 );
	SetMaxInputs( 0 );

	refreshDevices();

	SetOptionParamInfo( PT_INPUT, "Audio Input", kMaxDevices + 1, 0.0f );
	for( int i = 0; i <= kMaxDevices; ++i )
	{
		const char* label = ( i < (int)deviceNames_.size() ) ? deviceNames_[ i ].c_str() : "";
		SetParamElementInfo( PT_INPUT, i, label, (float)i );
	}

	SetParamInfo( PT_RESCAN, "Rescan Inputs", FF_TYPE_EVENT, false );

	SetOptionParamInfo( PT_LAYOUT, "Speakers", 6, 0.0f );
	SetParamElementInfo( PT_LAYOUT, 0, "Auto", 0.0f );
	SetParamElementInfo( PT_LAYOUT, 1, "Stereo", 1.0f );
	SetParamElementInfo( PT_LAYOUT, 2, "Quad", 2.0f );
	SetParamElementInfo( PT_LAYOUT, 3, "5.1", 3.0f );
	SetParamElementInfo( PT_LAYOUT, 4, "7.1", 4.0f );
	SetParamElementInfo( PT_LAYOUT, 5, "Ring", 5.0f );

	// FF_TYPE_INTEGER is exempt from the clamp that would otherwise pin a
	// STANDARD default into 0..1 before SetParamRange could widen it.
	SetParamInfo( PT_CHANNELS, "Max Channels", FF_TYPE_INTEGER, 2.0f );
	SetParamRange( PT_CHANNELS, 1.0f, 8.0f );

	SetOptionParamInfo( PT_SHAPE, "Shape", 3, 0.0f );
	SetParamElementInfo( PT_SHAPE, 0, "Circle", 0.0f );
	SetParamElementInfo( PT_SHAPE, 1, "Half Circle", 1.0f );
	SetParamElementInfo( PT_SHAPE, 2, "Raster", 2.0f );

	SetOptionParamInfo( PT_STYLE, "Style", 3, 0.0f );
	SetParamElementInfo( PT_STYLE, 0, "Scope", 0.0f );
	SetParamElementInfo( PT_STYLE, 1, "Bars", 1.0f );
	SetParamElementInfo( PT_STYLE, 2, "Particles", 2.0f );

	SetParamInfof( PT_GAIN, "Gain", FF_TYPE_STANDARD );
	SetParamInfof( PT_THICKNESS, "Thickness", FF_TYPE_STANDARD );
	SetParamInfof( PT_INNER, "Inner Radius", FF_TYPE_STANDARD );
	SetParamInfof( PT_ROTATION, "Rotation", FF_TYPE_STANDARD );
	SetParamInfof( PT_PERSIST, "Persistence", FF_TYPE_STANDARD );
	SetParamInfo( PT_HUE, "Hue", FF_TYPE_HUE, 0.5f );
	SetParamInfo( PT_SATURATION, "Saturation", FF_TYPE_SATURATION, 0.65f );
	SetParamInfo( PT_BRIGHTNESS, "Brightness", FF_TYPE_BRIGHTNESS, 1.0f );
	SetParamInfof( PT_BACKGROUND, "Background", FF_TYPE_STANDARD );

	// Only the Rose plugin has anything to choose here, but the parameter is
	// declared in all four so that the ids line up across the suite. A
	// parameter that exists at a different index in different bundles is how a
	// shared preset file becomes unusable.
	SetOptionParamInfo( PT_LOBES, "Lobes", 2, 0.0f );
	SetParamElementInfo( PT_LOBES, 0, "Field", 0.0f );
	SetParamElementInfo( PT_LOBES, 1, "Speakers", 1.0f );

	SetBufferParamInfo( PT_FFT, "Audio", kHostFFTBins, FF_USAGE_FFT );
	for( int i = 0; i < kHostFFTBins; ++i )
		SetParamElementInfo( PT_FFT, i, "", 0.0f );

	SetParamInfo( PT_ABOUT_FIRST, "About", FF_TYPE_TEXT, stoatworks::about::defaultText() );
	FFUInt32 aboutId = PT_ABOUT_FIRST + 1;
	for( const auto& button : stoatworks::about::buttons() )
		SetParamInfo( aboutId++, button.label, FF_TYPE_EVENT, false );

	params_[ PT_GAIN ]        = 0.5f;
	params_[ PT_THICKNESS ]   = 0.3f;
	params_[ PT_INNER ]       = 0.12f;
	params_[ PT_ROTATION ]    = 0.5f;
	params_[ PT_PERSIST ]     = 0.6f;
	params_[ PT_HUE ]         = 0.5f;
	params_[ PT_SATURATION ]  = 0.65f;
	params_[ PT_BRIGHTNESS ]  = 1.0f;
	params_[ PT_BACKGROUND ]  = 0.0f;
	params_[ PT_CHANNELS ]    = 2.0f;
	params_[ PT_LOBES ]       = ( display == Display::Rose ) ? 0.0f : 0.0f;
}

void SpasisPlugin::refreshDevices()
{
	deviceNames_.clear();

	// Element 0 is always the host. It is the only entry that works with no
	// setup at all, so it is the default -- a plugin that draws nothing until
	// the operator has found the right input in a dropdown is a plugin that
	// gets deleted before it is understood. What it can show is only the
	// spectrum; the three directional plugins say so on screen.
	deviceNames_.push_back( "Resolume (mono)" );

	for( const DeviceInfo& device : Capture::enumerate() )
	{
		if( (int)deviceNames_.size() > kMaxDevices )
			break;
		std::string label = device.name;
		if( label.size() > 30 )
			label = label.substr( 0, 30 );
		deviceNames_.push_back( label );
	}
}

void SpasisPlugin::applyInput()
{
	const int index = std::max( 0, (int)( params_[ PT_INPUT ] + 0.5f ) );
	if( index == openedIndex_ && !inputDirty_ )
		return;

	openedIndex_ = index;
	inputDirty_  = false;
	capture_.close();
	canvas_.clearHistory();
	lastChannels_ = 0;

	if( index == 0 || index >= (int)deviceNames_.size() )
		return;   // the host-FFT path, or an element with no device behind it

	const int maxChannels = std::max( 1, std::min( 8, (int)( params_[ PT_CHANNELS ] + 0.5f ) ) );
	if( !capture_.open( deviceNames_[ index ], maxChannels ) )
		diag::warn( "capture failed: " + capture_.error() );
	else
		diag::info( "capture open: " + capture_.openedName() );
}

Layout SpasisPlugin::chosenLayout( int channels ) const
{
	switch( (int)( params_[ PT_LAYOUT ] + 0.5f ) )
	{
	case 1:  return Layout::stereo();
	case 2:  return Layout::quad();
	case 3:  return Layout::surround51();
	case 4:  return Layout::surround71();
	case 5:  return Layout::ring( channels );
	default: return Layout::forChannelCount( channels );
	}
}

ViewParams SpasisPlugin::viewParams( float aspect ) const
{
	ViewParams view;
	view.geometry = (Geometry)std::min( 2, (int)( params_[ PT_SHAPE ] + 0.5f ) );
	view.style    = (Style)std::min( 2, (int)( params_[ PT_STYLE ] + 0.5f ) );

	// Gain is exponential, spanning -12 to +24 dB. A linear gain control spends
	// four fifths of its travel on settings nobody uses.
	view.gain        = std::pow( 10.0f, ( params_[ PT_GAIN ] * 36.0f - 12.0f ) / 20.0f );
	view.thickness   = params_[ PT_THICKNESS ] * 3.0f;
	view.innerRadius = params_[ PT_INNER ] * 0.6f;
	view.rotation    = ( params_[ PT_ROTATION ] - 0.5f ) * 2.0f * kPi;
	view.aspect      = aspect;
	return view;
}

float SpasisPlugin::frameInterval()
{
	return interval_;
}

FFResult SpasisPlugin::InitGL( const FFGLViewportStruct* viewport )
{
	diag::init();

	if( !canvas_.initGL() )
	{
		diag::error( "the canvas would not initialise; the plugin will draw nothing" );
		DeInitGL();
		return FF_FAIL;
	}
	inputDirty_ = true;

	// Forward the host's viewport. CFFGLPlugin::InitGL does `currentViewport =
	// *vp` with no null check, so passing nullptr here is undefined behaviour
	// -- and the interesting part is how it fails. The optimiser is entitled to
	// assume UB does not happen, so it deletes every statement after the call,
	// including the return. The plugin then compiles clean, links, loads,
	// exports plugMain, logs its way successfully through the whole of InitGL,
	// and fails to instantiate with no message. Do not "simplify" this back.
	return CFFGLPlugin::InitGL( viewport );
}

FFResult SpasisPlugin::DeInitGL()
{
	canvas_.deInitGL();
	capture_.close();
	return FF_SUCCESS;
}

FFResult SpasisPlugin::SetTime( double time )
{
	// Decide the unit once, from the first delta that could only be one of
	// them. Resolume at 50 fps sends steps of 20.0; a harness sends 0.0167.
	if( lastHostTime_ >= 0.0 && timeScale_ == 0.0 )
	{
		const double delta = time - lastHostTime_;
		if( delta > 0.001 && delta < 0.5 )
			timeScale_ = 1.0;
		else if( delta >= 2.0 && delta < 500.0 )
			timeScale_ = 0.001;
	}
	lastHostTime_ = time;
	hostTime_     = time;
	return FF_SUCCESS;
}

FFResult SpasisPlugin::ProcessOpenGL( ProcessOpenGLStruct* gl )
{
	SavedGLState saved;
	saved.Capture();

	//-- Timing --------------------------------------------------------------
	const double scaled = hostTime_ * ( timeScale_ != 0.0 ? timeScale_ : 1.0 );
	if( elapsed_ > 0.0 && scaled > elapsed_ )
	{
		const double delta = scaled - elapsed_;
		// A host that seeks, loops or drops a second of frames must not be
		// allowed to advance the ballistics by that whole gap -- the display
		// would jump to silence and climb back.
		interval_ = (float)std::min( 0.25, delta );
	}
	elapsed_ = ( scaled > 0.0 ) ? scaled : ( elapsed_ + interval_ );

	//-- Audio ---------------------------------------------------------------
	applyInput();

	const bool  hostPath = ( openedIndex_ == 0 ) || capture_.state() != CaptureState::Running;
	const int   channels = hostPath ? 1 : capture_.channels();
	const Layout layout  = chosenLayout( channels );

	if( channels != lastChannels_ || !analyser_.configured() )
	{
		Analyser::Config config;
		analyser_.configure( config, std::max( 1, channels ),
							 hostPath ? 48000 : capture_.sampleRate(), layout );
		lastChannels_ = channels;
		canvas_.clearHistory();
	}

	const Frame* frame = nullptr;
	if( hostPath )
	{
		float             bins[ kHostFFTBins ] = { 0.0f };
		const ParamInfo*  info                 = FindParamInfo( PT_FFT );
		if( info != nullptr )
			for( int i = 0; i < kHostFFTBins && i < (int)info->elements.size(); ++i )
				bins[ i ] = info->elements[ i ].value;
		frame = &analyser_.analyseHostFFT( bins, kHostFFTBins, elapsed_, frameInterval() );
	}
	else
	{
		frame = &analyser_.analyse( capture_.ring(), elapsed_, frameInterval() );
	}

	//-- Build ---------------------------------------------------------------
	const GLsizei width  = (GLsizei)std::max( 1, (int)saved.viewport[ 2 ] );
	const GLsizei height = (GLsizei)std::max( 1, (int)saved.viewport[ 3 ] );
	ViewParams    view   = viewParams( (float)width / (float)height );

	// A geometry change moves every vertex somewhere else, so the persistence
	// from the previous shape is a picture of an instrument that is no longer
	// on screen. Clearing it is the difference between a smooth change and a
	// ghost of the old display fading over the new one.
	const int shapeKey = (int)view.geometry * 8 + (int)view.style;
	if( shapeKey != lastShape_ )
	{
		canvas_.clearHistory();
		lastShape_ = shapeKey;
	}

	switch( display_ )
	{
	case Display::Field:   buildField( *frame, view, mesh_ ); break;
	case Display::Rose:
		if( (int)( params_[ PT_LOBES ] + 0.5f ) == 1 )
			buildSpeakers( *frame, view, mesh_ );
		else
			buildRose( *frame, view, mesh_ );
		break;
	case Display::Width:   buildWidth( *frame, view, mesh_ ); break;
	case Display::Balance: buildBalance( *frame, view, mesh_ ); break;
	}

	//-- Draw ----------------------------------------------------------------
	float rgb[ 3 ];
	hsbToRgb( params_[ PT_HUE ], params_[ PT_SATURATION ], params_[ PT_BRIGHTNESS ], rgb );
	const float foreground[ 4 ] = { rgb[ 0 ], rgb[ 1 ], rgb[ 2 ], 1.0f };
	const float background[ 4 ] = { 0.0f, 0.0f, 0.0f, params_[ PT_BACKGROUND ] };

	// Persistence maps to a per-frame multiplier, referred to a fixed frame
	// rate so that the trail is the same length in SECONDS at 30 fps and at 60.
	// A raw multiplier would make every composition look different on a
	// different machine.
	const float halfLife = 0.02f + params_[ PT_PERSIST ] * params_[ PT_PERSIST ] * 2.0f;
	const float decay    = std::exp( -frameInterval() / halfLife );

	canvas_.render( mesh_, view, gl != nullptr ? gl->HostFBO : 0, width, height,
					decay, foreground, background );

	saved.Restore();
	return FF_SUCCESS;
}

FFResult SpasisPlugin::SetFloatParameter( unsigned int index, float value )
{
	if( index >= PT_COUNT )
		return FF_FAIL;

	if( index >= PT_ABOUT_FIRST )
		return stoatworks::about::handleParam( index - PT_ABOUT_FIRST, value ) ? FF_SUCCESS : FF_FAIL;

	if( index == PT_RESCAN )
	{
		if( value > 0.5f )
		{
			refreshDevices();
			for( int i = 0; i <= kMaxDevices; ++i )
				SetParamElementInfo( PT_INPUT, i,
									 ( i < (int)deviceNames_.size() ) ? deviceNames_[ i ].c_str() : "",
									 (float)i );
			RaiseParamEvent( PT_INPUT, FF_EVENT_FLAG_ELEMENTS );
			inputDirty_ = true;
		}
		params_[ index ] = value;
		return FF_SUCCESS;
	}

	if( index == PT_INPUT || index == PT_CHANNELS )
		inputDirty_ = true;

	params_[ index ] = value;
	return FF_SUCCESS;
}

float SpasisPlugin::GetFloatParameter( unsigned int index )
{
	return ( index < PT_COUNT ) ? params_[ index ] : 0.0f;
}

char* SpasisPlugin::GetTextParameter( unsigned int index )
{
	if( index == PT_ABOUT_FIRST )
	{
		static const std::string line = stoatworks::about::textParam( 0 );
		return const_cast< char* >( line.c_str() );
	}
	return nullptr;
}

FFResult SpasisPlugin::SetTextParameter( unsigned int index, const char* )
{
	// LOAD-BEARING. instantiateGL pushes every declared default back through
	// the setters and destroys the instance the moment one returns FF_FAIL,
	// which is what the SDK's stub does. Without this the plugin cannot be
	// created in any real host while every offline harness still passes.
	if( index == PT_ABOUT_FIRST )
		return FF_SUCCESS;
	return FF_FAIL;
}

} // namespace spasis
