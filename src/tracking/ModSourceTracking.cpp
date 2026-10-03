#include "ModSourceTracking.hpp"
#include "../InstalledMods.hpp"
#include <Geode/utils/web.hpp>
using namespace geode::prelude;
namespace opengeode {
    void trackModDownloadSource(std::string const & url, bool throughOpenGeode) {
        // Track the actual download URL regardless of whether Geode is currently
        // using the stock API URL or a custom index URL.
        auto modsPath = std::string("/v1/mods/");
        auto modStart = url.find(modsPath);
        if (modStart == std::string::npos)
            return;
        modStart += modsPath.size();
        auto versionsPos = url.find("/versions/", modStart);
        if (versionsPos == std::string::npos || versionsPos <= modStart)
            return;
        auto versionStart = versionsPos + std::string("/versions/").size();
        auto downloadPos = url.find("/download", versionStart);
        if (downloadPos == std::string::npos || downloadPos <= versionStart)
            return;
        auto modID = url.substr(modStart, versionsPos - modStart);
        auto version = url.substr(versionStart, downloadPos - versionStart);
        if (modID.empty() || version.empty()) return;
        auto installed = getInstalledModSource(modID);
        bool changed = installed && installed->version != version;
        if (throughOpenGeode) {
            auto indexId = getActiveIndexId();
            auto currentUrl = getIndexUrl();
            for (auto const & entry: getAllIndexes()) {
                if (entry.url == currentUrl) {
                    indexId = entry.id;
                    break;
                }
            }
            setInstalledModSource(modID, version, indexId, changed);
        } else {
            setInstalledModSource(modID, version);
            if (changed)
                markModUpdatedFromGeode(modID);
        }
    }
}
// namespace opengeode
