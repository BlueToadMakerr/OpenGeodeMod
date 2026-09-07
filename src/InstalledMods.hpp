#pragma once

#include "Settings.hpp"

#include <cctype>
#include <optional>
#include <string>

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
}

inline std::optional<InstalledModSource> getInstalledModSource(std::string const& modID) {
    if (modID.empty()) return std::nullopt;

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

} // namespace opengeode
