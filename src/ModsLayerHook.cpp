#include "FilterPopup.hpp"
#include "IndexListPopup.hpp"
#include "ModsListUtils.hpp"
#include "AccountPopup.hpp"
#include "PopupSectionUtils.hpp"
#include "InstalledMods.hpp"
#include "Settings.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/SceneEvent.hpp>
#include <Geode/utils/web.hpp>

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
    std::string value = label->getString().c_str();
    auto prefix = std::string("(ID: ");
    if (!value.starts_with(prefix) || value.size() < prefix.size() + 1) return "";
    value.erase(0, prefix.size());
    if (!value.empty() && value.back() == ')') value.pop_back();
    return value;
}

void showInstallSource(std::string const& modID) {
    auto source = getInstalledModSource(modID);
    if (!source) return;

    auto description = fmt::format(
        "Installed from <cy>{}</c>\nVersion: <cg>{}</c>\n{}",
        source->indexName,
        source->version,
        source->indexUrl
    );
    createQuickPopup("Install Source", description, "OK");
}

class MoreManagePopup : public Popup {
protected:
    CCNode* m_modPopup = nullptr;

    bool init(CCNode* modPopup) {
        if (!Popup::init(190.f, 255.f, getPopupBackground())) return false;
        m_modPopup = modPopup;
        setTitle("More");
        if (auto close = createGeodeCloseButton()) setCloseButtonSpr(close, .8f);

        auto menu = CCMenu::create();
        menu->setContentSize({160.f, 205.f});
        menu->setAnchorPoint({.5f, .5f});
        menu->setPosition({95.f, 120.f});

        struct NativeAction {
            char const* id;
            char const* label;
        };

        for (auto const& action : std::initializer_list<NativeAction>{
            {"update-button", "Update"},
            {"enable-button", "Enable"},
            {"reenable-button", "Re-Enable"},
            {"unavailable-button", "Unavailable"},
            {"install-button", "Install"},
            {"uninstall-button", "Uninstall"},
            {"cancel-button", "Cancel"},
        }) {
            auto native = m_modPopup->getChildByIDRecursive(action.id);
            if (!native || !native->isVisible()) continue;

            auto button = CCMenuItemExt::createSpriteExtra(
                ButtonSprite::create(action.label, "goldFont.fnt", getButtonTexture("GJ_button_01.png"), .45f),
                [this, native](auto) {
                    onClose(nullptr);
                    if (auto item = typeinfo_cast<CCMenuItem*>(native)) item->activate();
                }
            );
            button->setID(fmt::format("opengeode-more-{}", action.id));
            menu->addChild(button);
        }

        menu->setLayout(
            ColumnLayout::create()
                ->setGap(5.f)
                ->setAxisAlignment(AxisAlignment::Center)
        );
        menu->updateLayout();
        m_mainLayer->addChild(menu);
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
    if (!popup || popup->getChildByID("opengeode-manage-extras"_spr)) return;

    auto manageTitle = popup->getChildByIDRecursive("manage-title");
    if (!manageTitle) return;

    auto manageContainer = manageTitle->getParent();
    if (!manageContainer) return;

    auto extras = CCMenu::create();
    extras->setID("opengeode-manage-extras"_spr);
    extras->setContentSize({85.f, 18.f});
    extras->setAnchorPoint({1.f, .5f});

    auto modID = getPopupModID(popup);
    auto source = getInstalledModSource(modID);

    if (source) {
        auto from = CCMenuItemExt::createSpriteExtra(
            ButtonSprite::create("From", "goldFont.fnt", getButtonTexture("GJ_button_01.png"), .32f),
            [modID](auto) { showInstallSource(modID); }
        );
        from->setID("opengeode-from-button"_spr);
        extras->addChild(from);
    }

    auto more = CCMenuItemExt::createSpriteExtra(
        ButtonSprite::create("More", "goldFont.fnt", getButtonTexture("GJ_button_01.png"), .32f),
        [popup](auto) {
            if (auto morePopup = MoreManagePopup::create(popup)) morePopup->show();
        }
    );
    more->setID("opengeode-more-button"_spr);
    extras->addChild(more);

    extras->setLayout(RowLayout::create()->setGap(3.f)->setAxisAlignment(AxisAlignment::End));
    extras->updateLayout();
    manageContainer->addChildAtPosition(extras, Anchor::Right, ccp(0.f, 0.f), ccp(1.f, .5f));
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

        ensureOpenGeodeModPopupExtras(scene);

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
    }

    void ensureOpenGeodeModPopupExtras(CCNode* scene) {
        auto manageTitle = scene->getChildByIDRecursive("manage-title");
        if (!manageTitle) return;

        // Walk upward until we reach the ModPopup that owns the Manage section.
        auto popup = manageTitle;
        while (popup) {
            if (popup->getChildByIDRecursive("mod-id-label")) break;
            popup = popup->getParent();
        }
        if (popup) ensureModPopupExtras(popup);
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
