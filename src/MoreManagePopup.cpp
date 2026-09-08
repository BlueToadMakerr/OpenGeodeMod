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
        return getButtonTexture("GJ_button_02.png");
    }
    if (std::string_view(id) == "uninstall-button") {
        return getButtonTexture("GJ_button_06.png");
    }
    if (std::string_view(id) == "unavailable-button") {
        return getButtonTexture("GJ_button_05.png");
    }
    return getButtonTexture("GJ_button_01.png");
}

CCNode* duplicateIcon(CCNode* icon) {
    if (auto sprite = typeinfo_cast<CCSprite*>(icon)) {
        auto texture = sprite->getTexture();
        if (!texture) return nullptr;
        return CCSprite::createWithTexture(
            texture,
            sprite->getTextureRect(),
            sprite->isTextureRectRotated()
        );
    }

    return nullptr;
}

IconButtonSprite* recreateNativeButton(char const* id, IconButtonSprite* source) {
    if (!source) return nullptr;

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

        for (auto child : CCArrayExt<CCNode*>(managementMenu->getChildren())) {
            if (!child || child->getID() == "opengeode-more-button") continue;

            auto action = typeinfo_cast<CCMenuItem*>(child);
            if (!action) continue;

            auto source = getVisibleIconButton(action);
            if (!source) continue;

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
