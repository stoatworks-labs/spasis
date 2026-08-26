/**
	What the HOST will be told about the parameters.

	Both of the bugs this file exists to catch survived a clean build, thirty
	invariant checks, a twelve-picture contact sheet and the fleet's instantiate
	sweep. Both were found by reading Resolume's own parameter list over REST,
	and both are checkable offline -- the declared parameter table is sitting in
	the SDK base class the whole time, and nothing was looking at it.

	The harness reads the plugin's own `params_`; the host reads the declared
	ParamInfo. A test that only consults the first can never see them disagree.
*/
#include "../../source/Spasis.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace spasis;

namespace
{
/// FindParamInfo is protected on the SDK's manager, so the check has to be
/// inside the class hierarchy. This is the only reason this type exists.
class Probe : public SpasisPlugin
{
public:
	explicit Probe( Display display ) : SpasisPlugin( display ) {}

	int check( const char* label, int& checks )
	{
		int bad = 0;
		for( unsigned int id = 0; id < PT_COUNT; ++id )
		{
			const ParamInfo* info = FindParamInfo( id );
			if( info == nullptr )
				continue;

			// -- the name the host will actually show -------------------------
			// FFGL hands the host a 16-character, non-null-terminated buffer.
			// The SDK stores the full std::string and returns a pointer to it,
			// so nothing plugin-side ever notices the cut.
			++checks;
			if( info->name.size() > 16 )
			{
				std::printf( "  FAIL  %s: \"%s\" is %zu chars; the host shows \"%.16s\"\n",
							 label, info->name.c_str(), info->name.size(), info->name.c_str() );
				++bad;
			}

			// -- the default the host will actually load ----------------------
			// SetParamInfof declares a parameter using GetFloatParameter( id )
			// as its default. Assigning params_ afterwards therefore sets the
			// plugin's idea of the value and never reaches the host, and the
			// control opens at zero in every composition ever saved.
			if( info->dwType != FF_TYPE_TEXT && info->dwType != FF_TYPE_EVENT &&
				info->dwType != FF_TYPE_BUFFER )
			{
				++checks;
				const float declared = info->defaultFloatVal;
				const float held     = GetFloatParameter( id );
				if( declared != held )
				{
					std::printf( "  FAIL  %s: \"%s\" declared default %.3f but the plugin holds %.3f\n",
								 label, info->name.c_str(), declared, held );
					++bad;
				}
			}

			// -- no blank rows in a menu --------------------------------------
			// Resolume renders an empty element name as an empty row. An option
			// parameter padded with spare slots therefore ends in a run of
			// blank lines that reads as a broken plugin. The FFT buffer is
			// exempt: its elements are data and are deliberately unnamed.
			if( info->dwType == FF_TYPE_OPTION )
			{
				for( size_t e = 0; e < info->elements.size(); ++e )
				{
					++checks;
					if( info->elements[ e ].name.empty() )
					{
						std::printf( "  FAIL  %s: \"%s\" element %zu has no name; the host draws a blank row\n",
									 label, info->name.c_str(), e );
						++bad;
						break;   // one report per parameter is enough
					}
				}
			}
		}
		return bad;
	}
};
} // namespace

int checkParams( int& checks )
{
	struct Case
	{
		const char* label;
		Display     display;
	};
	const Case cases[] = {
		{ "Field", Display::Field },
		{ "Rose", Display::Rose },
		{ "Width", Display::Width },
		{ "Balance", Display::Balance },
	};

	int bad = 0;
	for( const Case& c : cases )
	{
		Probe probe( c.display );
		bad += probe.check( c.label, checks );
	}
	if( bad == 0 )
		std::puts( "  ok    every name fits 16 chars, every default is declared, no blank menu rows" );
	return bad;
}
