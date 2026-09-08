#include "FilterPopup.hpp"
#include "IndexListPopup.hpp"
#include "ModsListUtils.hpp"
#include "AccountPopup.hpp"
#include "InstalledMods.hpp"
#include "PopupSectionUtils.hpp"
#include "Settings.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/IconButtonSprite.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/SceneEvent.hpp>
#include <Geode/utils/web.hpp>

#include <algorithm>
#include <string>

using namespace geode::prelude;

namespace opengeode {

Notification* g_switchNotif = nullptr;

namespace {

CCNode* createProfileButtonSprite() {
    auto profile = CCSprite::createWithSpriteFrameName("GJ_profileButton_001.png");
    if (!profile) return nullptr;
    constexpr float targetSize = 40.f;
    auto width = profile->getContentSize().width;
    auto height = profile->getContentSize().height;
    if (width <= 0.f || height <= 0.f) return nullptr;
    auto root = CCNode::create();
    if (!root) return nullptr;
    profile->setPosition({targetSize / 2.f, targetSize / 2.f});
    profile->setScale(targetSize / std::max(width, height));
    root->setContentSize({targetSize, targetSize});
    root->setAnchorPoint({.5f, .5f});
    root->addChild(profile);
    return root;
}

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
    CCNode* visual = nullptr;
};

NativeAction getNativeAction(CCNode* popup, char const* id) {
    auto node = popup->getChildByIDRecursive(id);
    if (!node || !node->isVisible()) return {};

    if (auto toggler = typeinfo_cast<CCMenuItemToggler*>(node)) {
        log::debug(
            "[OpenGeode] Native action '{}' is toggler: nodeVisible={}, on={}, off={}",
            id, toggler->isVisible(),
            toggler->m_onButton ? toggler->m_onButton->isVisible() : false,
            toggler->m_offButton ? toggler->m_offButton->isVisible() : false
        );
        if (toggler->m_onButton && toggler->m_onButton->isVisible()) {
            return {toggler, toggler->m_onButton};
        }
        if (toggler->m_offButton && toggler->m_offButton->isVisible()) {
            return {toggler, toggler->m_offButton};
        }
        return {};
    }

    auto item = typeinfo_cast<CCMenuItemSpriteExtra*>(node);
    if (!item) {
        log::debug("[OpenGeode] Native action '{}' exists but is not CCMenuItemSpriteExtra", id);
        return {};
    }

    auto visual = item->getNormalImage();
    log::debug(
        "[OpenGeode] Native action '{}': nodeVisible={}, visual={}",
        id, item->isVisible(), visual != nullptr
    );
    return {item, visual};
}

bool isPopupInstalled(CCNode* popup) {
    return getNativeAction(popup, "uninstall-button").action ||
        getNativeAction(popup, "update-button").action ||
        getNativeAction(popup, "enable-button").action ||
        getNativeAction(popup, "reenable-button").action;
}

CCMenu* getNativeInstallMenu(CCNode* popup) {
    for (auto const& id : {
        "update-button", "enable-button", "reenable-button", "unavailable-button",
        "install-button", "uninstall-button", "cancel-button"
    }) {
        auto native = getNativeAction(popup, id);
        if (!native.action) continue;
        auto menu = typeinfo_cast<CCMenu*>(native.action->getParent());
        if (menu) {
            log::info("[OpenGeode] Using native management menu from visible '{}' button; children={}", id, menu->getChildrenCount());
            return menu;
        }
    }
    log::debug("[OpenGeode] Could not find visible native management button/menu yet");
    return nullptr;
}

CCNode* cloneNativeVisual(CCNode* visual) {
    if (!visual) {
        log::error("[OpenGeode] Cannot clone native visual: null");
        return nullptr;
    }
    log::debug("[OpenGeode] Cloning native visual ptr={} visible={}", static_cast<void*>(visual), visual->isVisible());
    auto copy = typeinfo_cast<CCNode*>(visual->copyWithZone(nullptr));
    if (!copy) {
        log::error("[OpenGeode] copyWithZone failed for native visual ptr={}", static_cast<void*>(visual));
        return nullptr;
    }
    copy->setPosition({0.f, 0.f});
    copy->setScale(.5f);
    return copy;
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

IconButtonSprite* createFixedManageButton(char const* text, char const* iconFrame) {
    auto icon = CCSprite::createWithSpriteFrameName(iconFrame);
    if (!icon) return nullptr;
    auto button = IconButtonSprite::create(
        "GJ_button_01.png", icon, text, "bigFont.fnt"
    );
    if (button) button->setScale(.5f);
    return button;
}

IconButtonSprite* createThemedManageButton(char const* text, char const* iconFrame) {
    auto icon = CCSprite::createWithSpriteFrameName(iconFrame);
    if (!icon) return nullptr;
    auto button = IconButtonSprite::create(
        getButtonTexture("GJ_button_01.png"), icon, text, "bigFont.fnt"
    );
    if (button) button->setScale(.5f);
    return button;
}

class MoreManagePopup : public Popup {
    CCNode* m_modPopup = nullptr;

protected:
    bool addNativeVisual(CCMenu* menu, char const* id, CCMenuItem* action, CCNode* visual) {
        if (!action || !visual) {
            log::warn("[OpenGeode] More popup cannot add '{}' action={} visual={}", id, action != nullptr, visual != nullptr);
            return false;
        }
        auto copy = cloneNativeVisual(visual);
        if (!copy) {
            log::warn("[OpenGeode] More popup clone failed for '{}'", id);
            return false;
        }
        auto item = CCMenuItemExt::createSpriteExtra(
            copy,
            [action, this](CCMenuItemSpriteExtra*) {
                log::info("[OpenGeode] More popup activating native action");
                action->activate();
                this->onClose(nullptr);
            }
        );
        menu->addChild(item);
        log::debug("[OpenGeode] More popup added '{}' from native management menu", id);
        return true;
    }

    bool init(CCNode* modPopup) {
        if (!Popup::init(190.f, 255.f, getPopupBackground())) return false;
        m_modPopup = modPopup;
        setTitle("More");
        if (auto close = createGeodeCloseButton()) setCloseButtonSpr(close, .8f);

        auto menu = CCMenu::create();
        menu->setContentSize({150.f, 190.f});
        menu->setLayout(ColumnLayout::create()->setGap(6.f)->setAxisAlignment(AxisAlignment::Center));
        m_mainLayer->addChildAtPosition(menu, Anchor::Center);

        log::info("[OpenGeode] Building More popup from native management menu");

        auto installMenu = getNativeInstallMenu(modPopup);
        if (!installMenu) {
            log::warn("[OpenGeode] More popup has no native management menu");
            return true;
        }

        for (auto child : CCArrayExt<CCNode*>(installMenu->getChildren())) {
            auto id = child->getID();
            if (id == "opengeode-more-button") {
                log::debug("[OpenGeode] Skipping OpenGeode More button");
                continue;
            }

            if (auto toggler = typeinfo_cast<CCMenuItemToggler*>(child)) {
                log::debug(
                    "[OpenGeode] Reading toggler '{}' from native management menu: on={}, off={}",
                    id,
                    toggler->m_onButton != nullptr,
                    toggler->m_offButton != nullptr
                );
                if (toggler->m_offButton) {
                    addNativeVisual(menu, id.c_str(), toggler, toggler->m_offButton);
                }
                if (toggler->m_onButton) {
                    addNativeVisual(menu, id.c_str(), toggler, toggler->m_onButton);
                }
                continue;
            }

            auto item = typeinfo_cast<CCMenuItemSpriteExtra*>(child);
            if (!item) {
                log::debug("[OpenGeode] Native management child '{}' is not a sprite menu item", id);
                continue;
            }
            addNativeVisual(menu, id.c_str(), item, item->getNormalImage());
        }

        menu->updateLayout();
        log::info("[OpenGeode] More popup finished with {} children", menu->getChildrenCount());
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

void ensureModPopupExtras(CCNode* popup) {
    auto manageTitle = popup->getChildByIDRecursive("manage-title");
    if (!manageTitle) {
        log::debug("[OpenGeode] Mod popup has no manage-title yet");
        return;
    }

    // This intentionally uses the exact same visible-native-button detection that
    // the old working build used. The parent of that visible button is the menu
    // where Geode puts its management controls, so both OpenGeode controls belong there.
    auto installMenu = getNativeInstallMenu(popup);
    if (!installMenu) return;

    log::info(
        "[OpenGeode] Native management menu found: size={}x{} children={}",
        installMenu->getContentWidth(), installMenu->getContentHeight(),
        installMenu->getChildrenCount()
    );

    auto modID = getPopupModID(popup);
    if (!installMenu->getChildByID("opengeode-from-button"_spr) &&
        !modID.empty() && getInstalledModSource(modID) && isPopupInstalled(popup)) {
        if (auto sprite = createFixedManageButton("From", "GJ_downloadsIcon_001.png")) {
            auto from = CCMenuItemExt::createSpriteExtra(
                sprite,
                [modID](CCMenuItemSpriteExtra*) { showInstallSource(modID); }
            );
            from->setID("opengeode-from-button"_spr);
            installMenu->addChild(from);
            log::info("[OpenGeode] Added From to the same native management menu");
        }
    }

    if (!installMenu->getChildByID("opengeode-more-button"_spr)) {
        if (auto sprite = createThemedManageButton("More", "GJ_filterIcon_001.png")) {
            auto more = CCMenuItemExt::createSpriteExtra(
                sprite,
                [popup](CCMenuItemSpriteExtra*) {
                    MoreManagePopup::create(popup)->show();
                }
            );
            more->setID("opengeode-more-button"_spr);
            installMenu->addChild(more);
            log::info("[OpenGeode] Added More to the same native management menu");
        }
    }

    installMenu->updateLayout();
    log::debug("[OpenGeode] Native management menu layout updated; children={}", installMenu->getChildrenCount());
}

class ModsLayerWatcher : public CCNode {
    async::TaskHolder<web::WebResponse> m_capabilityTask;
    std::string m_capabilityIndex;
    CCMenuItemSpriteExtra* m_accountButton = nullptr;
    bool m_capabilityPending = false;

protected:
    bool init() {
        if (!CCNode::init()) return false;
        this->setID("OpenGeode.mods-layer-watcher"_spr);
        this->schedule(schedule_selector(ModsLayerWatcher::check), 0.25f);
        return true;
    }

    void check(float) {
        auto scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene || scene != this->getParent()) return;
        auto listFrame = scene->getChildByIDRecursive("mod-list-frame");
        if (!listFrame) return;
        auto modList = listFrame->getChildByID("ModList");
        if (!modList) return;
        if (g_switchNotif) { g_switchNotif->cancel(); g_switchNotif = nullptr; }
        if (auto overlay = scene->getChildByID("switch-overlay"_spr)) overlay->removeFromParentAndCleanup(true);

        auto topContainer = modList->getChildByID("top-container");
        if (!topContainer) return;
        auto searchMenu = topContainer->getChildByID("search-menu");
        if (!searchMenu) return;
        auto filtersMenu = typeinfo_cast<CCMenu*>(searchMenu->getChildByID("search-filters-menu"));
        if (!filtersMenu) return;

        ensureIndexSwitcherButton(scene);
        ensureFilterButton(filtersMenu);
        ensureAccountButton(scene);
        ensureModPopupExtras(scene);
    }
