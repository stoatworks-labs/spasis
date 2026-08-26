/*
 * Stoatworks Labs - About window data for spasis.
 *
 * HAND-WRITTEN, pending registration. Every other repo generates this from
 * stoatworks-backend/scripts/sync-about.py, whose facts come from the website's
 * projects.json. spasis is not in projects.json yet, so this is a placeholder
 * with the same shape -- add the project there and re-run the sync, and this
 * file becomes generated like the rest.
 *
 * `version` here is a fallback read from this repo's own manifest at sync
 * time. Anything with a build step injects the real one at build time and
 * overrides this.
 */
#pragma once

namespace stoatworks::about
{
    inline constexpr auto name = "spasis";
    inline constexpr auto slug = "spasis";
    inline constexpr auto hook = "Sonar-style audio field sources for Resolume";
    inline constexpr auto licence = "MIT";
    inline constexpr auto guide = "https://stoatworks-labs.com/software/spasis/guide/";
    inline constexpr auto page = "https://stoatworks-labs.com/software/spasis/";
    inline constexpr auto repo = "https://github.com/stoatworks-labs/spasis";
    inline constexpr auto versionFallback = "v0.1.0";

    inline constexpr auto org = "Stoatworks Labs";
    inline constexpr auto home = "https://stoatworks-labs.com";
    inline constexpr auto tagline = "Open tools for the people who run the show.";

    /* The canonical funding links, matching FUNDING.yml and the support footer. */
    struct Link { const char* name; const char* url; };
    inline constexpr Link funding[] = {
        { "GitHub Sponsors", "https://github.com/sponsors/stoatworks-labs" },
        { "Ko-fi", "https://ko-fi.com/stoatworkslabs" },
        { "Patreon", "https://patreon.com/StoatworksLabs" },
        { "Liberapay", "https://liberapay.com/stoatworks-labs" },
    };
}
