#pragma once

#include <Geode/Geode.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace geode::prelude;

namespace opengeode {

inline bool g_shouldReopenModsList = false;

inline std::filesystem::path settingPath(std::string const& key) {
    auto dir = Mod::get()->getSaveDir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir / (key + ".txt");
}

inline std::string readSetting(std::string const& key, std::string const& fallback) {
    auto path = settingPath(key);
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return fallback;
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

inline void writeSetting(std::string const& key, std::string const& value) {
    auto path = settingPath(key);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (file.is_open()) file << value;
}

inline void deleteSetting(std::string const& key) {
    std::error_code ec;
    std::filesystem::remove(settingPath(key), ec);
}

inline std::vector<std::string> splitCSV(std::string const& raw) {
    std::vector<std::string> out;
    std::stringstream ss(raw);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

inline std::string joinCSV(std::vector<std::string> const& items) {
    std::string out;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i) out += ',';
        out += items[i];
    }
    return out;
}

enum class ModStatus { Accepted, Unlisted, Pending, Rejected };

inline ModStatus statusFromString(std::string const& status) {
    if (status == "unlisted") return ModStatus::Unlisted;
    if (status == "pending") return ModStatus::Pending;
    if (status == "rejected") return ModStatus::Rejected;
    return ModStatus::Accepted;
}

inline std::string statusToString(ModStatus status) {
    switch (status) {
        case ModStatus::Accepted: return "accepted";
        case ModStatus::Unlisted: return "unlisted";
        case ModStatus::Pending: return "pending";
        case ModStatus::Rejected: return "rejected";
    }
    return "accepted";
}

inline char const* statusDisplayName(ModStatus status) {
    switch (status) {
        case ModStatus::Accepted: return "Accepted";
        case ModStatus::Unlisted: return "Unlisted";
        case ModStatus::Pending: return "Pending";
        case ModStatus::Rejected: return "Rejected";
    }
    return "Accepted";
}

struct TabFilterConfig {
    std::string platform;
    std::string geodeVersion;
    std::string gdVersion;
    ModStatus status = ModStatus::Accepted;

    bool isDefault() const {
        return platform.empty() && geodeVersion.empty() && gdVersion.empty() && status == ModStatus::Accepted;
    }

    void clear() {
        platform.clear(); geodeVersion.clear(); gdVersion.clear(); status = ModStatus::Accepted;
    }
};

inline std::unordered_map<std::string, TabFilterConfig>& getTabConfigs() {
    static std::unordered_map<std::string, TabFilterConfig> configs;
    return configs;
}

inline std::string& getCachedActiveTabKey() {
    static std::string activeKey = "installed-button";
    return activeKey;
}

inline TabFilterConfig& getCurrentTabConfig() {
    return getTabConfigs()[getCachedActiveTabKey()];
}

inline bool isFilterActiveForCurrentTab() { return !getCurrentTabConfig().isDefault(); }

inline std::string getIndexUrl() { return readSetting("custom-index-url", "https://api.geode-sdk.org"); }

inline void setIndexUrl(std::string url) {
    if (!url.empty() && url.back() == '/') url.pop_back();
    writeSetting("custom-index-url", url);
}

struct IndexEntry { std::string id; std::string name; std::string url; };

inline std::vector<IndexEntry> getAllIndexes() {
    std::vector<IndexEntry> out;
    for (auto const& id : splitCSV(readSetting("custom-index-ids", ""))) {
        IndexEntry entry;
        entry.id = id;
        entry.name = readSetting("custom-index-name-" + id, "");
        entry.url = readSetting("custom-index-url-" + id, "");
        if (!entry.url.empty()) out.push_back(entry);
    }
    return out;
}

inline std::string getActiveIndexId() {
    auto url = getIndexUrl();
    for (auto const& entry : getAllIndexes()) {
        if (entry.url == url) return entry.id;
    }
    // The fallback keeps tokens isolated even if a legacy installation has
    // an active URL which is not present in the saved index list.
    std::hash<std::string> hasher;
    return "url-" + fmt::format("{:016x}", static_cast<unsigned long long>(hasher(url)));
}

inline std::string getAuthAccessToken() {
    return readSetting("auth-access-" + getActiveIndexId(), "");
}

inline std::string getAuthRefreshToken() {
    return readSetting("auth-refresh-" + getActiveIndexId(), "");
}

inline void setAuthTokens(std::string const& accessToken, std::string const& refreshToken) {
    auto id = getActiveIndexId();
    writeSetting("auth-access-" + id, accessToken);
    writeSetting("auth-refresh-" + id, refreshToken);
}

inline void clearAuthTokens() {
    auto id = getActiveIndexId();
    deleteSetting("auth-access-" + id);
    deleteSetting("auth-refresh-" + id);
}

inline bool hasAuthTokens() {
    return !getAuthAccessToken().empty() && !getAuthRefreshToken().empty();
}

inline bool addCustomIndex(std::string name, std::string url) {
    if (!url.empty() && url.back() == '/') url.pop_back();
    for (auto const& entry : getAllIndexes()) if (entry.url == url) return false;
    auto ids = splitCSV(readSetting("custom-index-ids", ""));
    int nextId = 0;
    for (auto const& id : ids) nextId = std::max(nextId, std::atoi(id.c_str()) + 1);
    std::string id = std::to_string(nextId);
    ids.push_back(id);
    writeSetting("custom-index-ids", joinCSV(ids));
    writeSetting("custom-index-name-" + id, name);
    writeSetting("custom-index-url-" + id, url);
    return true;
}

inline void ensurePresetsExist() {
    if (readSetting("custom-index-ids", "NONE") == "NONE") {
        writeSetting("custom-index-ids", "");
        addCustomIndex("Geode Index API", "https://api.geode-sdk.org");
        addCustomIndex("Open Geode Index", "https://open-geode.7m.pl");
        setIndexUrl("https://api.geode-sdk.org");
    }
}

inline bool updateCustomIndex(std::string const& id, std::string name, std::string url) {
    if (!url.empty() && url.back() == '/') url.pop_back();
    for (auto const& entry : getAllIndexes()) if (entry.id != id && entry.url == url) return false;
    bool wasActive = readSetting("custom-index-url-" + id, "") == getIndexUrl();
    writeSetting("custom-index-name-" + id, name);
    writeSetting("custom-index-url-" + id, url);
    if (wasActive) setIndexUrl(url);
    return true;
}

inline void deleteCustomIndex(std::string const& id) {
    auto ids = splitCSV(readSetting("custom-index-ids", ""));
    auto url = readSetting("custom-index-url-" + id, "");
    ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
    writeSetting("custom-index-ids", joinCSV(ids));
    deleteSetting("custom-index-name-" + id);
    deleteSetting("custom-index-url-" + id);
    deleteSetting("auth-access-" + id);
    deleteSetting("auth-refresh-" + id);
    if (!url.empty() && url == getIndexUrl()) {
        setIndexUrl(!ids.empty() ? readSetting("custom-index-url-" + ids[0], "https://api.geode-sdk.org") : "https://api.geode-sdk.org");
    }
}

} // namespace opengeode
