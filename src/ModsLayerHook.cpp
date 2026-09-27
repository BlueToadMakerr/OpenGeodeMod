#include "FilterPopup.hpp"
#include "IndexListPopup.hpp"
#include "ModsListUtils.hpp"
#include "AccountPopup.hpp"
#include "MoreManagePopup.hpp"
#include "VersionsPopup.hpp"
#include "Settings.hpp"
#include "IndexUpdates.hpp"

#include <Geode/Geode.hpp>
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

class ModsLayerWatcher : public CCNode {
    async::TaskHolder<web::WebResponse> m_capabilityTask;
    std::string m_capabilityIndex;
    CCMenuItemSpriteExtra* m_accountButton = nullptr;
    CCNode* m_updateBadge = nullptr;
    bool m_capabilityPending = false;
    bool m_capabilityAttempted = false;
    bool m_inModsLayer = false;

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
        if (!listFrame) {
            if (m_inModsLayer) {
                m_inModsLayer = false;
                m_capabilityAttempted = false;
                m_capabilityPending = false;
                m_capabilityTask.cancel();
                m_capabilityIndex.clear();
                if (m_accountButton) {
                    m_accountButton->removeFromParent();
                    m_accountButton = nullptr;
                }
            }
            return;
        }

        if (!m_inModsLayer) {
            m_inModsLayer = true;
            m_capabilityAttempted = false;
            m_capabilityPending = false;
        }

        if (g_switchNotif) { g_switchNotif->cancel(); g_switchNotif = nullptr; }
        if (auto overlay = scene->getChildByID("switch-overlay"_spr)) overlay->removeFromParentAndCleanup(true);

        auto modList = listFrame->getChildByID("ModList");
        if (!modList) return;

        auto topContainer = modList->getChildByID("top-container");
        if (!topContainer) return;
        auto searchMenu = topContainer->getChildByID("search-menu");
        if (!searchMenu) return;
        auto filtersMenu = typeinfo_cast<CCMenu*>(searchMenu->getChildByID("search-filters-menu"));
        if (!filtersMenu) return;

        ensureIndexSwitcherButton(scene);
        if (!indexUpdatesLoading() && !hasFreshIndexUpdateCache()) {
            fetchIndexUpdates([this] {
                auto scene = CCDirector::sharedDirector()->getRunningScene();
                if (scene) ensureIndexSwitcherButton(scene);
            });
        }
        ensureFilterButton(filtersMenu);
        ensureAccountButton(scene);
        ensureVersionsButton(scene);
        ensureModPopupExtras(scene);
    }

    void ensureIndexSwitcherButton(CCNode* scene) {
        auto actionsMenu = typeinfo_cast<CCMenu*>(scene->getChildByIDRecursive("actions-menu"));
        if (!actionsMenu) return;

        auto indexBtn = typeinfo_cast<CCMenuItemSpriteExtra*>(actionsMenu->getChildByID("index-switcher-button"_spr));
        if (!indexBtn) {
            indexBtn = CCMenuItemExt::createSpriteExtra(
                CircleButtonSprite::createWithSpriteFrameName("geode.loader/geode-logo.png", 0.85f, CircleBaseColor::Blue),
                [](auto) { showIndexListPopup(); }
            );
            indexBtn->setScale(0.8f);
            indexBtn->m_baseScale = 0.8f;
            indexBtn->setID("index-switcher-button"_spr);
            actionsMenu->addChild(indexBtn);
        }

        auto count = getTotalUpdateCount();
        if (m_updateBadge) {
            m_updateBadge->removeFromParentAndCleanup(true);
            m_updateBadge = nullptr;
        }

        if (count > 0) {
            auto badge = CCNode::create();
            badge->setContentSize({40.5f, 40.5f});
            badge->setAnchorPoint({.5f, .5f});

            auto icon = CCSprite::createWithSpriteFrameName("geode.loader/updates-available.png");
            if (icon) {
                icon->setScale(.825f);
                icon->setPosition({20.25f, 20.25f});
                badge->addChild(icon);
            }

            auto label = CCLabelBMFont::create(std::to_string(count).c_str(), "bigFont.fnt");
            label->setScale(.42f);
            label->setAnchorPoint({.5f, .5f});
            label->setPosition({20.25f, 20.25f});
            badge->addChild(label, 2);

            badge->setPosition({indexBtn->getContentWidth() - 7.f, indexBtn->getContentHeight() - 7.f});
            indexBtn->addChild(badge, 10);
            m_updateBadge = badge;
        }

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
        if (!backMenu || m_capabilityAttempted || m_capabilityPending || m_accountButton) return;

        auto currentIndex = getIndexUrl();
        if (currentIndex.empty()) return;

        m_capabilityIndex = currentIndex;
        m_capabilityAttempted = true;
        m_capabilityPending = true;

        auto req = web::WebRequest();
        req.header("Accept", "application/json");
        auto token = getAuthAccessToken();
        if (!token.empty())
            req.header("Authorization", "Bearer " + token);
        m_capabilityTask.spawn(req.get(trimSlash(currentIndex) + "/OpenGeode"), [this, backMenu](web::WebResponse res) {
            m_capabilityPending = false;
            auto hasSavedAccessToken = hasAuthAccessToken();
            if (hasSavedAccessToken) {
                auto sprite = createProfileButtonSprite();
                if (!sprite) return;
                m_accountButton = CCMenuItemSpriteExtra::create(sprite, this, menu_selector(ModsLayerWatcher::onAccount));
                m_accountButton->setScale(.8f);
                m_accountButton->m_baseScale = .8f;
                m_accountButton->setID("opengeode-account-button"_spr);
                backMenu->addChild(m_accountButton);
                backMenu->updateLayout();
                return;
            }
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
