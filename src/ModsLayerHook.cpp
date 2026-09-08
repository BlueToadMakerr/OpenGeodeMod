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

NativeAction getVisibleNativeAction(CCNode* popup, char const* id) {
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

    auto item = typeinfo_cast<CCMenuItemSpriteExtra*>(node);
    if (!item) return {};
    return {item, item->getNormalImage()};
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

void logMoreNodeDetails(char const* label, CCNode* node) {
    if (!node) {
        log::debug("[OpenGeode][More] {}: null", label);
        return;
    }

    auto size = node->getContentSize();
    auto anchor = node->getAnchorPoint();
    log::debug(
        "[OpenGeode][More] {}: ptr={} id='{}' sprite={} iconButton={} visible={} children={} size={}x{} anchor=({}, {}) scale=({}, {}) parent={}",
        label,
        static_cast<void*>(node),
        node->getID(),
        typeinfo_cast<CCSprite*>(node) != nullptr,
        typeinfo_cast<IconButtonSprite*>(node) != nullptr,
        node->isVisible(),
        node->getChildrenCount(),
        size.width,
        size.height,
        anchor.x,
        anchor.y,
        node->getScaleX(),
        node->getScaleY(),
        static_cast<void*>(node->getParent())
    );

    auto index = 0;
    for (auto child : CCArrayExt<CCNode*>(node->getChildren())) {
        if (!child) continue;
        auto childSize = child->getContentSize();
        log::debug(
            "[OpenGeode][More] {} child[{}]: ptr={} id='{}' sprite={} iconButton={} visible={} children={} size={}x{} scale=({}, {})",
            label,
            index,
            static_cast<void*>(child),
            child->getID(),
            typeinfo_cast<CCSprite*>(child) != nullptr,
            typeinfo_cast<IconButtonSprite*>(child) != nullptr,
            child->isVisible(),
            child->getChildrenCount(),
            childSize.width,
            childSize.height,
            child->getScaleX(),
            child->getScaleY()
        );
        ++index;
    }
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
        if (!managementMenu) {
            log::debug("[OpenGeode][More] Cannot build More: native management menu was not found");
            return true;
        }

        log::debug(
            "[OpenGeode][More] Management menu: ptr={} children={} size={}x{} position=({}, {}) scale=({}, {})",
            static_cast<void*>(managementMenu),
            managementMenu->getChildrenCount(),
            managementMenu->getContentSize().width,
            managementMenu->getContentSize().height,
            managementMenu->getPositionX(),
            managementMenu->getPositionY(),
            managementMenu->getScaleX(),
            managementMenu->getScaleY()
        );

        for (auto child : CCArrayExt<CCNode*>(managementMenu->getChildren())) {
            if (!child) continue;
            auto id = child->getID();

            if (id == "opengeode-more-button") continue;

            auto action = typeinfo_cast<CCMenuItem*>(child);
            if (!action) {
                log::debug("[OpenGeode][More] Skip id='{}': child is not a CCMenuItem", id);
                logMoreNodeDetails("non-action child", child);
                continue;
            }

            log::debug(
                "[OpenGeode][More] Candidate id='{}': ptr={} toggler={} spriteExtra={} visible={} children={}",
                id,
                static_cast<void*>(action),
                typeinfo_cast<CCMenuItemToggler*>(action) != nullptr,
                typeinfo_cast<CCMenuItemSpriteExtra*>(action) != nullptr,
                action->isVisible(),
                action->getChildrenCount()
            );

            CCNode* visual = nullptr;
            if (auto toggler = typeinfo_cast<CCMenuItemToggler*>(action)) {
                logMoreNodeDetails("toggler on", toggler->m_onButton);
                logMoreNodeDetails("toggler off", toggler->m_offButton);
                if (toggler->m_onButton && toggler->m_onButton->isVisible()) visual = toggler->m_onButton;
                else if (toggler->m_offButton && toggler->m_offButton->isVisible()) visual = toggler->m_offButton;
            }
            else if (auto spriteItem = typeinfo_cast<CCMenuItemSpriteExtra*>(action)) {
                visual = spriteItem->getNormalImage();
                logMoreNodeDetails("sprite-extra normal", visual);
                logMoreNodeDetails("sprite-extra selected", spriteItem->getSelectedImage());
            }

            if (!visual) {
                log::debug("[OpenGeode][More] id='{}': no visual could be selected", id);
                continue;
            }

            logMoreNodeDetails("selected visual", visual);

            log::debug(
                "[OpenGeode][More] COPY TEST id='{}': attempting copyWithZone(nullptr) on visual={}",
                id,
                static_cast<void*>(visual)
            );
            auto copy = typeinfo_cast<CCNode*>(visual->copyWithZone(nullptr));
            if (!copy) {
                log::debug(
                    "[OpenGeode][More] COPY FAILED id='{}': copyWithZone(nullptr) returned null",
                    id
                );
                continue;
            }

            logMoreNodeDetails("copied visual", copy);

            copy->setPosition({0.f, 0.f});
            copy->setScale(.5f);

            auto item = CCMenuItemExt::createSpriteExtra(
                copy,
                [action, this](CCMenuItemSpriteExtra*) {
                    action->activate();
                    this->onClose(nullptr);
                }
            );
            menu->addChild(item);
            log::debug("[OpenGeode][More] Added id='{}' to More", id);
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
        indexBtn->setScale(0.8f);
        indexBtn->m_baseScale = 0.8f;
        indexBtn->setID("index-switcher-button"_spr);
        actionsMenu->addChild(indexBtn);
        actionsMenu->updateLayout();
    }

    void ensureFilterButton(CCMenu* filtersMenu) {
        if (auto existingBtn = filtersMenu->getChildByID("index-filter-button"_spr)) existingBtn->removeFromParent();
        auto filterBtn = CCMenuItemExt::createSpriteExtra(buildFilterButtonSprite(), [](auto) { showFilterPopup(); });
        filterBtn->setID("index-filter-button"_spr);
        filtersMenu->addChild(filterBtn, -100);
        filtersMenu->updateLayout();
    }

    void ensureAccountButton(CCNode* scene) {
        auto backMenu = typeinfo_cast<CCMenu*>(scene->getChildByIDRecursive("back-menu"));
        if (!backMenu) return;
        auto currentIndex = getIndexUrl();
        if (currentIndex != m_capabilityIndex) {
            m_capabilityIndex = currentIndex;
            m_capabilityPending = false;
            m_capabilityTask.cancel();
            if (m_accountButton) {
                m_accountButton->removeFromParent();
                m_accountButton = nullptr;
            }
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
            m_accountButton->setScale(.8f);
            m_accountButton->m_baseScale = .8f;
            m_accountButton->setID("opengeode-account-button"_spr);
            backMenu->addChild(m_accountButton);
            backMenu->updateLayout();
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
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
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
