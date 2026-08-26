/**
	The FF_SOURCE registration for Spasis Width.

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
class SpasisWidth : public spasis::SpasisPlugin
{
public:
	SpasisWidth() : SpasisPlugin( spasis::Display::Width )
	{
	}
};
} // namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< SpasisWidth >,   // Create method
	"SP03",                  // Plugin unique ID, maximum length 4
	"Spasis Width",                  // Plugin name, maximum length 16
	2,                     // API major version
	1,                     // API minor version
	0,                     // Plugin major version
	1,                     // Plugin minor version
	FF_SOURCE,             // Plugin type
	"Stereo width against frequency",
	"spasis FFGL source"
);

extern "C" const char* SpasisWidthBuildStamp()
{
	return "spasis " SPASIS_VERSION " Spasis Width, built " __DATE__ " " __TIME__;
}
