#pragma once

#include "Settings.hpp"

#include <Geode/Geode.hpp>
#include <cctype>
#include <optional>
#include <string>
#include <unordered_map>

namespace opengeode {

struct InstalledModSource {
    std::string indexId;
    std::string indexName;
    std::string indexUrl;
    std::string version;
};

inline std::string installedModKey(std::string modID) {
    for (auto& c : modID) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') {
            c = '_';
        }
    }
    return modID;
}

inline std::string installedModSettingPrefix(std::string const& modID) {
    return "installed-mod-" + installedModKey(modID);
}

inline void setInstalledModSource(std::string const& modID, std::string const& version) {
    if (modID.empty() || version.empty()) return;

    auto prefix = installedModSettingPrefix(modID);
    auto indexUrl = getIndexUrl();
    auto indexId = getActiveIndexId();
    auto indexName = indexId;

    for (auto const& entry : getAllIndexes()) {
        if (entry.url == indexUrl) {
            indexName = entry.name;
            indexId = entry.id;
            break;
        }
    }

    writeSetting(prefix + "-index-id", indexId);
    writeSetting(prefix + "-index-name", indexName);
    writeSetting(prefix + "-index-url", indexUrl);
    writeSetting(prefix + "-version", version);
    deleteSetting(prefix + "-updated");
}

inline void setInstalledModSource(
    std::string const& modID,
    std::string const& version,
    std::string const& indexId,
    bool updated
) {
    if (modID.empty() || version.empty() || indexId.empty()) return;

    auto prefix = installedModSettingPrefix(modID);
    auto indexName = indexId;
    auto indexUrl = std::string();

    for (auto const& entry : getAllIndexes()) {
        if (entry.id == indexId) {
            indexName = entry.name;
            indexUrl = entry.url;
            break;
        }
    }

    writeSetting(prefix + "-index-id", indexId);
    writeSetting(prefix + "-index-name", indexName);
    writeSetting(prefix + "-index-url", indexUrl);
    writeSetting(prefix + "-version", version);
    writeSetting(prefix + "-updated", updated ? "1" : "0");
}

inline bool wasModUpdatedFromIndex(std::string const& modID) {
    return readSetting(installedModSettingPrefix(modID) + "-updated", "0") == "1";
}

inline bool hasPendingModUpdate(std::string const& modID) {
    return wasModUpdatedFromIndex(modID);
}

inline void clearPendingModUpdates() {
    for (auto* mod : Loader::get()->getAllMods()) {
        if (!mod) continue;
        deleteSetting(installedModSettingPrefix(mod->getID()) + "-updated");
    }
}

inline std::optional<InstalledModSource> getInstalledModSource(std::string const& modID) {
    if (modID.empty() || !Loader::get()->isModInstalled(modID)) return std::nullopt;

    auto prefix = installedModSettingPrefix(modID);
    auto version = readSetting(prefix + "-version", "");
    if (version.empty()) return std::nullopt;

    return InstalledModSource{
        readSetting(prefix + "-index-id", ""),
        readSetting(prefix + "-index-name", ""),
        readSetting(prefix + "-index-url", ""),
        std::move(version)
    };
}

inline void clearInstalledModSource(std::string const& modID) {
    if (modID.empty()) return;
    auto prefix = installedModSettingPrefix(modID);
    deleteSetting(prefix + "-index-id");
    deleteSetting(prefix + "-index-name");
    deleteSetting(prefix + "-index-url");
    deleteSetting(prefix + "-version");
}

inline std::unordered_map<std::string, std::string>& pendingVersionInstalls() {
    static std::unordered_map<std::string, std::string> pending;
    return pending;
}

inline void setPendingVersionInstall(std::string const& modID, std::string const& version) {
    if (modID.empty() || version.empty()) return;
    pendingVersionInstalls()[modID] = version;
}

inline std::optional<std::string> takePendingVersionInstall(std::string const& modID) {
    auto& pending = pendingVersionInstalls();
    auto it = pending.find(modID);
    if (it == pending.end()) return std::nullopt;
    auto version = it->second;
    pending.erase(it);
    return version;
}

} // namespace opengeode
