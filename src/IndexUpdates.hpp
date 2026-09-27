#pragma once

#include "Settings.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/utils/web.hpp>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

using namespace geode::prelude;

namespace opengeode {

inline std::unordered_map<std::string, int>& indexUpdateCounts() {
    static std::unordered_map<std::string, int> counts;
    return counts;
}

inline int getIndexUpdateCount(std::string const& id) {
    auto it = indexUpdateCounts().find(id);
    return it == indexUpdateCounts().end() ? -1 : it->second;
}

inline void fetchIndexUpdates(std::function<void()> onUpdated = {}) {
    static async::TaskHolder<web::WebResponse> task;

    auto indexes = getAllIndexes();
    auto mods = Loader::get()->getAllMods();
    std::vector<std::string> modIDs;
    modIDs.reserve(mods.size());
    for (auto* mod : mods) {
        if (mod && mod->getID() != "geode.loader")
            modIDs.push_back(mod->getID());
    }

    if (indexes.empty() || modIDs.empty()) {
        for (auto const& index : indexes)
            indexUpdateCounts()[index.id] = 0;
        if (onUpdated) onUpdated();
        return;
    }

    auto index = std::make_shared<size_t>(0);
    auto requestNext = std::make_shared<std::function<void()>>();
    *requestNext = [indexes = std::move(indexes), modIDs = std::move(modIDs), index, requestNext, onUpdated]() mutable {
        if (*index >= indexes.size()) {
            if (onUpdated) onUpdated();
            return;
        }

        auto const entry = indexes[*index];
        ++*index;

        auto req = web::WebRequest();
        req.param("platform", GEODE_PLATFORM_SHORT_IDENTIFIER);
        req.param("gd", Loader::get()->getGameVersion());
        req.param("geode", Loader::get()->getVersion().toNonVString());
        if (Loader::get()->isPatchless())
            req.param("jitless", "true");
        req.param("ids", ranges::join(modIDs, ";"));

        auto token = readSetting("auth-access-" + entry.id, "");
        if (!token.empty())
            req.header("Authorization", "Bearer " + token);

        auto url = entry.url;
        if (!url.empty() && url.back() == '/') url.pop_back();

        task.spawn(
            req.get(url + "/v1/mods/updates"),
            [entry, index, requestNext, onUpdated](web::WebResponse response) {
                int count = 0;
                if (response.ok()) {
                    auto json = response.json().unwrapOr(matjson::Value());
                    auto payload = json.contains("payload") ? json["payload"] : json;
                    if (payload.isObject() && payload.contains("updates")) {
                        count = static_cast<int>(payload["updates"].size());
                    }
                    else if (payload.isArray()) {
                        count = static_cast<int>(payload.size());
                    }
                }

                indexUpdateCounts()[entry.id] = count;
                if (onUpdated) onUpdated();
                (*requestNext)();
            }
        );
    };

    (*requestNext)();
}

} // namespace opengeode
