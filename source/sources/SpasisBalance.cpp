/**
	The FF_SOURCE registration for Spasis Balance.

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
class SpasisBalance : public spasis::SpasisPlugin
{
public:
	SpasisBalance() : SpasisPlugin( spasis::Display::Balance )
	{
	}
};
} // namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< SpasisBalance >,   // Create method
	"SP04",                  // Plugin unique ID, maximum length 4
	"Spasis Balance",                  // Plugin name, maximum length 16
	2,                     // API major version
	1,                     // API minor version
	0,                     // Plugin major version
	1,                     // Plugin minor version
	FF_SOURCE,             // Plugin type
	"Tonal balance: the spectrum, in three geometries",
	"spasis FFGL source"
);

extern "C" const char* SpasisBalanceBuildStamp()
{
	return "spasis " SPASIS_VERSION " Spasis Balance, built " __DATE__ " " __TIME__;
}
