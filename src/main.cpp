#include "Settings.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>

using namespace geode::prelude;

namespace opengeode {

$on_mod(Loaded) {
    ensurePresetsExist();

    web::WebRequestInterceptEvent().listen(
        [](std::string_view id, web::WebRequest& req) {
            std::string givenUrl = req.getUrl().data();

            // Requests made by StatsFetcher intentionally bypass the index
            // override so statistics are fetched from the URL being edited.
            if (givenUrl.find("no_override=1") != std::string::npos) {
                return ListenerResult::Propagate;
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
