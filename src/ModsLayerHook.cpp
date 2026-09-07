#include "FilterPopup.hpp"
#include "IndexListPopup.hpp"
#include "ModsListUtils.hpp"
#include "AccountPopup.hpp"
#include "Settings.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/SceneEvent.hpp>
#include <Geode/utils/web.hpp>

using namespace geode::prelude;

namespace opengeode {

Notification* g_switchNotif = nullptr;

namespace {

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

        if (g_switchNotif) {
            g_switchNotif->cancel();
            g_switchNotif = nullptr;
        }
        if (auto overlay = scene->getChildByID("switch-overlay"_spr)) {
            overlay->removeFromParentAndCleanup(true);
        }

        auto topContainer = modList->getChildByID("top-container");
        if (!topContainer) return;
        auto searchMenu = topContainer->getChildByID("search-menu");
        if (!searchMenu) return;
        auto filtersMenu = typeinfo_cast<CCMenu*>(searchMenu->getChildByID("search-filters-menu"));
        if (!filtersMenu) return;

        this->ensureIndexSwitcherButton(scene);
        this->ensureFilterButton(filtersMenu);
        this->ensureAccountButton(scene);
    }

    void ensureIndexSwitcherButton(CCNode* scene) {
        auto actionsMenu = typeinfo_cast<CCMenu*>(scene->getChildByIDRecursive("actions-menu"));
        if (!actionsMenu) return;
        if (actionsMenu->getChildByID("index-switcher-button"_spr)) return;

        auto indexBtn = CCMenuItemExt::createSpriteExtra(
            CircleButtonSprite::createWithSpriteFrameName(
                "geode.loader/geode-logo.png", 0.85f, CircleBaseColor::Blue
            ),
            [](auto) { showIndexListPopup(); }
        );
        indexBtn->setScale(0.8f);
        indexBtn->m_baseScale = 0.8f;
        indexBtn->setID("index-switcher-button"_spr);
        actionsMenu->addChild(indexBtn);
        actionsMenu->updateLayout();
    }

    void ensureFilterButton(CCMenu* filtersMenu) {
        if (auto existingBtn = filtersMenu->getChildByID("index-filter-button"_spr)) {
            existingBtn->removeFromParent();
        }
        auto filterBtn = CCMenuItemExt::createSpriteExtra(
            buildFilterButtonSprite(), [](auto) { showFilterPopup(); }
        );
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
            if (json["enabled"].asBool().unwrapOr(false) != true ||
                json["allowGdLogin"].asBool().unwrapOr(false) != true) {
                return;
            }

            auto sprite = CCSprite::createWithSpriteFrameName("Gj_profileButton_001.png");
            if (!sprite) return;
            sprite->setScale(0.8f);
            m_accountButton = CCMenuItemSpriteExtra::create(
                sprite,
                this,
                menu_selector(ModsLayerWatcher::onAccount)
            );
            m_accountButton->setID("opengeode-account-button"_spr);
            backMenu->addChild(m_accountButton);
            backMenu->updateLayout();
        });
    }

    void onAccount(CCObject*) {
        showAccountPopup();
    }

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
