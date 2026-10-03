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
        for(auto& c:modID)if(!std::isalnum(static_cast<unsigned char>(c))&&c!='-'&&c!='_')c='_';
        return modID;
    }
    inline std::string installedModSettingPrefix(std::string const& modID) {
        return "installed-mod-"+installedModKey(modID);
    }
    inline void setInstalledModSource(std::string const& modID,std::string const& version) {
        if(modID.empty()||version.empty())return;
        auto prefix=installedModSettingPrefix(modID);
        auto indexUrl=getIndexUrl();
        auto indexId=getActiveIndexId();
        auto indexName=indexId;
        for(auto const& entry:getAllIndexes())if(entry.url==indexUrl) {
            indexName=entry.name;
            indexId=entry.id;
            break;
        }
        writeSetting(prefix+"-index-id",indexId);
        writeSetting(prefix+"-index-name",indexName);
        writeSetting(prefix+"-index-url",indexUrl);
        writeSetting(prefix+"-version",version);
        deleteSetting(prefix+"-updated");
        deleteSetting(prefix+"-geode-updated");
    }
    inline void setInstalledModSource(std::string const& modID,std::string const& version,std::string const& indexId,bool updated) {
        if(modID.empty()||version.empty()||indexId.empty())return;
        auto prefix=installedModSettingPrefix(modID);
        auto indexName=indexId;
        auto indexUrl=std::string();
        for(auto const& entry:getAllIndexes())if(entry.id==indexId) {
            indexName=entry.name;
            indexUrl=entry.url;
            break;
        }
        writeSetting(prefix+"-index-id",indexId);
        writeSetting(prefix+"-index-name",indexName);
        writeSetting(prefix+"-index-url",indexUrl);
        writeSetting(prefix+"-version",version);
        writeSetting(prefix+"-updated",updated?"1":"0");
        deleteSetting(prefix+"-geode-updated");
    }
    inline bool wasModUpdatedFromGeode(std::string const& modID) {
        return readSetting(installedModSettingPrefix(modID)+"-geode-updated","0")=="1";
    }
    inline bool wasModUpdatedFromIndex(std::string const& modID) {
        return readSetting(installedModSettingPrefix(modID)+"-updated","0")=="1"||wasModUpdatedFromGeode(modID);
    }
    inline bool wasModUpdatedThroughOpenGeode(std::string const& modID) {
        return readSetting(installedModSettingPrefix(modID)+"-updated","0")=="1";
    }
    inline bool wasModUpdatedThroughAnything(std::string const& modID) {
        return wasModUpdatedFromIndex(modID);
    }
    inline void markModUpdatedFromGeode(std::string const& modID) {
        if(!modID.empty())writeSetting(installedModSettingPrefix(modID)+"-geode-updated","1");
    }
    inline void clearModUpdatedFromGeode(std::string const& modID) {
        if(!modID.empty())deleteSetting(installedModSettingPrefix(modID)+"-geode-updated");
    }
    inline bool hasPendingModUpdate(std::string const& modID) {
        return wasModUpdatedFromIndex(modID);
    }
    inline void clearPendingModUpdates() {
        for(auto* mod:Loader::get()->getAllMods()) {
            if(!mod)continue;
            auto p=installedModSettingPrefix(mod->getID());
            deleteSetting(p+"-updated");
            deleteSetting(p+"-geode-updated");
        }
    }
    inline std::optional<InstalledModSource> getInstalledModSource(std::string const& modID) {
        if(modID.empty()||!Loader::get()->isModInstalled(modID))return std::nullopt;
        auto prefix=installedModSettingPrefix(modID);
        auto version=readSetting(prefix+"-version","");
        if(version.empty())return std::nullopt;
        return InstalledModSource {
            readSetting(prefix+"-index-id",""),readSetting(prefix+"-index-name",""),readSetting(prefix+"-index-url",""),std::move(version)
        };
    }
    inline void clearInstalledModSource(std::string const& modID) {
        if(modID.empty())return;
        auto prefix=installedModSettingPrefix(modID);
        deleteSetting(prefix+"-index-id");
        deleteSetting(prefix+"-index-name");
        deleteSetting(prefix+"-index-url");
        deleteSetting(prefix+"-version");
        deleteSetting(prefix+"-updated");
        deleteSetting(prefix+"-geode-updated");
    }
    inline std::unordered_map<std::string,std::string>& pendingVersionInstalls() {
        static std::unordered_map<std::string,std::string> pending;
        return pending;
    }
    inline void setPendingVersionInstall(std::string const& modID,std::string const& version) {
        if(modID.empty()||version.empty())return;
        pendingVersionInstalls()[modID]=version;
    }
    inline std::optional<std::string> takePendingVersionInstall(std::string const& modID) {
        auto& pending=pendingVersionInstalls();
        auto it=pending.find(modID);
        if(it==pending.end())return std::nullopt;
        auto version=it->second;
        pending.erase(it);
        return version;
    }
    void showAlreadyUpdatedPopup(std::string const& modID);
}
