#include "Settings.hpp"
#include "InstalledMods.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>

using namespace geode::prelude;

namespace opengeode {

namespace {

std::string sourceMismatchKey(std::string const& modID, std::string const& version, std::string const& indexID) {
    return "mod-source-approved-" + modID + "-" + version + "-" + indexID;
}

void showSourceMismatchPopup(
    std::string const& modID,
    std::string const& version,
    std::string const& modName,
    std::string const& installedIndex,
    std::string const& newIndex
) {
    createQuickPopup(
        "Download From New Index?",
        fmt::format(
            "<cy>{}</c> is installed from <cg>{}</c>, but this update is from <co>{}</c>.\n\n"
            "Download the update from the new index?\n\n"
            "If you choose <cg>Download</c>, please <cy>retry the download</c> afterward.",
            modName,
            installedIndex,
            newIndex
        ),
        "Cancel",
        "Download",
        [modID, version](FLAlertLayer*, bool confirmed) {
            if (confirmed) {
                writeSetting(sourceMismatchKey(modID, version, getActiveIndexId()), "1");
            }
        }
    );
}

} // namespace

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

            // Check source mismatches before honoring no_override. Some normal
            // Geode download requests carry no_override, but they still need to
            // pass through the source-mismatch check.
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
                            auto installedSource = getInstalledModSource(modID);
                            auto activeIndex = getActiveIndexId();
                            if (installedSource &&
                                !installedSource->indexId.empty() &&
                                installedSource->indexId != activeIndex &&
                                installedSource->version != version) {
                                auto approval = sourceMismatchKey(modID, version, activeIndex);
                                if (readSetting(approval, "") != "1") {
                                    std::string installedName = installedSource->indexName.empty() ? installedSource->indexId : installedSource->indexName;
                                    std::string activeName = activeIndex;
                                    for (auto const& index : getAllIndexes()) {
                                        if (index.id == activeIndex) {
                                            activeName = index.name.empty() ? index.id : index.name;
                                            break;
                                        }
                                    }
                                    auto mod = Loader::get()->getInstalledMod(modID);
                                    auto modName = mod ? std::string(mod->getName()) : modID;
                                    showSourceMismatchPopup(modID, version, modName, installedName, activeName);

                                    // Keep the request in the normal web pipeline, but
                                    // replace it with a deliberately invalid URL so the
                                    // original download cannot happen. This mirrors the
                                    // previous data: URL approach without relying on the
                                    // data: scheme being accepted by the web layer.
                                    req.url("https://opengeode.invalid/source-mismatch");
                                    return ListenerResult::Propagate;
                                }
                            }

                            if (installedSource) {
                                setInstalledModSource(modID, version, activeIndex, true);
                            } else {
                                setInstalledModSource(modID, version);
                            }
                        }
                    }
                }
            }

            if (req.getUrlParams().count("no_override") > 0) return ListenerResult::Propagate;

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
        Priority::Stub
    ).leak();
}

} // namespace opengeode