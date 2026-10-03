#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/LazySprite.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/utils/web.hpp>
#include <filesystem>
#include <memory>
#include <string>
using namespace geode::prelude;
namespace opengeode {
    void showNotification(std::string message, NotificationIcon icon, float time);
    void notifyStatus(std::string const & message);
    struct LoadingNotification: std::enable_shared_from_this < LoadingNotification > {
        Ref < Notification > notification;
        bool cancelRequested = false;
        static std::shared_ptr < LoadingNotification > create(std::string message);
        void hide();
    };
    std::string trimSlash(std::string url);
    std::string errorText(web::WebResponse const & response);
    std::string stringValue(matjson::Value const & value, char const * key, std::string fallback = "");
    int intValue(matjson::Value const & value, char const * key, int fallback = 0);
    LazySprite * createContainedImage(cocos2d::CCNode * holder, cocos2d::CCSize size, std::string const & url, std::filesystem::path const * localPath = nullptr);
    cocos2d::CCNode * createAttachmentBox(float size, std::string const & url, std::filesystem::path const * localPath = nullptr);
    std::string getModID(cocos2d::CCNode * popup);
}
// namespace opengeode
