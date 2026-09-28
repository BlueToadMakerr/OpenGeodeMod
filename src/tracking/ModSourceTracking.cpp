#include "ModSourceTracking.hpp"
#include "../InstalledMods.hpp"

#include <Geode/utils/web.hpp>

using namespace geode::prelude;

namespace opengeode {

void trackModDownloadSource(std::string const& url) {
    auto downloadPrefix = std::string("https://api.geode-sdk.org/v1/mods/");
    if (!url.starts_with(downloadPrefix)) return;

    auto modStart = downloadPrefix.size();
    auto versionsPos = url.find("/versions/", modStart);
    if (versionsPos == std::string::npos || versionsPos <= modStart) return;

    auto versionStart = versionsPos + std::string("/versions/").size();
    auto downloadPos = url.find("/download", versionStart);
    if (downloadPos == std::string::npos || downloadPos <= versionStart) return;

    auto modID = url.substr(modStart, versionsPos - modStart);
    auto version = url.substr(versionStart, downloadPos - versionStart);
    if (!modID.empty() && !version.empty()) {
        setInstalledModSource(modID, version);
    }
}

} // namespace opengeode
