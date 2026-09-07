#pragma once

#include "Settings.hpp"

#include <optional>
#include <string>

namespace opengeode {

struct InstalledModSource {
    std::string indexId;
    std::string indexName;
    std::string indexUrl;
    std::string version;
};

inline std::string installedModKey(std::string const& modID) {
    std::string key = "installed-source-";
    for (auto c : modID) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_') {
            key += c;
        }
        else {
            key += '_';
        }
    }
    return key;
}

inline void setInstalledModSource(std::string const& modID, std::string const& version) {
    auto indexId = getActiveIndexId();
    auto indexName = indexId;
    auto indexUrl = getIndexUrl();

    for (auto const& entry : getAllIndexes()) {
        if (entry.id == indexId) {
            indexName = entry.name.empty() ? entry.url : entry.name;
            indexUrl = entry.url;
            break;
        }
    }

    auto key = installedModKey(modID);
    writeSetting(key + "-index-id", indexId);
    writeSetting(key + "-index-name", indexName);
    writeSetting(key + "-index-url", indexUrl);
    writeSetting(key + "-version", version);
}

inline std::optional<InstalledModSource> getInstalledModSource(std::string const& modID) {
    auto key = installedModKey(modID);
    auto version = readSetting(key + "-version", "");
    auto indexUrl = readSetting(key + "-index-url", "");
    if (version.empty() || indexUrl.empty()) return std::nullopt;

    InstalledModSource source;
    source.indexId = readSetting(key + "-index-id", "");
    source.indexName = readSetting(key + "-index-name", indexUrl);
    source.indexUrl = indexUrl;
    source.version = version;
    return source;
}

inline void clearInstalledModSource(std::string const& modID) {
    auto key = installedModKey(modID);
    deleteSetting(key + "-index-id");
    deleteSetting(key + "-index-name");
    deleteSetting(key + "-index-url");
    deleteSetting(key + "-version");
}

} // namespace opengeode
