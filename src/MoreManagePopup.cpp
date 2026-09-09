#include "MoreManagePopup.hpp"
#include "InstalledMods.hpp"
#include "PopupSectionUtils.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/IconButtonSprite.hpp>
#include <Geode/ui/Popup.hpp>

using namespace geode::prelude;

namespace opengeode {

namespace {

std::string getPopupModID(CCNode* popup) {
    auto label = typeinfo_cast<CCLabelBMFont*>(popup->getChildByIDRecursive("mod-id-label"));
    if (!label) return "";
    std::string value = label->getString();
    auto prefix = std::string("(ID: ");
    if (!value.starts_with(prefix) || value.size() <= prefix.size()) return "";
    value.erase(0, prefix.size());
    if (!value.empty() && value.back() == ')') value.pop_back();
    return value;
}

struct NativeAction {
    CCMenuItem* action = nullptr;
    IconButtonSprite* visual = nullptr;
};

void debugSprite(char const* label, CCSprite* sprite) {
    if (!sprite) {
        log::info("[OpenGeode][MoreDebug] {}: null", label);
        return;
    }

    auto rect = sprite->getTextureRect();
    auto size = sprite->getTexture() ? sprite->getTexture()->getContentSize() : CCSize{0.f, 0.f};
    log::info(
        "[OpenGeode][MoreDebug] {}: texture={} rect=({}, {}, {}, {}) rotated={} flipX={} flipY={} textureSize=({}, {})",
        label,
        sprite->getTexture() ? sprite->getTexture()->getName() : 0,
        rect.origin.x, rect.origin.y, rect.size.width, rect.size.height,
        sprite->isTextureRectRotated(),
        sprite->isFlipX(), sprite->isFlipY(),
        size.width, size.height
    );
}

void debugNativeButton(char const* id, IconButtonSprite* button) {
    if (!button) {
        log::info("[OpenGeode][MoreDebug] {}: no IconButtonSprite", id);
        return;
    }

    log::info(
        "[OpenGeode][MoreDebug] {}: string=\"{}\" scale={} size=({}, {}) bg={} icon={}",
        id,
        button->getString(),
        button->getScale(),
        button->getContentSize().width, button->getContentSize().height,
        fmt::ptr(button->getBg()),
        fmt::ptr(button->getIcon())
    );

    debugSprite("  icon", typeinfo_cast<CCSprite*>(button->getIcon()));

    auto bg = button->getBg();
    if (!bg) return;

    log::info(
        "[OpenGeode][MoreDebug] {}: NineSlice size=({}, {}) insets=({}, {}, {}, {}) repeat={} scaleMultiplier={}",
        id,
        bg->getContentSize().width, bg->getContentSize().height,
        bg->getInsetTop(), bg->getInsetRight(), bg->getInsetBottom(), bg->getInsetLeft(),
        bg->getRepeatCenter(), bg->getScaleMultiplier()
    );

    debugSprite("  bg topLeft", bg->getTopLeft());
    debugSprite("  bg topRight", bg->getTopRight());
    debugSprite("  bg bottomLeft", bg->getBottomLeft());
    debugSprite("  bg bottomRight", bg->getBottomRight());
    debugSprite("  bg top", bg->getTop());
    debugSprite("  bg bottom", bg->getBottom());
    debugSprite("  bg left", bg->getLeft());
    debugSprite("  bg right", bg->getRight());
    debugSprite("  bg center", bg->getCenter());
}

IconButtonSprite* getVisibleIconButton(CCMenuItem* item) {
    if (auto toggler = typeinfo_cast<CCMenuItemToggler*>(item)) {
        auto wrapper = toggler->m_onButton && toggler->m_onButton->isVisible()
            ? toggler->m_onButton
            : toggler->m_offButton;
        if (!wrapper) return nullptr;
        for (auto child : CCArrayExt<CCNode*>(wrapper->getChildren())) {
            if (auto button = typeinfo_cast<IconButtonSprite*>(child)) return button;
        }
        return nullptr;
    }

    if (auto spriteItem = typeinfo_cast<CCMenuItemSpriteExtra*>(item)) {
        return typeinfo_cast<IconButtonSprite*>(spriteItem->getNormalImage());
    }

    return nullptr;
}

void debugManagementChild(CCNode* child) {
    if (!child) return;

    auto id = child->getID();
    auto action = typeinfo_cast<CCMenuItem*>(child);
    auto toggler = typeinfo_cast<CCMenuItemToggler*>(action);
    log::info(
        "[OpenGeode][MoreDebug] management child id=\"{}\" visible={} enabled={} menuItem={} toggler={}",
        id,
        child->isVisible(),
        action ? action->isEnabled() : false,
        action != nullptr,
        toggler != nullptr
    );

    if (!action) return;

    if (toggler) {
        log::info(
            "[OpenGeode][MoreDebug]   toggler onVisible={} offVisible={} on={} off={}",
            toggler->m_onButton ? toggler->m_onButton->isVisible() : false,
            toggler->m_offButton ? toggler->m_offButton->isVisible() : false,
            fmt::ptr(toggler->m_onButton),
            fmt::ptr(toggler->m_offButton)
        );
    }

    auto visual = getVisibleIconButton(action);
    if (visual) {
        debugNativeButton(id.c_str(), visual);
    }
    else {
        log::info("[OpenGeode][MoreDebug]   no visible IconButtonSprite");
    }
}

NativeAction getVisibleNativeAction(CCNode* popup, char const* id) {
    auto node = popup->getChildByIDRecursive(id);
    if (!node || !node->isVisible()) return {};
    auto action = typeinfo_cast<CCMenuItem*>(node);
    if (!action) return {};
    return {action, getVisibleIconButton(action)};
}

bool isPopupInstalled(CCNode* popup) {
    return getVisibleNativeAction(popup, "uninstall-button").action ||
        getVisibleNativeAction(popup, "update-button").action ||
        getVisibleNativeAction(popup, "enable-button").action ||
        getVisibleNativeAction(popup, "reenable-button").action;
}

CCMenu* getNativeManagementMenu(CCNode* popup) {
    for (auto const& id : {
        "update-button", "enable-button", "reenable-button", "unavailable-button",
        "install-button", "uninstall-button", "cancel-button"
    }) {
        auto native = getVisibleNativeAction(popup, id);
        if (!native.action) continue;
        auto menu = typeinfo_cast<CCMenu*>(native.action->getParent());
        if (menu) return menu;
    }
    return nullptr;
}

void showInstallSource(std::string const& modID) {
    auto source = getInstalledModSource(modID);
    if (!source) return;
    auto description = fmt::format(
        "Installed from <cy>{}</c>\nVersion: <cg>{}</c>\n{}",
        source->indexName.empty() ? source->indexUrl : source->indexName,
        source->version,
        source->indexUrl
    );
    FLAlertLayer::create("Install Source", description, "OK")->show();
}

IconButtonSprite* createThemedManageButton(char const* text, char const* iconFrame) {
    auto icon = CCSprite::createWithSpriteFrameName(iconFrame);
    if (!icon) return nullptr;
    auto button = IconButtonSprite::create(getButtonTexture("GJ_button_01.png"), icon, text, "bigFont.fnt");
    if (button) button->setScale(.5f);
    return button;
}

char const* getNativeButtonTexture(char const* id) {
    if (std::string_view(id) == "enable-button" || std::string_view(id) == "reenable-button") {
        return "GJ_button_02.png";
    }
    if (std::string_view(id) == "uninstall-button") {
        return "GJ_button_06.png";
    }
    if (std::string_view(id) == "unavailable-button") {
        return "GJ_button_05.png";
    }
    return "GJ_button_01.png";
}

CCNode* duplicateIcon(CCNode* icon) {
    auto sprite = typeinfo_cast<CCSprite*>(icon);
    if (!sprite || !sprite->getTexture()) return nullptr;

    auto texture = sprite->getTexture();
    auto rect = sprite->getTextureRect();
    auto rotated = sprite->isTextureRectRotated();

    log::info(
        "[OpenGeode][MoreDebug] duplicateIcon: preserving texture={} rect=({}, {}, {}, {}) rotated={} flipX={} flipY={}",
        texture->getName(),
        rect.origin.x, rect.origin.y, rect.size.width, rect.size.height,
        rotated,
        sprite->isFlipX(), sprite->isFlipY()
    );

    auto frame = CCSpriteFrame::createWithTexture(
        texture,
        rect,
        rotated,
        {0.f, 0.f},
        rect.size
    );
    if (!frame) {
        log::info("[OpenGeode][MoreDebug] duplicateIcon: failed to create sprite frame");
        return nullptr;
    }

    auto duplicate = CCSprite::createWithSpriteFrame(frame);
    if (!duplicate) {
        log::info("[OpenGeode][MoreDebug] duplicateIcon: failed to create sprite from frame");
        return nullptr;
    }

    duplicate->setFlipX(sprite->isFlipX());
    duplicate->setFlipY(sprite->isFlipY());
    debugSprite("duplicateIcon result", duplicate);
    return duplicate;
}

IconButtonSprite* recreateNativeButton(char const* id, IconButtonSprite* source) {
    if (!source) return nullptr;

    debugNativeButton(id, source);

    auto icon = duplicateIcon(source->getIcon());
    if (!icon) return nullptr;

    auto button = IconButtonSprite::create(
        getNativeButtonTexture(id),
        icon,
        source->getString(),
        "bigFont.fnt"
    );
    if (!button) return nullptr;

    button->setScale(source->getScale());
    return button;
}

class MoreManagePopup : public Popup {
    CCNode* m_modPopup = nullptr;

protected:
    bool init(CCNode* modPopup) {
        if (!Popup::init(190.f, 255.f, getPopupBackground())) return false;
        m_modPopup = modPopup;
        setTitle("More");
        if (auto close = createGeodeCloseButton()) setCloseButtonSpr(close, .8f);

        auto menu = CCMenu::create();
        menu->setContentSize({150.f, 190.f});
        menu->setLayout(ColumnLayout::create()->setGap(6.f)->setAxisAlignment(AxisAlignment::Center));
        m_mainLayer->addChildAtPosition(menu, Anchor::Center);

        auto managementMenu = getNativeManagementMenu(modPopup);
        if (!managementMenu) return true;

        log::info("[OpenGeode][MoreDebug] Native management menu children={}", managementMenu->getChildrenCount());
        for (auto child : CCArrayExt<CCNode*>(managementMenu->getChildren())) {
            debugManagementChild(child);
        }

        for (auto child : CCArrayExt<CCNode*>(managementMenu->getChildren())) {
            if (!child || child->getID() == "opengeode-more-button") continue;

            auto action = typeinfo_cast<CCMenuItem*>(child);
            if (!action || !action->isVisible()) continue;

            auto source = getVisibleIconButton(action);
            if (!source || !source->isVisible()) continue;

            auto button = recreateNativeButton(child->getID().c_str(), source);
            if (!button) continue;

            auto item = CCMenuItemExt::createSpriteExtra(
                button,
                [action, this](CCMenuItemSpriteExtra*) {
                    action->activate();
                    this->onClose(nullptr);
                }
            );
            menu->addChild(item);
        }

        menu->updateLayout();
        return true;
    }

public:
    static MoreManagePopup* create(CCNode* modPopup) {
        auto ret = new MoreManagePopup();
        if (ret && ret->init(modPopup)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

} // namespace

void ensureModPopupExtras(CCNode* popup) {
    auto manageTitle = popup->getChildByIDRecursive("manage-title");
    if (!manageTitle) return;

    auto managementMenu = getNativeManagementMenu(popup);
    if (!managementMenu) return;

    auto modID = getPopupModID(popup);
    if (!managementMenu->getChildByID("opengeode-from-button"_spr) &&
        !modID.empty() && getInstalledModSource(modID) && isPopupInstalled(popup)) {
        if (auto sprite = createThemedManageButton("From", "GJ_downloadsIcon_001.png")) {
            auto from = CCMenuItemExt::createSpriteExtra(
                sprite,
                [modID](CCMenuItemSpriteExtra*) { showInstallSource(modID); }
            );
            from->setID("opengeode-from-button"_spr);
            managementMenu->addChild(from);
        }
    }

    if (!managementMenu->getChildByID("opengeode-more-button"_spr)) {
        if (auto sprite = createThemedManageButton("More", "GJ_filterIcon_001.png")) {
            auto more = CCMenuItemExt::createSpriteExtra(
                sprite,
                [popup](CCMenuItemSpriteExtra*) {
                    MoreManagePopup::create(popup)->show();
                }
            );
            more->setID("opengeode-more-button"_spr);
            managementMenu->addChild(more);
        }
    }

    managementMenu->updateLayout();
}

} // namespace opengeode
