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

bool isNativeButtonVisible(CCNode* popup, char const* id) {
    auto node = popup->getChildByIDRecursive(id);
    return node && node->isVisible();
}

bool isPopupInstalled(CCNode* popup) {
    return isNativeButtonVisible(popup, "uninstall-button") ||
        isNativeButtonVisible(popup, "update-button") ||
        isNativeButtonVisible(popup, "enable-button") ||
        isNativeButtonVisible(popup, "reenable-button");
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
    createQuickPopup("Install Source", description, "OK", "", [](FLAlertLayer*, bool) {});
}

CCMenuItemSpriteExtra* makeGeodeActionButton(
    CCNode* nativeNode,
    char const* id,
    std::function<void(CCMenuItemSpriteExtra*)> callback
) {
    auto native = typeinfo_cast<CCMenuItemSpriteExtra*>(nativeNode);
    if (!native || !native->isVisible()) return nullptr;

    CCNode* sprite = nullptr;
    if (std::string(id) == "update-button") {
        sprite = createGeodeButton(
            CCSprite::createWithSpriteFrameName("update.png"_spr), "Update",
            GeodeButtonSprite::Install, false
        );
    }
    else if (std::string(id) == "install-button") {
        sprite = createGeodeButton(
            CCSprite::createWithSpriteFrameName("GJ_downloadsIcon_001.png"), "Install",
            GeodeButtonSprite::Install, false
        );
    }
    else if (std::string(id) == "uninstall-button") {
        sprite = createGeodeButton(
            CCSprite::createWithSpriteFrameName("delete-white.png"_spr), "Uninstall",
            GeodeButtonSprite::Default, false
        );
    }
    else if (std::string(id) == "cancel-button") {
        sprite = createGeodeButton(
            CCSprite::createWithSpriteFrameName("GJ_deleteIcon_001.png"), "Cancel",
            GeodeButtonSprite::Default, false
        );
    }
    else if (std::string(id) == "unavailable-button") {
        sprite = createGeodeButton(
            CCSprite::createWithSpriteFrameName("exclamation.png"_spr), "Unavailable",
            GeodeButtonSprite::Gray, false
        );
        if (sprite) sprite->setColor({155, 155, 155});
    }
    else if (std::string(id) == "enable-button") {
        auto toggler = typeinfo_cast<CCMenuItemToggler*>(nativeNode);
        bool disabling = toggler && toggler->m_onButton && toggler->m_onButton->isVisible();
        sprite = createGeodeButton(
            CCSprite::createWithSpriteFrameName(disabling ? "GJ_deleteIcon_001.png" : "GJ_completesIcon_001.png"),
            disabling ? "Disable" : "Enable",
            disabling ? GeodeButtonSprite::Delete : GeodeButtonSprite::Enable, false
        );
    }
    else if (std::string(id) == "reenable-button") {
        auto toggler = typeinfo_cast<CCMenuItemToggler*>(nativeNode);
        bool disabling = toggler && toggler->m_onButton && toggler->m_onButton->isVisible();
        sprite = createGeodeButton(
            CCSprite::createWithSpriteFrameName("reset.png"_spr),
            disabling ? "Re-Disable" : "Re-Enable",
            GeodeButtonSprite::Default, false
        );
    }

    if (!sprite) return nullptr;
    sprite->setScale(.5f);
    return CCMenuItemExt::createSpriteExtra(sprite, std::move(callback));
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
                ButtonSprite::create("From", 40, true, "goldFont.fnt", "GJ_button_01.png", 25.f, .6f),
                [modID](CCMenuItemSpriteExtra*) { showInstallSource(modID); }
            );
            menu->addChild(item);
        }

        for (auto const& id : {
            "update-button", "enable-button", "reenable-button", "unavailable-button",
            "install-button", "uninstall-button", "cancel-button"
        }) {
            auto native = m_modPopup->getChildByIDRecursive(id);
            auto item = makeGeodeActionButton(native, id, [this, native](CCMenuItemSpriteExtra*) {
                this->onClose(nullptr);
                if (auto menuItem = typeinfo_cast<CCMenuItem*>(native)) menuItem->activate();
            });
            if (item) menu->addChild(item);
        }

        menu->updateLayout();
        return true;
    }

public:
    static MoreManagePopup* create(CCNode* modPopup) {
        auto ret = new MoreManagePopup();
        if (ret && ret->init(modPopup)) { ret->autorelease(); return ret; }
        delete ret;
        return nullptr;
    }
};

void ensureModPopupExtras(CCNode* popup) {
    auto manageTitle = popup->getChildByIDRecursive("manage-title");
    if (!manageTitle) return;

    // Geode's actual action row is the installContainer immediately below the
    // Manage/status row. Find it through one of Geode's native action buttons
    // rather than modifying the private ModPopup implementation.
    auto installButton = popup->getChildByIDRecursive("install-button");
    if (!installButton) return;
    auto installMenu = installButton->getParent();
    auto installContainer = installMenu ? installMenu->getParent() : nullptr;
    if (!installContainer || installContainer->getChildByID("opengeode-manage-menu"_spr)) return;

    auto customMenu = CCMenu::create();
    customMenu->setID("opengeode-manage-menu"_spr);
    customMenu->setContentSize({90.f, 25.f});
    customMenu->setAnchorPoint({1.f, .5f});
    customMenu->setPosition({installContainer->getContentWidth() - 5.f, installContainer->getContentHeight() / 2.f});
    customMenu->setLayout(RowLayout::create()->setGap(4.f)->setAxisAlignment(AxisAlignment::Center));
    installContainer->addChild(customMenu);

    auto modID = getPopupModID(popup);
    if (!modID.empty() && getInstalledModSource(modID) && isPopupInstalled(popup)) {
        auto from = CCMenuItemExt::createSpriteExtra(
            ButtonSprite::create("From", 40, true, "goldFont.fnt", "GJ_button_01.png", 25.f, .6f),
            [modID](CCMenuItemSpriteExtra*) { showInstallSource(modID); }
        );
        customMenu->addChild(from);
    }

    auto more = CCMenuItemExt::createSpriteExtra(
        ButtonSprite::create("More", 40, true, "goldFont.fnt", "GJ_button_01.png", 25.f, .6f),
        [popup](CCMenuItemSpriteExtra*) { MoreManagePopup::create(popup)->show(); }
    );
    customMenu->addChild(more);
    customMenu->updateLayout();
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

}

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

}
