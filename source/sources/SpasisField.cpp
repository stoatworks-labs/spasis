/**
	The FF_SOURCE registration for Spasis Field.

	**Not part of spasis_core.** `CFFGLPluginInfo` registers itself from a
	file-scope constructor that nothing references by name, so putting any of
	these four in the shared library would register all four into all four
	bundles -- and making that library STATIC rather than OBJECT would let the
	linker drop the translation unit entirely, giving a bundle that loads,
	exports plugMain, and reports that it contains no plugins.
*/
#include "../Spasis.h"

namespace
{
class SpasisField : public spasis::SpasisPlugin
{
public:
	SpasisField() : SpasisPlugin( spasis::Display::Field )
	{
	}
};
} // namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< SpasisField >,   // Create method
	"SP01",                  // Plugin unique ID, maximum length 4
	"Spasis Field",                  // Plugin name, maximum length 16
	2,                     // API major version
	1,                     // API minor version
	0,                     // Plugin major version
	1,                     // Plugin minor version
	FF_SOURCE,             // Plugin type
	"Goniometer: the stereo and surround field as a trace",
	"spasis FFGL source"
);

extern "C" const char* SpasisFieldBuildStamp()
{
	return "spasis " SPASIS_VERSION " Spasis Field, built " __DATE__ " " __TIME__;
}
