#pragma once

#include "Settings.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/utils/web.hpp>

#include <chrono>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

using namespace geode::prelude;

namespace opengeode {

struct IndexUpdateInfo {
    std::string indexID;
    std::string indexName;
    std::string modID;
    std::string modName;
    std::string currentVersion;
    std::string newVersion;
    bool disabled = false;
};

inline std::unordered_map<std::string, int>& indexUpdateCounts() {
    static std::unordered_map<std::string, int> counts;
    return counts;
}

inline std::vector<IndexUpdateInfo>& indexUpdates() {
    static std::vector<IndexUpdateInfo> updates;
    return updates;
}

inline std::chrono::steady_clock::time_point& indexUpdatesFetchedAt() {
    static std::chrono::steady_clock::time_point time{};
    return time;
}

inline bool& indexUpdatesLoading() {
    static bool loading = false;
    return loading;
}

inline int getIndexUpdateCount(std::string const& id) {
    auto it = indexUpdateCounts().find(id);
    return it == indexUpdateCounts().end() ? -1 : it->second;
}

inline bool hasFreshIndexUpdateCache() {
    if (indexUpdatesFetchedAt() == std::chrono::steady_clock::time_point{} ||
        std::chrono::steady_clock::now() - indexUpdatesFetchedAt() >= std::chrono::minutes(5))
        return false;

    for (auto const& entry : getAllIndexes()) {
        if (!indexUpdateCounts().contains(entry.id)) return false;
    }
    return true;
}

inline void invalidateIndexUpdateCache() {
    indexUpdatesFetchedAt() = {};
    indexUpdateCounts().clear();
    indexUpdates().clear();
}

inline void fetchIndexUpdates(std::function<void()> callback = {}, bool force = false) {
    if (!force && hasFreshIndexUpdateCache()) {
        if (callback) callback();
        return;
    }
    if (indexUpdatesLoading()) return;
    indexUpdatesLoading() = true;

    indexUpdateCounts().clear();
    indexUpdates().clear();

    auto indexes = getAllIndexes();
    auto mods = Loader::get()->getAllMods();
    std::unordered_map<std::string, Mod*> installed;
    std::vector<std::string> ids;
    for (auto* mod : mods) {
        if (!mod || mod->getID() == "geode.loader") continue;
        installed[mod->getID()] = mod;
        ids.push_back(mod->getID());
    }

    if (indexes.empty() || ids.empty()) {
        for (auto const& entry : indexes) indexUpdateCounts()[entry.id] = 0;
        indexUpdatesFetchedAt() = std::chrono::steady_clock::now();
        indexUpdatesLoading() = false;
        if (callback) callback();
        return;
    }

    struct State {
        size_t pending = 0;
        std::function<void()> callback;
        std::unordered_map<std::string, Mod*> installed;
    };
    auto state = std::make_shared<State>();
    state->callback = std::move(callback);
    state->installed = std::move(installed);

    struct Task { async::TaskHolder<web::WebResponse> holder; };
    auto tasks = std::make_shared<std::vector<std::shared_ptr<Task>>>();

    constexpr size_t BATCH_SIZE = 200;
    for (auto const& entry : indexes) {
        indexUpdateCounts()[entry.id] = 0;
        auto base = entry.url;
        if (!base.empty() && base.back() == '/') base.pop_back();

        for (size_t start = 0; start < ids.size(); start += BATCH_SIZE) {
            auto end = std::min(start + BATCH_SIZE, ids.size());
            std::vector<std::string> batch(ids.begin() + start, ids.begin() + end);
            auto task = std::make_shared<Task>();
            tasks->push_back(task);
            ++state->pending;

            auto req = web::WebRequest();
            req.param("platform", GEODE_PLATFORM_SHORT_IDENTIFIER);
            req.param("gd", Loader::get()->getGameVersion());
            req.param("geode", Loader::get()->getVersion().toNonVString());
            if (Loader::get()->isPatchless()) req.param("jitless", "true");
            req.param("ids", ranges::join(batch, ";"));

            task->holder.spawn(
                req.get(base + "/v1/mods/updates"),
                [state, tasks, entry](web::WebResponse response) {
                    if (response.ok()) {
                        auto json = response.json().unwrapOr(matjson::Value());
                        auto payload = json.contains("payload") ? json["payload"] : json;
                        auto updates = payload.isObject() && payload.contains("updates") ? payload["updates"] : payload;
                        if (updates.isArray()) {
                            for (auto const& update : updates) {
                                auto id = update["id"].asString().unwrapOr("");
                                auto version = update["version"].asString().unwrapOr("");
                                auto it = state->installed.find(id);
                                if (id.empty() || version.empty() || it == state->installed.end()) continue;
                                indexUpdates().push_back({
                                    entry.id, entry.name, id, it->second->getName(),
                                    it->second->getVersion().toVString(),
                                    version, !it->second->isOrWillBeEnabled()
                                });
                                ++indexUpdateCounts()[entry.id];
                            }
                        }
                    }

                    if (--state->pending == 0) {
                        indexUpdatesFetchedAt() = std::chrono::steady_clock::now();
                        indexUpdatesLoading() = false;
                        if (state->callback) state->callback();
                    }
                }
            );
        }
    }
}

} // namespace opengeode