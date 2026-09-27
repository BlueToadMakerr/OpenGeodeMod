#pragma once

#include "Settings.hpp"
#include "InstalledMods.hpp"
#include "PopupSectionUtils.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/loader/Dirs.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/ui/MDTextArea.hpp>
#include <Geode/ui/Popup.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <functional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
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
    bool outdated = false;
    bool updatedFromIndex = false;
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

inline int getIndexUpdateCount(std::string const& indexID) {
    auto const& counts = indexUpdateCounts();
    auto it = counts.find(indexID);
    return it != counts.end() ? it->second : 0;
}

inline int getTotalUpdateCount() {
    int total = 0;
    for (auto const& [_, count] : indexUpdateCounts()) {
        total += count;
    }
    return total;
}

inline std::tuple<int, int, int> parseUpdateVersion(std::string value) {
    if (!value.empty() && value.front() == 'v') value.erase(value.begin());
    int parts[3] = {0, 0, 0};
    size_t start = 0;
    for (int i = 0; i < 3 && start <= value.size(); ++i) {
        auto end = value.find('.', start);
        auto part = value.substr(start, end == std::string::npos ? std::string::npos : end - start);
        size_t numberEnd = 0;
        while (numberEnd < part.size() && std::isdigit(static_cast<unsigned char>(part[numberEnd]))) ++numberEnd;
        if (numberEnd > 0) parts[i] = std::stoi(part.substr(0, numberEnd));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return {parts[0], parts[1], parts[2]};
}

inline bool isNewerUpdateVersion(std::string const& current, std::string const& available) {
    return parseUpdateVersion(available) > parseUpdateVersion(current);
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

inline void appendPendingUpdatedMods() {
    for (auto* mod : Loader::get()->getAllMods()) {
        if (!mod || mod->getID() == "geode.loader" || !wasModUpdatedFromIndex(mod->getID())) continue;

        auto source = getInstalledModSource(mod->getID());
        if (!source || source->version != mod->getVersion().toVString() || source->indexId.empty()) continue;

        auto existing = std::find_if(indexUpdates().begin(), indexUpdates().end(), [&](auto const& update) {
            return update.modID == mod->getID() && update.indexID == source->indexId;
        });

        if (existing != indexUpdates().end()) {
            existing->updatedFromIndex = true;
            continue;
        }

        indexUpdates().push_back({
            source->indexId,
            source->indexName.empty() ? source->indexId : source->indexName,
            mod->getID(),
            mod->getName(),
            source->version,
            source->version,
            !mod->isLoaded(),
            mod->targetsOutdatedVersion().has_value(),
            true
        });
    }
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
    state->callback = [] {};
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

                                auto currentVersion = it->second->getVersion().toVString();
                                if (!isNewerUpdateVersion(currentVersion, version)) continue;

                                auto const loadProblem = it->second->targetsOutdatedVersion();
                                bool outdated = loadProblem.has_value() && loadProblem->type == LoadProblem::Type::Outdated;
                                bool disabled = !it->second->isLoaded();

                                // Avoid duplicate reports from the same index while allowing the
                                // same mod to appear once for every index that has a newer version.
                                auto duplicate = std::find_if(indexUpdates().begin(), indexUpdates().end(), [&](auto const& existing) {
                                    return existing.indexID == entry.id && existing.modID == id;
                                });
                                if (duplicate != indexUpdates().end()) continue;

                                indexUpdates().push_back({
                                    entry.id,
                                    entry.name,
                                    id,
                                    it->second->getName(),
                                    currentVersion,
                                    version,
                                    disabled,
                                    outdated
                                });
                                ++indexUpdateCounts()[entry.id];
                            }
                        }
                    }

                    if (--state->pending == 0) {
                        appendPendingUpdatedMods();
                        indexUpdatesFetchedAt() = std::chrono::steady_clock::now();
                        indexUpdatesLoading() = false;
                        if (state->callback) state->callback();
                    }
                }
            );
        }
    }
}



inline bool& indexSourceInferenceLoading() {
    static bool loading = false;
    return loading;
}

inline std::vector<std::function<void()>>& indexSourceInferenceCallbacks() {
    static std::vector<std::function<void()>> callbacks;
    return callbacks;
}

inline void finishIndexSourceInference() {
    indexSourceInferenceLoading() = false;
    auto callbacks = std::move(indexSourceInferenceCallbacks());
    indexSourceInferenceCallbacks().clear();
    for (auto& callback : callbacks) {
        if (callback) callback();
    }
}

