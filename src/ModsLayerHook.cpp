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

void logNode(const char* tag, CCNode* node) {
    if (!node) {
        log::debug("[OpenGeode][Mgmt] {}: null", tag);
        return;
    }
    log::debug(
        "[OpenGeode][Mgmt] {}: node={} id='{}' visible={} parent={} children={}",
        tag,
        static_cast<void*>(node),
        node->getID(),
        node->isVisible(),
        static_cast<void*>(node->getParent()),
        node->getChildrenCount()
    );
}

void logParentChain(const char* tag, CCNode* node) {
    log::debug("[OpenGeode][Mgmt] ---- parent chain: {} ----", tag);
    auto current = node;
    int depth = 0;
    while (current && depth < 12) {
        log::debug(
            "[OpenGeode][Mgmt] chain[{}]: node={} id='{}' visible={} parent={}",
            depth,
            static_cast<void*>(current),
            current->getID(),
            current->isVisible(),
            static_cast<void*>(current->getParent())
        );
        current = current->getParent();
        ++depth;
    }
    log::debug("[OpenGeode][Mgmt] ---- end parent chain ----");
}

struct NativeAction {
    CCMenuItem* action = nullptr;
    CCNode* visual = nullptr;
};

NativeAction getVisibleNativeAction(CCNode* popup, char const* id) {
    log::debug("[OpenGeode][Mgmt] Looking for native id='{}' from popup={}", id, static_cast<void*>(popup));
    auto node = popup->getChildByIDRecursive(id);
    logNode("recursive lookup result", node);
    if (!node) {
        log::debug("[OpenGeode][Mgmt] id='{}' FAILED: recursive lookup returned null", id);
        return {};
    }
    if (!node->isVisible()) {
        log::debug("[OpenGeode][Mgmt] id='{}' FAILED: node exists but is not visible", id);
        logParentChain("hidden native action", node);
        return {};
    }

    if (auto toggler = typeinfo_cast<CCMenuItemToggler*>(node)) {
        log::debug("[OpenGeode][Mgmt] id='{}' is toggler={} on={} off={}", id, static_cast<void*>(toggler), static_cast<void*>(toggler->m_onButton), static_cast<void*>(toggler->m_offButton));
        if (toggler->m_onButton && toggler->m_onButton->isVisible()) {
            log::debug("[OpenGeode][Mgmt] id='{}' using VISIBLE onButton={}", id, static_cast<void*>(toggler->m_onButton));
            logParentChain("toggler/onButton", toggler);
            return {toggler, toggler->m_onButton};
        }
        if (toggler->m_offButton && toggler->m_offButton->isVisible()) {
            log::debug("[OpenGeode][Mgmt] id='{}' using VISIBLE offButton={}", id, static_cast<void*>(toggler->m_offButton));
            logParentChain("toggler/offButton", toggler);
            return {toggler, toggler->m_offButton};
        }
        log::debug("[OpenGeode][Mgmt] id='{}' FAILED: toggler has no visible state child", id);
        return {};
    }

    auto item = typeinfo_cast<CCMenuItemSpriteExtra*>(node);
    if (!item) {
        log::debug("[OpenGeode][Mgmt] id='{}' FAILED: node is not CCMenuItemSpriteExtra", id);
        logParentChain("invalid native action", node);
        return {};
    }
    log::debug("[OpenGeode][Mgmt] id='{}' is sprite action={} visual={}", id, static_cast<void*>(item), static_cast<void*>(item->getNormalImage()));
    logParentChain("sprite action", item);
    return {item, item->getNormalImage()};
}

bool isPopupInstalled(CCNode* popup) {
    return getVisibleNativeAction(popup, "uninstall-button").action ||
        getVisibleNativeAction(popup, "update-button").action ||
        getVisibleNativeAction(popup, "enable-button").action ||
        getVisibleNativeAction(popup, "reenable-button").action;
}

