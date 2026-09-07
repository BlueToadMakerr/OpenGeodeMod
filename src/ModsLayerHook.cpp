#include "FilterPopup.hpp"
#include "IndexListPopup.hpp"
#include "ModsListUtils.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/SceneEvent.hpp>

using namespace geode::prelude;

namespace opengeode {

Notification* g_switchNotif = nullptr;

namespace {

class ModsLayerWatcher : public CCNode {
protected:
    bool init() {
        if (!CCNode::init())
            return false;

        this->setID("OpenGeode.mods-layer-watcher"_spr);
        this->schedule(schedule_selector(ModsLayerWatcher::check), 0.25f);
        return true;
    }

    void check(float) {
        auto scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene || scene != this->getParent())
            return;

        auto listFrame = scene->getChildByIDRecursive("mod-list-frame");
        if (!listFrame)
            return;

        auto modList = listFrame->getChildByID("ModList");
        if (!modList)
            return;

        // The destination Mods UI is now fully present. Remove both pieces of
        // temporary switching UI before installing our buttons.
        if (g_switchNotif) {
            g_switchNotif->cancel();
            g_switchNotif = nullptr;
        }
        if (auto overlay = scene->getChildByID("switch-overlay"_spr)) {
            overlay->removeFromParentAndCleanup(true);
        }

        auto topContainer = modList->getChildByID("top-container");
        if (!topContainer)
            return;

        auto searchMenu = topContainer->getChildByID("search-menu");
        if (!searchMenu)
            return;

        auto filtersMenu = typeinfo_cast<CCMenu*>(
            searchMenu->getChildByID("search-filters-menu")
        );
        if (!filtersMenu)
            return;

        this->ensureIndexSwitcherButton(scene);
        this->ensureFilterButton(filtersMenu);
    }

    void ensureIndexSwitcherButton(CCNode* scene) {
        auto actionsMenu = typeinfo_cast<CCMenu*>(scene->getChildByIDRecursive("actions-menu"));
        if (!actionsMenu)
            return;

        if (actionsMenu->getChildByID("index-switcher-button"_spr))
            return;

        auto indexBtn = CCMenuItemExt::createSpriteExtra(
            CircleButtonSprite::createWithSpriteFrameName(
                "geode.loader/geode-logo.png",
                0.85f,
                CircleBaseColor::Blue
            ),
            [](auto) {
                showIndexListPopup();
            }
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
            buildFilterButtonSprite(),
            [](auto) {
                showFilterPopup();
            }
        );

        filterBtn->setID("index-filter-button"_spr);
        filtersMenu->addChild(filterBtn, -100);
        filtersMenu->updateLayout();
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
    SceneEvent().listen([](CCScene* scene) {
        if (!scene)
            return ListenerResult::Propagate;

        // ModsLayer is an internal Geode class and has no public
        // Geode/modify/ModsLayer.hpp binding. Instead of hooking it,
        // attach a small watcher to each incoming scene and modify the
        // UI strictly through its documented node IDs.
        if (scene->getChildByID("OpenGeode.mods-layer-watcher"_spr))
            return ListenerResult::Propagate;

        auto watcher = ModsLayerWatcher::create();
        if (watcher)
            scene->addChild(watcher);

        return ListenerResult::Propagate;
    }).leak();
}

} // namespace opengeode
