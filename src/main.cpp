#include "Settings.hpp"
#include "InstalledMods.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>

using namespace geode::prelude;

namespace opengeode {

$on_mod(Loaded) {
    ensurePresetsExist();
    clearPendingModUpdates();

    web::WebRequestInterceptEvent().listen(
        [](std::string_view id, web::WebRequest& req) {
            std::string givenUrl = req.getUrl().data();
            auto modsPath = std::string("/v1/mods/");

            auto modStart = givenUrl.find(modsPath);
            if (modStart != std::string::npos) {
                modStart += modsPath.size();
                auto modEnd = givenUrl.find('/', modStart);
                if (modEnd != std::string::npos && modEnd > modStart) {
                    auto modID = givenUrl.substr(modStart, modEnd - modStart);
                    auto endpoint = givenUrl.substr(modEnd);
                    auto overrideVersion = pendingVersionInstalls().find(modID);
                    if (overrideVersion != pendingVersionInstalls().end()) {
                        auto version = overrideVersion->second;

                        if (endpoint.starts_with("/versions/") && endpoint.find("/download") == std::string::npos) {
                            givenUrl = givenUrl.substr(0, modEnd) + "/versions/" + version;
                            req.url(givenUrl);
                        }
                        else if (endpoint.starts_with("/download") ||
                            (endpoint.starts_with("/versions/") && endpoint.find("/download") != std::string::npos)) {
                            auto downloadPath = fmt::format("/v1/mods/{}/versions/{}/download", modID, version);
                            auto apiPos = givenUrl.find(modsPath);
                            givenUrl.replace(apiPos, givenUrl.size() - apiPos, downloadPath);
                            req.url(givenUrl);
                            takePendingVersionInstall(modID);
                        }
                    }
                }
            }

            if (req.getUrlParams().count("no_override") > 0) return ListenerResult::Propagate;

            // Geode performs mod installs and updates through the /download
            // endpoint. Record the source before the request is rewritten to
            // the currently selected index so installed mods keep the index
            // they were actually downloaded from.
            if (string::contains(givenUrl, "api.geode-sdk.org/v1/mods/")) {
                auto downloadPrefix = std::string("https://api.geode-sdk.org/v1/mods/");
                if (givenUrl.starts_with(downloadPrefix)) {
                    auto modStart = downloadPrefix.size();
                    auto versionsPos = givenUrl.find("/versions/", modStart);
                    if (versionsPos != std::string::npos && versionsPos > modStart) {
                        auto versionStart = versionsPos + std::string("/versions/").size();
                        auto downloadPos = givenUrl.find("/download", versionStart);
                        if (downloadPos != std::string::npos && downloadPos > versionStart) {
                            auto modID = givenUrl.substr(modStart, versionsPos - modStart);
                            auto version = givenUrl.substr(versionStart, downloadPos - versionStart);
                            if (!modID.empty() && !version.empty()) {
                                setInstalledModSource(modID, version);
                            }
                        }
                    }
                }
            }

            if (!string::contains(givenUrl, "api.geode-sdk.org")) return ListenerResult::Propagate;

            auto const modListPrefix = std::string("https://api.geode-sdk.org/v1/mods");
            bool isModListRequest = false;
            if (givenUrl.starts_with(modListPrefix)) {
                auto next = givenUrl.size() == modListPrefix.size() ? '\0' : givenUrl[modListPrefix.size()];
                isModListRequest = next == '\0' || next == '?';
            }

            auto targetIndex = getIndexUrl();
            givenUrl = string::replace(givenUrl, "https://api.geode-sdk.org", targetIndex);
            req.url(givenUrl);

            auto accessToken = getAuthAccessToken();
            if (!accessToken.empty()) req.header("Authorization", "Bearer " + accessToken);

            auto const& config = getCurrentTabConfig();
            if (!config.platform.empty()) {
                if (req.getUrlParams().count("platforms") > 0) req.param("platforms", config.platform);
                if (req.getUrlParams().count("platform") > 0) req.param("platform", config.platform);
            }
            if (!config.geodeVersion.empty()) {
                if (req.getUrlParams().count("geode") > 0) req.param("geode", config.geodeVersion);
            }
            if (!config.gdVersion.empty()) {
                if (req.getUrlParams().count("gd") > 0) req.param("gd", config.gdVersion);
            }
            if (isModListRequest) req.param("status", statusToString(config.status));

            return ListenerResult::Propagate;
        },
        Priority::First
    ).leak();
}

} // namespace opengeode
