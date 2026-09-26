#include "CommentsUtils.hpp"

#include <Geode/utils/string.hpp>

#include <algorithm>
#include <cmath>
#include <memory>

using namespace geode::prelude;

namespace opengeode {

std::shared_ptr<LoadingNotification> LoadingNotification::create(std::string message) {
    auto ref = std::make_shared<LoadingNotification>();

    geode::queueInMainThread([
        ref,
        message = std::move(message)
    ] {
        ref->notification = Notification::create(
            message,
            NotificationIcon::Loading,
            0.f
        );

        ref->notification->show();

        if (ref->cancelRequested) {
            ref->notification->cancel();
            ref->notification = nullptr;
        }
    });

    return ref;
}

void LoadingNotification::hide() {
    auto self = shared_from_this();

    geode::queueInMainThread([self] {
        self->cancelRequested = true;

        if (self->notification) {
            self->notification->cancel();
            self->notification = nullptr;
        }
    });
}

void showNotification(
    std::string message,
    NotificationIcon icon,
    float time
) {
    geode::queueInMainThread(
        [message = std::move(message), icon, time] {
            Notification::create(message, icon, time)->show();
        }
    );
}

void notifyStatus(std::string const& message) {
    showNotification(message, NotificationIcon::Info, 1.5f);
}

std::string trimSlash(std::string url) {
    while (!url.empty() && url.back() == '/') url.pop_back();
    return url;
}

std::string errorText(web::WebResponse const& response) {
    if (auto json = response.json()) {
        for (auto const* key : {"error", "detail", "message"}) {
            auto value = (*json)[key].asString().unwrapOr("");
            if (!value.empty()) return value;
        }
    }
    if (response.code() > 0) return fmt::format("HTTP {}", response.code());
    if (!response.errorMessage().empty()) return std::string(response.errorMessage());
    return "Request failed.";
}

std::string stringValue(matjson::Value const& value, char const* key, std::string fallback = "") {
    return value[key].asString().unwrapOr(fallback);
}

int intValue(matjson::Value const& value, char const* key, int fallback = 0) {
    return value[key].asInt().unwrapOr(fallback);
}

LazySprite* createContainedImage(
    CCNode* holder,
    CCSize size,
    std::string const& url,
    std::filesystem::path const* localPath = nullptr
) {
    if (localPath) {
        std::error_code ec;
        if (!std::filesystem::exists(*localPath, ec) || ec)
            return nullptr;
    }
    else if (url.empty()) {
        return nullptr;
    }

    auto sprite = LazySprite::create(size, false);
    if (!sprite) return nullptr;

    holder->addChildAtPosition(sprite, Anchor::Center);
    sprite->setLoadCallback([sprite, size](Result<> result) {
        if (!result) {
            sprite->setVisible(false);
            return;
        }

        auto real = sprite->getContentSize();
        if (auto inner = sprite->getChildByType<CCSprite>(0)) {
            auto innerSize = inner->getScaledContentSize();
            if (innerSize.width > 0.f && innerSize.height > 0.f)
                real = innerSize;
        }
        if (real.width > 0.f && real.height > 0.f) {
            auto fit = std::min(
                1.f,
                std::min(size.width / real.width, size.height / real.height)
            );
            sprite->setScale(fit);
        }
        if (auto parent = sprite->getParent())
            sprite->setPosition(parent->getContentSize() / 2.f);
    });

    if (localPath) sprite->loadFromFile(*localPath);
    else sprite->loadFromUrl(url);

    return sprite;
}

CCNode* createAttachmentBox(
    float size,
    std::string const& url,
    std::filesystem::path const* localPath = nullptr
) {
    auto box = CCNode::create();
    box->setContentSize({size, size});
    box->setAnchorPoint({.5f, .5f});
    box->setLayout(AnchorLayout::create());

    auto bg = NineSlice::create("square02b_001.png");
    bg->setColor(ccBLACK);
    bg->setOpacity(120);
    bg->setScale(.3f);
    bg->setContentSize(box->getContentSize() / bg->getScale());
    box->addChildAtPosition(bg, Anchor::Center);

    createContainedImage(box, {size - 4.f, size - 4.f}, url, localPath);
    return box;
}

std::string getModID(CCNode* popup) {
    auto label = typeinfo_cast<CCLabelBMFont*>(popup->getChildByIDRecursive("mod-id-label"));
    if (!label) return "";

    auto value = std::string(label->getString());
    constexpr char const* prefix = "(ID: ";
    if (!value.starts_with(prefix)) return "";

    value.erase(0, 5);
    if (!value.empty() && value.back() == ')') value.pop_back();
    return value;
}

} // namespace opengeode