CCMenu* getNativeManagementMenu(CCNode* popup) {
    log::debug("[OpenGeode][Mgmt] ===== FIND MANAGEMENT MENU popup={} =====", static_cast<void*>(popup));
    for (auto const& id : {
        "update-button", "enable-button", "reenable-button", "unavailable-button",
        "install-button", "uninstall-button", "cancel-button"
    }) {
        auto native = getVisibleNativeAction(popup, id);
        if (!native.action) {
            log::debug("[OpenGeode][Mgmt] id='{}' did not produce an action", id);
            continue;
        }
        auto parent = native.action->getParent();
        logNode("native action parent", parent);
        auto menu = typeinfo_cast<CCMenu*>(parent);
        if (menu) {
            log::debug("[OpenGeode][Mgmt] FOUND management menu={} from id='{}'", static_cast<void*>(menu), id);
            logParentChain("management menu", menu);
            log::debug("[OpenGeode][Mgmt] Management menu children:");
            for (auto child : CCArrayExt<CCNode*>(menu->getChildren())) {
                if (!child) continue;
                log::debug(
                    "[OpenGeode][Mgmt] child={} id='{}' visible={} parent={} children={}",
                    static_cast<void*>(child),
                    child->getID(),
                    child->isVisible(),
                    static_cast<void*>(child->getParent()),
                    child->getChildrenCount()
                );
            }
            return menu;
        }
        log::debug("[OpenGeode][Mgmt] id='{}' parent was NOT a CCMenu", id);
    }
    log::debug("[OpenGeode][Mgmt] ===== MANAGEMENT MENU NOT FOUND =====");
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

IconButtonSprite* createFixedManageButton(char const* text, char const* iconFrame) {
    auto icon = CCSprite::createWithSpriteFrameName(iconFrame);
    if (!icon) return nullptr;
    auto button = IconButtonSprite::create("GJ_button_01.png", icon, text, "bigFont.fnt");
    if (button) button->setScale(.5f);
    return button;
}

IconButtonSprite* createThemedManageButton(char const* text, char const* iconFrame) {
    auto icon = CCSprite::createWithSpriteFrameName(iconFrame);
    if (!icon) return nullptr;
    auto button = IconButtonSprite::create(getButtonTexture("GJ_button_01.png"), icon, text, "bigFont.fnt");
    if (button) button->setScale(.5f);
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

        log::debug("[OpenGeode][Mgmt] ===== BUILD MORE POPUP modPopup={} =====", static_cast<void*>(modPopup));
        auto managementMenu = getNativeManagementMenu(modPopup);
        if (!managementMenu) {
            log::debug("[OpenGeode][Mgmt] More popup: NO management menu, aborting button copy");
            return true;
        }

        log::debug("[OpenGeode][Mgmt] More popup reading DIRECT children from managementMenu={}", static_cast<void*>(managementMenu));
        for (auto child : CCArrayExt<CCNode*>(managementMenu->getChildren())) {
            if (!child) continue;
            log::debug("[OpenGeode][Mgmt] More candidate child={} id='{}' visible={}", static_cast<void*>(child), child->getID(), child->isVisible());

            if (child->getID() == "opengeode-more-button") {
                log::debug("[OpenGeode][Mgmt] Skipping our More button");
                continue;
            }

            auto action = typeinfo_cast<CCMenuItem*>(child);
            if (!action) {
                log::debug("[OpenGeode][Mgmt] Skipping child id='{}': not CCMenuItem", child->getID());
                continue;
            }

            CCNode* visual = nullptr;
            if (auto toggler = typeinfo_cast<CCMenuItemToggler*>(action)) {
                log::debug("[OpenGeode][Mgmt] Candidate id='{}' is toggler={} on={} off={}", child->getID(), static_cast<void*>(toggler), static_cast<void*>(toggler->m_onButton), static_cast<void*>(toggler->m_offButton));
                if (toggler->m_onButton && toggler->m_onButton->isVisible()) visual = toggler->m_onButton;
                else if (toggler->m_offButton && toggler->m_offButton->isVisible()) visual = toggler->m_offButton;
            }
            else if (auto spriteItem = typeinfo_cast<CCMenuItemSpriteExtra*>(action)) {
                visual = spriteItem->getNormalImage();
            }

            if (!visual) {
                log::debug("[OpenGeode][Mgmt] Candidate id='{}' FAILED: no visible visual", child->getID());
                continue;
            }

            logNode("visual selected for More", visual);
            auto copy = typeinfo_cast<CCNode*>(visual->copyWithZone(nullptr));
            if (!copy) {
                log::debug("[OpenGeode][Mgmt] Candidate id='{}' FAILED: visual copy failed", child->getID());
                continue;
            }
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
            log::debug("[OpenGeode][Mgmt] Added candidate id='{}' to More", child->getID());
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

    log::debug("[OpenGeode][Mgmt] ===== ENSURE EXTRAS popup={} =====", static_cast<void*>(popup));
    logParentChain("manage-title", manageTitle);

    auto managementMenu = getNativeManagementMenu(popup);
    if (!managementMenu) {
        log::debug("[OpenGeode][Mgmt] Extras: FAILED to locate management menu");
        return;
    }

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
            log::debug("[OpenGeode][Mgmt] Added From button={} to managementMenu={}", static_cast<void*>(from), static_cast<void*>(managementMenu));
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
            log::debug("[OpenGeode][Mgmt] Added More button={} to managementMenu={}", static_cast<void*>(more), static_cast<void*>(managementMenu));
        }
    }

    log::debug("[OpenGeode][Mgmt] Final management menu children after OpenGeode additions:");
    for (auto child : CCArrayExt<CCNode*>(managementMenu->getChildren())) {
        if (!child) continue;
        log::debug("[OpenGeode][Mgmt] final child={} id='{}' visible={}", static_cast<void*>(child), child->getID(), child->isVisible());
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
