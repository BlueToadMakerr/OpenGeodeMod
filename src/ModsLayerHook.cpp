#include "FilterPopup.hpp"
#include "IndexListPopup.hpp"
#include "ModsListUtils.hpp"
#include "AccountPopup.hpp"
#include "InstalledMods.hpp"
#include "PopupSectionUtils.hpp"
#include "Settings.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>
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
    CCMenuItemSpriteExtra* visual = nullptr;
};

NativeAction getNativeAction(CCNode* popup, char const* id) {
    auto node = popup->getChildByIDRecursive(id);
    if (!node || !node->isVisible()) return {};

    if (auto toggler = typeinfo_cast<CCMenuItemToggler*>(node)) {
        if (toggler->m_onButton && toggler->m_onButton->isVisible()) {
            return {toggler, toggler->m_onButton};
        }
        if (toggler->m_offButton && toggler->m_offButton->isVisible()) {
            return {toggler, toggler->m_offButton};
        }
        return {};
    }

    if (auto item = typeinfo_cast<CCMenuItemSpriteExtra*>(node)) {
        return {item, item};
    }
    return {};
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
        auto action = getNativeAction(popup, id);
        if (!action.action) continue;
        auto menu = typeinfo_cast<CCMenu*>(action.action->getParent());
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
    createQuickPopup(
        "Install Source", description, "OK", "",
        [](FLAlertLayer*, bool) {}
    );
}

CCMenuItemSpriteExtra* cloneNativeButton(CCMenuItemSpriteExtra* native, CCObject* target, SEL_MenuHandler selector) {
    auto source = native->getNormalImage();
    if (!source) return nullptr;
    auto copied = typeinfo_cast<CCNode*>(source->copyWithZone(nullptr));
    if (!copied) return nullptr;
    auto item = CCMenuItemSpriteExtra::create(copied, target, selector);
    if (!item) return nullptr;
    item->setScale(.5f);
    return item;
}

