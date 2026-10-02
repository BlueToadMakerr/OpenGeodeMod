#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/utils/web.hpp>

using namespace geode::prelude;

namespace opengeode {

struct StatsFetcher {
    CCLabelBMFont* label = nullptr;
    CCLabelBMFont* opengeodeLabel = nullptr;
    async::TaskHolder<web::WebResponse> listener;
    async::TaskHolder<web::WebResponse> opengeodeListener;

    void fetch(std::string url) {
        if (!label) return;
        label->setString("Loading info...");
        if (opengeodeLabel) opengeodeLabel->setVisible(false);
        if (!url.empty() && url.back() == '/') url.pop_back();

        auto req = web::WebRequest();
        req.param("no_override", "1");
        listener.spawn(req.get(url + "/v1/stats"), [this, url](web::WebResponse res) {
            if (res.ok()) {
                auto json = res.json().unwrapOr(matjson::Value());
                if (json.contains("payload")) {
                    auto payload = json["payload"];
                    int totalMods = payload["total_mod_count"].asInt().unwrapOr(0);
                    int totalDownloads = payload["total_mod_downloads"].asInt().unwrapOr(0);
                    label->setString(fmt::format("Mods: {} | Downloads: {}", totalMods, totalDownloads).c_str());
                    if (opengeodeLabel) opengeodeLabel->setVisible(true);

                    auto capabilityReq = web::WebRequest();
                    capabilityReq.param("no_override", "1");
                    opengeodeListener.spawn(capabilityReq.get(url + "/OpenGeode"), [this](web::WebResponse capability) {
                        bool enabled = capability.ok() && capability.json().unwrapOr(matjson::Value())["enabled"].asBool().unwrapOr(false);
                        if (opengeodeLabel) opengeodeLabel->setString(fmt::format("Open Geode: {}", enabled ? "Enabled" : "Disabled").c_str());
                    });
                    return;
                }
            }
            label->setString("Could not retrieve info from endpoint.");
            if (opengeodeLabel) opengeodeLabel->setVisible(false);
        });
    }
};

} // namespace opengeode