inline void inferOriginalIndexSources(std::function<void()> callback = {}) {
    if (indexSourceInferenceLoading()) {
        if (callback) indexSourceInferenceCallbacks().push_back(std::move(callback));
        return;
    }

    auto updates = indexUpdates();
    std::unordered_set<std::string> modIDs;
    for (auto const& update : updates) {
        if (
            readSetting("mod-source-index-" + update.modID, "").empty() &&
            readSetting("mod-source-candidates-" + update.modID, "").empty()
        )
            modIDs.insert(update.modID);
    }

    if (modIDs.empty()) {
        if (callback) callback();
        return;
    }

    indexSourceInferenceLoading() = true;
    if (callback) indexSourceInferenceCallbacks().push_back(std::move(callback));

    struct State {
        size_t pending = 0;
        std::function<void()> callback;
        std::unordered_map<std::string, std::vector<std::string>> matches;
        std::unordered_map<std::string, std::string> versions;
        std::unordered_set<std::string> checked;
    };
    auto state = std::make_shared<State>();
    state->callback = [] {};

    struct Task {
        async::TaskHolder<web::WebResponse> holder;
    };
    auto tasks = std::make_shared<std::vector<std::shared_ptr<Task>>>();

    auto indexes = getAllIndexes();
    for (auto const& modID : modIDs) {
        auto mod = Loader::get()->getInstalledMod(modID);
        if (!mod) continue;

        auto version = mod->getVersion().toVString();
        state->versions[modID] = version;
        auto checkedKey = "mod-source-checked-version-" + modID;
        if (readSetting(checkedKey, "") == version) {
            state->checked.insert(modID);
            continue;
        }

        // Mark this installed version as checked before starting requests. This
        // prevents repeated popup openings from re-querying the same version,
        // even if the requests are still finishing in the background.
        writeSetting(checkedKey, version);
        deleteSetting("mod-source-index-" + modID);
        deleteSetting("mod-source-version-" + modID);
        deleteSetting("mod-source-candidates-" + modID);

        for (auto const& index : indexes) {
            auto base = index.url;
            if (!base.empty() && base.back() == '/') base.pop_back();

            auto task = std::make_shared<Task>();
            tasks->push_back(task);
            ++state->pending;

            task->holder.spawn(
                web::WebRequest().get(
                    base + fmt::format("/v1/mods/{}/versions/{}", modID, version)
                ),
                [state, tasks, modID, index](web::WebResponse response) {
                    if (response.ok()) {
                        auto json = response.json().unwrapOr(matjson::Value());
                        auto payload = json.contains("payload") ? json["payload"] : json;
                        if (payload.isObject()) {
                            auto returnedVersion = payload["version"].asString().unwrapOr("");
                            if (returnedVersion == state->versions[modID])
                                state->matches[modID].push_back(index.id);
                        }
                    }

                    if (--state->pending == 0) {
                        for (auto const& [id, matches] : state->matches) {
                            if (matches.size() == 1) {
                                writeSetting("mod-source-index-" + id, matches.front());
                                writeSetting("mod-source-version-" + id, state->versions[id]);
                            } else if (matches.size() > 1) {
                                writeSetting("mod-source-candidates-" + id, joinCSV(matches));
                            }
                            writeSetting("mod-source-checked-version-" + id, state->versions[id]);
                        }
                        for (auto const& id : state->checked)
                            writeSetting("mod-source-checked-version-" + id, state->versions[id]);

                        finishIndexSourceInference();
                    }
                }
            );
        }
    }

    if (state->pending == 0) {
        for (auto const& id : state->checked)
            writeSetting("mod-source-checked-version-" + id, state->versions[id]);
        finishIndexSourceInference();
    }
}

inline std::unordered_set<std::string>& completedIndexUpdates() {
    static std::unordered_set<std::string> completed;
    return completed;
}

inline std::string indexUpdateKey(IndexUpdateInfo const& update) {
    return update.indexID + ":" + update.modID + ":" + update.newVersion;
}