ButtonSprite* createOpenGeodeButton(char const* text) {
    return ButtonSprite::create(text, 40, 40, 1.f, true);
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
        menu->setPosition({20.f, 35.f});
        menu->setLayout(ColumnLayout::create()->setGap(6.f)->setAxisAlignment(AxisAlignment::Center));
        m_mainLayer->addChild(menu);

        auto modID = getPopupModID(modPopup);
        if (!modID.empty() && getInstalledModSource(modID) && isPopupInstalled(modPopup)) {
            auto item = CCMenuItemExt::createSpriteExtra(
                createOpenGeodeButton("From"),
                [modID](CCMenuItemSpriteExtra*) { showInstallSource(modID); }
            );
            menu->addChild(item);
        }

        for (auto const& id : {
            "update-button", "enable-button", "reenable-button", "unavailable-button",
            "install-button", "uninstall-button", "cancel-button"
        }) {
            auto native = getNativeAction(modPopup, id);
            if (!native.action || !native.visual) continue;
            auto item = cloneNativeButton(native.visual, this, menu_selector(MoreManagePopup::onNativeAction));
            if (!item) continue;
            item->setUserObject("OpenGeode.native-action"_spr, native.action);
            menu->addChild(item);
        }
        menu->updateLayout();
        return true;
    }

    void onNativeAction(CCObject* sender) {
        auto item = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
        if (!item) return;
        auto native = static_cast<CCMenuItem*>(item->getUserObject("OpenGeode.native-action"_spr));
        if (!native) return;
        this->onClose(nullptr);
        native->activate();
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
    if (!manageTitle) return;

    // ModPopup.cpp creates `manageContainer`, then a separate `installContainer`
    // directly below it. The actual native buttons live in installContainer's
    // m_installMenu. Find that exact menu through one of Geode's own buttons
    // instead of guessing from the Manage node hierarchy.
    auto installMenu = getNativeInstallMenu(popup);
    if (!installMenu) return;

    auto modID = getPopupModID(popup);
    if (!installMenu->getChildByID("opengeode-from-button"_spr) &&
        !modID.empty() && getInstalledModSource(modID) && isPopupInstalled(popup)) {
        auto from = CCMenuItemExt::createSpriteExtra(
            createOpenGeodeButton("From"),
            [modID](CCMenuItemSpriteExtra*) { showInstallSource(modID); }
        );
        from->setID("opengeode-from-button"_spr);
        installMenu->addChild(from);
    }

    if (!installMenu->getChildByID("opengeode-more-button"_spr)) {
        auto more = CCMenuItemExt::createSpriteExtra(
            createOpenGeodeButton("More"),
            [popup](CCMenuItemSpriteExtra*) {
                MoreManagePopup::create(popup)->show();
            }
        );
        more->setID("opengeode-more-button"_spr);
        installMenu->addChild(more);
    }

    // Use the same RowLayout Geode assigned to m_installMenu so our controls
    // occupy the exact management row below the Manage heading.
    installMenu->updateLayout();
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

    void ensureIndexSwitcherButton(CCNode* scene) {
        auto actionsMenu = typeinfo_cast<CCMenu*>(scene->getChildByIDRecursive("actions-menu"));
        if (!actionsMenu || actionsMenu->getChildByID("index-switcher-button"_spr)) return;
        auto indexBtn = CCMenuItemExt::createSpriteExtra(
            CircleButtonSprite::createWithSpriteFrameName("geode.loader/geode-logo.png", 0.85f, CircleBaseColor::Blue),
            [](auto) { showIndexListPopup(); }
        );
        indexBtn->setScale(0.8f); indexBtn->m_baseScale = 0.8f; indexBtn->setID("index-switcher-button"_spr);
        actionsMenu->addChild(indexBtn); actionsMenu->updateLayout();
    }

    void ensureFilterButton(CCMenu* filtersMenu) {
        if (auto existingBtn = filtersMenu->getChildByID("index-filter-button"_spr)) existingBtn->removeFromParent();
        auto filterBtn = CCMenuItemExt::createSpriteExtra(buildFilterButtonSprite(), [](auto) { showFilterPopup(); });
        filterBtn->setID("index-filter-button"_spr); filtersMenu->addChild(filterBtn, -100); filtersMenu->updateLayout();
    }

    void ensureAccountButton(CCNode* scene) {
        auto backMenu = typeinfo_cast<CCMenu*>(scene->getChildByIDRecursive("back-menu"));
        if (!backMenu) return;
        auto currentIndex = getIndexUrl();
        if (currentIndex != m_capabilityIndex) {
            m_capabilityIndex = currentIndex;
            m_capabilityPending = false;
            m_capabilityTask.cancel();
            if (m_accountButton) { m_accountButton->removeFromParent(); m_accountButton = nullptr; }
        }
        if (m_accountButton || m_capabilityPending) return;
        m_capabilityPending = true;

        auto req = web::WebRequest();
        req.header("Accept", "application/json");
        m_capabilityTask.spawn(req.get(trimSlash(currentIndex) + "/OpenGeode"), [this, backMenu](web::WebResponse res) {
            m_capabilityPending = false;
            if (!res.ok()) return;
            auto json = res.json().unwrapOr(matjson::Value());
            if (!json["enabled"].asBool().unwrapOr(false) || !json["allowGdLogin"].asBool().unwrapOr(false)) return;
            auto sprite = createProfileButtonSprite();
            if (!sprite) return;
            m_accountButton = CCMenuItemSpriteExtra::create(sprite, this, menu_selector(ModsLayerWatcher::onAccount));
            m_accountButton->setScale(.8f); m_accountButton->m_baseScale = .8f;
            m_accountButton->setID("opengeode-account-button"_spr);
            backMenu->addChild(m_accountButton); backMenu->updateLayout();
        });
    }

    void onAccount(CCObject*) { showAccountPopup(); }

    static std::string trimSlash(std::string url) {
        while (!url.empty() && url.back() == '/') url.pop_back();
        return url;
    }

public:
    static ModsLayerWatcher* create() {
        auto ret = new ModsLayerWatcher();
        if (ret && ret->init()) { ret->autorelease(); return ret; }
        delete ret; return nullptr;
    }
};

} // namespace

$on_mod(Loaded) {
    ensurePresetsExist();
    SceneEvent().listen([](CCScene* scene) {
        if (!scene) return ListenerResult::Propagate;
        if (scene->getChildByID("OpenGeode.mods-layer-watcher"_spr)) return ListenerResult::Propagate;
        auto watcher = ModsLayerWatcher::create();
        if (watcher) scene->addChild(watcher);
        return ListenerResult::Propagate;
    }).leak();
}

} // namespace opengeode
