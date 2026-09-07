#include "Settings.hpp"
#include "InstalledMods.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>

#include <regex>

using namespace geode::prelude;

namespace opengeode {

$on_mod(Loaded) {
    ensurePresetsExist();

    web::WebRequestInterceptEvent().listen(
        [](std::string_view id, web::WebRequest& req) {
            if (req.getUrlParams().count("no_override") > 0) {
                return ListenerResult::Propagate;
            }

            std::string givenUrl = req.getUrl().data();
            if (!string::contains(givenUrl, "api.geode-sdk.org")) {
                return ListenerResult::Propagate;
            }

            // Geode downloads a mod through /v1/mods/{id}/versions/{version}/download.
            // Record the selected index before rewriting the request to a custom index.
            static std::regex const downloadPattern(
                R"(/v1/mods/([^/]+)/versions/([^/]+)/download(?:\?|$))"
            );
            std::smatch match;
            if (std::regex_search(givenUrl, match, downloadPattern) && match.size() >= 3) {
                setInstalledModSource(match[1].str(), match[2].str());
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
