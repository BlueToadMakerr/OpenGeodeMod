#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/utils/web.hpp>

using namespace geode::prelude;

namespace opengeode {

struct StatsFetcher {
    CCLabelBMFont* label = nullptr;
    async::TaskHolder<web::WebResponse> listener;
    async::TaskHolder<web::WebResponse> opengeodeListener;

    void fetch(std::string url) {
        if (!label) return;
        label->setString("Loading info...");

        if (!url.empty() && url.back() == '/') url.pop_back();

        auto req = web::WebRequest();
        req.param("no_override", "1");

        listener.spawn(
            req.get(url + "/v1/stats"),
            [this](web::WebResponse res) {
                if (res.ok()) {
                    auto json = res.json().unwrapOr(matjson::Value());
                    if (json.contains("payload")) {
                        auto payload = json["payload"];
                        int totalMods = payload["total_mod_count"].asInt().unwrapOr(0);
                        int totalDownloads = payload["total_mod_downloads"].asInt().unwrapOr(0);
                        label->setString(fmt::format("Mods: {} | Downloads: {}", totalMods, totalDownloads).c_str());

                        auto capabilityReq = web::WebRequest();
                        capabilityReq.param("no_override", "1");
                        opengeodeListener.spawn(
                            capabilityReq.get(url + "/OpenGeode"),
                            [this](web::WebResponse capability) {
                                bool enabled = false;
                                if (capability.ok()) {
                                    auto capabilityJson = capability.json().unwrapOr(matjson::Value());
                                    enabled = capabilityJson["enabled"].asBool().unwrapOr(false);
                                }
                                auto current = std::string(label->getString());
                                label->setString(
                                    fmt::format("{} | OpenGeode: {}", current, enabled ? "Enabled" : "Disabled").c_str()
                                );
                            }
                        );
                        return;
                    }
                }

                label->setString("Could not retrieve info from endpoint.");
            }
        );
    }
};

} // namespace opengeode