inline void showIndexDownloadFailure(
    IndexUpdateInfo const& update,
    int code,
    std::string body,
    std::string error
) {
    auto responseText = body;
    if (responseText.empty()) responseText = error;
    if (responseText.empty()) responseText = "No response body was provided by the server.";

    class DownloadFailurePopup : public Popup {
        std::string m_text;
        bool init(std::string text, std::string title) {
            if (!Popup::init(380.f, 285.f, getPopupBackground())) return false;
            setTitle(title.c_str());
            if (auto close = createGeodeCloseButton()) setCloseButtonSpr(close, .875f);
            auto area = MDTextArea::create(text, {350.f, 220.f}, true);
            if (!area) return false;
            area->getScrollLayer()->m_cutContent = false;
            area->getScrollLayer()->m_disableMovement = false;
            area->getScrollLayer()->setMouseEnabled(true);
            if (auto bg = area->getChildByType<CCScale9Sprite>(0)) bg->setVisible(false);
            m_mainLayer->addChildAtPosition(area, Anchor::Center);
            return true;
        }
    public:
        static DownloadFailurePopup* create(std::string text, std::string title) {
            auto ret = new DownloadFailurePopup();
            if (ret && ret->init(std::move(text), std::move(title))) {
                ret->autorelease();
                return ret;
            }
            delete ret;
            return nullptr;
        }
    };

    auto text = fmt::format(
        "Failed to download {} v{} from {}.\\n\\nServer response:\\n{}",
        update.modName,
        update.newVersion,
        update.indexName,
        responseText
    );
    if (code <= 0 && !error.empty())
        text += fmt::format("\\n\\nError: {}", error);

    Loader::get()->queueInMainThread([text = std::move(text)] {
        if (auto popup = DownloadFailurePopup::create(std::move(text), "Download Failed")) {
            popup->show();
        }
    });
}

inline void downloadIndexUpdate(
    IndexUpdateInfo update,
    std::function<void(bool)> callback = {},
    std::function<void(float)> progressCallback = {}
) {
    static std::unordered_map<std::string, std::shared_ptr<async::TaskHolder<web::WebResponse>>> tasks;

    auto key = indexUpdateKey(update);
    if (tasks.contains(key)) return;

    auto task = std::make_shared<async::TaskHolder<web::WebResponse>>();
    tasks[key] = task;

    auto indexes = getAllIndexes();
    auto base = std::find_if(indexes.begin(), indexes.end(), [&](auto const& entry) {
        return entry.id == update.indexID;
    });
    if (base == indexes.end()) {
        tasks.erase(key);
        if (callback) callback(false);
        return;
    }

    auto url = base->url;
    if (!url.empty() && url.back() == '/') url.pop_back();
    url += fmt::format("/v1/mods/{}/versions/{}/download", update.modID, update.newVersion);

    auto req = web::WebRequest();
    if (progressCallback) {
        req.onProgress([progressCallback = std::move(progressCallback)](web::WebProgress const& progress) mutable {
            if (auto value = progress.downloadProgress()) {
                Loader::get()->queueInMainThread([progressCallback, value = *value] {
                    progressCallback(std::clamp(value, 0.f, 1.f));
                });
            }
        });
    }

    auto token = getAuthAccessTokenForIndex(update.indexID);
    if (!token.empty())
        req.header("Authorization", "Bearer " + token);

    task->spawn(req.get(url), [key, update = std::move(update), callback = std::move(callback)](web::WebResponse response) mutable {
        bool success = false;
        int responseCode = response.code();
        std::string responseBody;
        std::string responseError;

        if (response.ok()) {
            auto data = std::move(response).data();
            auto path = dirs::getModsDir() / (update.modID + ".geode");

            if (auto mod = Loader::get()->getInstalledMod(update.modID)) {
                std::error_code ec;
                std::filesystem::remove(mod->getPackagePath(), ec);
                if (ec) {
                    log::error("Failed to remove old package for {}: {}", update.modID, ec.message());
                } else {
                    success = file::writeBinary(path, data).isOk();
                }
            } else {
                success = file::writeBinary(path, data).isOk();
            }

            if (success) {
                completedIndexUpdates().insert(key);
                writeSetting("mod-source-index-" + update.modID, update.indexID);
                writeSetting("mod-source-version-" + update.modID, update.newVersion);
                setInstalledModSource(update.modID, update.newVersion, update.indexID, true);
            } else {
                responseBody = "The server returned a valid download, but the file could not be saved locally.";
            }
        } else {
            responseBody = std::string(response.data().begin(), response.data().end());
            responseError = std::string(response.errorMessage());
            log::error("Failed to download {} {} from {}: HTTP {}", update.modID, update.newVersion, update.indexName, response.code());
        }

        if (!success) {
            showIndexDownloadFailure(update, responseCode, std::move(responseBody), std::move(responseError));
        }

        tasks.erase(key);
        Loader::get()->queueInMainThread([callback = std::move(callback), success] {
            if (callback) callback(success);
        });
    });
}

} // namespace opengeode
