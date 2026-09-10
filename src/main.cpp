#include "Settings.hpp"
#include "InstalledMods.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>

using namespace geode::prelude;

namespace opengeode {

$on_mod(Loaded) {
    ensurePresetsExist();

    web::WebRequestInterceptEvent().listen(
        [](std::string_view id, web::WebRequest& req) {
            std::string givenUrl = req.getUrl().data();
            auto modsPath = std::string("/v1/mods/");

            // When the native Geode install button is activated from the
            // Versions popup, replace its normal latest-version download with
            // the exact version selected by the user. This intentionally runs
            // before the no_override guard so a native request that carries
            // that flag still honors an explicit version selection.
            auto modStart = givenUrl.find(modsPath);
            if (modStart != std::string::npos) {
                modStart += modsPath.size();
                auto modEnd = givenUrl.find('/', modStart);
                if (modEnd != std::string::npos && modEnd > modStart) {
                    auto modID = givenUrl.substr(modStart, modEnd - modStart);
                    auto endpoint = givenUrl.substr(modEnd);
                    if (endpoint.starts_with("/download")) {
                        auto overrideVersion = takePendingVersionInstall(modID);
                        if (overrideVersion) {
                            auto downloadPath = fmt::format(
                                "/v1/mods/{}/versions/{}/download", modID, *overrideVersion
                            );
                            auto apiPos = givenUrl.find(modsPath);
                            givenUrl.replace(apiPos, givenUrl.size() - apiPos, downloadPath);
                            req.url(givenUrl);
                        }
                    }
                }
            }

            if (req.getUrlParams().count("no_override") > 0) {
                return ListenerResult::Propagate;
            }

            // Track versioned downloads, including those created by the native
            // Geode install flow after a pending-version override.
            auto versionedPos = givenUrl.find(modsPath);
            if (versionedPos != std::string::npos) {
                auto versionedModStart = versionedPos + modsPath.size();
                auto versionMarker = givenUrl.find("/versions/", versionedModStart);
                if (versionMarker != std::string::npos && versionMarker > versionedModStart) {
                    auto versionStart = versionMarker + std::string("/versions/").size();
                    auto downloadPos = givenUrl.find("/download", versionStart);
                    if (downloadPos != std::string::npos && downloadPos > versionStart) {
                        auto modID = givenUrl.substr(versionedModStart, versionMarker - versionedModStart);
                        auto version = givenUrl.substr(versionStart, downloadPos - versionStart);
                        if (!modID.empty() && !version.empty()) {
                            setInstalledModSource(modID, version);
                        }
                    }
                }
            }

            if (!string::contains(givenUrl, "api.geode-sdk.org")) {
                return ListenerResult::Propagate;
            }

            auto const modListPrefix = std::string("https://api.geode-sdk.org/v1/mods");
            bool isModListRequest = false;
            if (givenUrl.starts_with(modListPrefix)) {
                auto next = givenUrl.size() == modListPrefix.size()
                    ? '\0'
                    : givenUrl[modListPrefix.size()];
                isModListRequest = next == '\0' || next == '?';
            }

            auto targetIndex = getIndexUrl();
            givenUrl = string::replace(givenUrl, "https://api.geode-sdk.org", targetIndex);
            req.url(givenUrl);

            auto accessToken = getAuthAccessToken();
            if (!accessToken.empty()) {
                req.header("Authorization", "Bearer " + accessToken);
            }

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

            if (isModListRequest) {
                req.param("status", statusToString(config.status));
            }

            return ListenerResult::Propagate;
        },
        Priority::Stub
    ).leak();
}

} // namespace opengeode
