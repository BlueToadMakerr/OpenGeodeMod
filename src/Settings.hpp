#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/file.hpp>
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
inline std::filesystem::path legacySettingPath(std::string const& key) { return Mod::get()->getSaveDir() / (key + ".txt"); }
inline std::filesystem::path backupPath() { return dirs::getSaveDir() / "open-geode" / "backup.json"; }
inline bool hasOpenGeodeSaveData() {
    auto const& data = Mod::get()->getSaveContainer();
    if (!data.isObject()) return false;
    for (auto const& key : {std::string_view("custom-index-ids"), std::string_view("custom-index-url"), std::string_view("installed-mod-"), std::string_view("mod-source-"), std::string_view("auth-access-"), std::string_view("auth-refresh-")}) if (data.has(key)) return true;
    return false;
}
inline bool restoreFromBackup() {
    auto path = backupPath();
    if (!std::filesystem::is_regular_file(path)) return false;
    auto json = file::readJson(path);
    if (!json || !json.unwrap().isObject()) { log::warn("OpenGeode backup could not be read: {}", path.string()); return false; }
    Mod::get()->getSaveContainer() = std::move(json.unwrap());
    log::info("Restored OpenGeode save data from backup: {}", path.string());
    return true;
}
inline void restoreFromBackupIfNeeded() {
    if (hasOpenGeodeSaveData()) return;
    if (restoreFromBackup()) {
        auto result = Mod::get()->saveData();
        if (!result) log::warn("Failed to persist restored OpenGeode save data: {}", result.unwrapErr());
    }
}
inline void backupSaveData() {
    auto path = backupPath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) { log::warn("Could not create OpenGeode backup directory: {}", ec.message()); return; }
    auto result = file::writeToJson(path, Mod::get()->getSaveContainer());
    if (!result) log::warn("Could not write OpenGeode backup: {}", result.unwrapErr());
}
inline void migrateLegacyTextSaveData() {
    auto dir = Mod::get()->getSaveDir();
    if (!std::filesystem::is_directory(dir)) return;
    std::error_code ec;
    for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec) || entry.path().extension() != ".txt") continue;
        auto key = entry.path().stem().string();
        if (key.empty() || Mod::get()->hasSavedValue(key)) continue;
        std::ifstream file(entry.path(), std::ios::binary);
        if (!file.is_open()) continue;
        std::ostringstream ss; ss << file.rdbuf();
        Mod::get()->setSavedValue<std::string>(key, ss.str());
        std::filesystem::remove(entry.path(), ec); ec.clear();
    }
}
inline std::string readSetting(std::string const& key, std::string const& fallback) {
    if (Mod::get()->hasSavedValue(key)) return Mod::get()->getSavedValue<std::string>(key, fallback);
    auto path = legacySettingPath(key); std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return fallback;
    std::ostringstream ss; ss << file.rdbuf(); auto value = ss.str();
    Mod::get()->setSavedValue<std::string>(key, value); std::error_code ec; std::filesystem::remove(path, ec); return value;
}
inline void writeSetting(std::string const& key, std::string const& value) { Mod::get()->setSavedValue<std::string>(key, value); }
inline void deleteSetting(std::string const& key) { Mod::get()->getSaveContainer().erase(key); }
inline std::vector<std::string> splitCSV(std::string const& raw) { std::vector<std::string> out; std::stringstream ss(raw); std::string item; while (std::getline(ss,item,',')) if (!item.empty()) out.push_back(item); return out; }
inline std::string joinCSV(std::vector<std::string> const& items) { std::string out; for (size_t i=0;i<items.size();++i) { if(i) out+=','; out+=items[i]; } return out; }
enum class ModStatus { Accepted, Unlisted, Pending, Rejected };
inline ModStatus statusFromString(std::string const& status) { if(status=="unlisted") return ModStatus::Unlisted; if(status=="pending") return ModStatus::Pending; if(status=="rejected") return ModStatus::Rejected; return ModStatus::Accepted; }
inline std::string statusToString(ModStatus status) { switch(status){case ModStatus::Accepted:return "accepted";case ModStatus::Unlisted:return "unlisted";case ModStatus::Pending:return "pending";case ModStatus::Rejected:return "rejected";} return "accepted"; }
inline char const* statusDisplayName(ModStatus status) { switch(status){case ModStatus::Accepted:return "Accepted";case ModStatus::Unlisted:return "Unlisted";case ModStatus::Pending:return "Pending";case ModStatus::Rejected:return "Rejected";} return "Accepted"; }
struct TabFilterConfig { std::string platform; std::string geodeVersion; std::string gdVersion; ModStatus status=ModStatus::Accepted; bool isDefault() const { return platform.empty()&&geodeVersion.empty()&&gdVersion.empty()&&status==ModStatus::Accepted; } void clear(){platform.clear();geodeVersion.clear();gdVersion.clear();status=ModStatus::Accepted;} };
inline std::unordered_map<std::string,TabFilterConfig>& getTabConfigs(){static std::unordered_map<std::string,TabFilterConfig> configs;return configs;}
inline std::string& getCachedActiveTabKey(){static std::string activeKey="installed-button";return activeKey;}
inline TabFilterConfig& getCurrentTabConfig(){return getTabConfigs()[getCachedActiveTabKey()];}
inline bool isFilterActiveForCurrentTab(){return !getCurrentTabConfig().isDefault();}
inline std::string getIndexUrl(){return readSetting("custom-index-url","https://api.geode-sdk.org");}
inline void setIndexUrl(std::string url){if(!url.empty()&&url.back()=='/')url.pop_back();writeSetting("custom-index-url",url);}
struct IndexEntry{std::string id;std::string name;std::string url;};
inline std::vector<IndexEntry> getAllIndexes(){std::vector<IndexEntry> out;for(auto const& id:splitCSV(readSetting("custom-index-ids",""))){IndexEntry e;e.id=id;e.name=readSetting("custom-index-name-"+id,"");e.url=readSetting("custom-index-url-"+id,"");if(!e.url.empty())out.push_back(e);}return out;}
inline std::string getActiveIndexId(){auto url=getIndexUrl();for(auto const& e:getAllIndexes())if(e.url==url)return e.id;std::hash<std::string> h;return "url-"+fmt::format("{:016x}",static_cast<unsigned long long>(h(url)));}
inline std::string getAuthAccessTokenForIndex(std::string const& id){return readSetting("auth-access-"+id,"");}
inline std::string getAuthAccessToken(){return getAuthAccessTokenForIndex(getActiveIndexId());}
inline std::string getAuthRefreshToken(){return readSetting("auth-refresh-"+getActiveIndexId(),"");}
inline bool hasAuthAccessToken(){return !getAuthAccessToken().empty();}
inline void setAuthTokens(std::string const& accessToken,std::string const& refreshToken){auto id=getActiveIndexId();writeSetting("auth-access-"+id,accessToken);writeSetting("auth-refresh-"+id,refreshToken);}
inline void clearAuthTokens(){auto id=getActiveIndexId();deleteSetting("auth-access-"+id);deleteSetting("auth-refresh-"+id);}
inline bool hasAuthTokens(){return hasAuthAccessToken()&&!getAuthRefreshToken().empty();}
inline bool addCustomIndex(std::string name,std::string url){if(!url.empty()&&url.back()=='/')url.pop_back();for(auto const& e:getAllIndexes())if(e.url==url)return false;auto ids=splitCSV(readSetting("custom-index-ids",""));int nextId=0;for(auto const& id:ids)nextId=std::max(nextId,std::atoi(id.c_str())+1);std::string id=std::to_string(nextId);ids.push_back(id);writeSetting("custom-index-ids",joinCSV(ids));writeSetting("custom-index-name-"+id,name);writeSetting("custom-index-url-"+id,url);return true;}
inline void ensurePresetsExist(){if(readSetting("custom-index-ids","NONE")=="NONE"){writeSetting("custom-index-ids","");addCustomIndex("Geode Index API","https://api.geode-sdk.org");addCustomIndex("Open Geode Index","https://open-geode.7m.pl");setIndexUrl("https://api.geode-sdk.org");}}
inline bool updateCustomIndex(std::string const& id,std::string name,std::string url){if(!url.empty()&&url.back()=='/')url.pop_back();for(auto const& e:getAllIndexes())if(e.id!=id&&e.url==url)return false;bool active=readSetting("custom-index-url-"+id,"")==getIndexUrl();writeSetting("custom-index-name-"+id,name);writeSetting("custom-index-url-"+id,url);if(active)setIndexUrl(url);return true;}
inline void deleteCustomIndex(std::string const& id){auto ids=splitCSV(readSetting("custom-index-ids",""));auto url=readSetting("custom-index-url-"+id,"");ids.erase(std::remove(ids.begin(),ids.end(),id),ids.end());writeSetting("custom-index-ids",joinCSV(ids));deleteSetting("custom-index-name-"+id);deleteSetting("custom-index-url-"+id);deleteSetting("auth-access-"+id);deleteSetting("auth-refresh-"+id);if(!url.empty()&&url==getIndexUrl())setIndexUrl(!ids.empty()?readSetting("custom-index-url-"+ids[0],"https://api.geode-sdk.org"):"https://api.geode-sdk.org");}
} // namespace opengeode
