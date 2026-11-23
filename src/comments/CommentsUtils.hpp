#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/LazySprite.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/utils/web.hpp>

#include <asp/fs/fs.hpp>
#include <memory>
#include <string>

using namespace geode::prelude;

namespace opengeode {

void showNotification(std::string message, NotificationIcon icon, float time);
void notifyStatus(std::string const& message);

struct LoadingNotification : std::enable_shared_from_this<LoadingNotification> {
    Ref<Notification> notification;
    bool cancelRequested = false;

    static std::shared_ptr<LoadingNotification> create(std::string message);
    void hide();
};

std::string trimSlash(std::string url);
std::string errorText(web::WebResponse const& response);
std::string stringValue(matjson::Value const& value, char const* key, std::string fallback = "");
int intValue(matjson::Value const& value, char const* key, int fallback = 0);

LazySprite* createContainedImage(
    cocos2d::CCNode* holder,
    cocos2d::CCSize size,
    std::string const& url,
    asp::fs::Path const* localPath = nullptr
);

cocos2d::CCNode* createAttachmentBox(
    float size,
    std::string const& url,
    asp::fs::Path const* localPath = nullptr
);

std::string getModID(cocos2d::CCNode* popup);

/// Returns the last component of a path (or the whole path if it has none)
std::string pathFileName(asp::fs::PathView path);

} // namespace opengeode
