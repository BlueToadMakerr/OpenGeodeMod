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
            // Requests such as the index stats request can explicitly opt out
            // of the global index override. Check the request parameters
            // themselves rather than relying on the serialized URL, since the
            // intercept can run before WebRequest has appended them to the URL.
            if (req.getUrlParams().count("no_override") > 0) {
                return ListenerResult::Propagate;
            }

            std::string givenUrl = req.getUrl().data();

            // Geode follows redirects for downloads. Depending on where the
            // intercept runs, the request we see can therefore already be
            // pointed at the selected index instead of api.geode-sdk.org.
            // Track the download path independently of the hostname so both
            // the original API request and the redirected index request are
            // captured.
            auto modsPath = std::string("/v1/mods/");
            auto versionsPos = givenUrl.find(modsPath);
            if (versionsPos != std::string::npos) {
                auto modStart = versionsPos + modsPath.size();
                auto versionMarker = givenUrl.find("/versions/", modStart);
                if (versionMarker != std::string::npos && versionMarker > modStart) {
                    auto versionStart = versionMarker + std::string("/versions/").size();
                    auto downloadPos = givenUrl.find("/download", versionStart);
                    if (downloadPos != std::string::npos && downloadPos > versionStart) {
                        auto modID = givenUrl.substr(modStart, versionMarker - modStart);
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

            // Authentication is per-index, just like the rest of the account
            // state. Every intercepted Geode API request should carry the
            // access token for the currently selected index when logged in.
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

            // Only alter the mod-list request. Other /v1/mods endpoints such
            // as /mods/{id}, /mods/updates, and /mods/{id}/logo must not receive
            // a list-only status parameter. The status values are the same
            // variants used by the existing Unverified Mods implementation.
            if (isModListRequest) {
                req.param("status", statusToString(config.status));
            }

            return ListenerResult::Propagate;
        },
        Priority::Stub
    ).leak();
}

} // namespace opengeode
