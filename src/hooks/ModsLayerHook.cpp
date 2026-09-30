#include "../FilterPopup.hpp"
#include "../IndexListPopup.hpp"
#include "../ModsListUtils.hpp"
#include "../AccountPopup.hpp"
#include "../MoreManagePopup.hpp"
#include "../VersionsPopup.hpp"
#include "../Settings.hpp"
#include "../InstalledMods.hpp"
#include "../IndexUpdates.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/SceneEvent.hpp>

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

std::string getTextureCacheKey(CCTexture2D* texture) {
    if (!texture) return "";
    auto cache = CCTextureCache::sharedTextureCache();
    auto textures = cache ? cache->snapshotTextures() : nullptr;
    if (!textures) return "";
    for (auto key : CCArrayExt<CCString*>(textures->allKeys())) {
        if (key && textures->objectForKey(key->getCString()) == texture)
            return key->getCString();
    }
    return "";
}

std::string getModIDFromModItem(CCNode* modItem) {
    auto logo = modItem->getChildByIDRecursive("logo-sprite");
    auto lazySprite = typeinfo_cast<LazySprite*>(logo);
    if (!lazySprite || !lazySprite->getTexture()) return "";

    auto key = getTextureCacheKey(lazySprite->getTexture());
    constexpr std::string_view prefix = "/files/geode/unzipped/";
    auto start = key.find(prefix);
    if (start == std::string::npos) return "";
    start += prefix.size();

    auto end = key.find("/logo.png", start);
    if (end == std::string::npos || end <= start) return "";
    return key.substr(start, end - start);
}

void hideUpdatedModListButtons(CCNode* listFrame) {
    auto contentLayer = listFrame->getChildByIDRecursive("content-layer");
    if (!contentLayer) return;

    for (auto child : CCArrayExt<CCNode*>(contentLayer->getChildren())) {
        if (!child) continue;

        auto modID = getModIDFromModItem(child);
        if (modID.empty() || !wasModUpdatedFromIndex(modID)) continue;

        auto viewMenu = child->getChildByIDRecursive("view-menu");
        if (!viewMenu) continue;

        auto updateButton = viewMenu->getChildByID("update-button"_spr);
        if (!updateButton) continue;

        updateButton->removeFromParentAndCleanup(true);
        if (auto menu = typeinfo_cast<CCMenu*>(viewMenu)) menu->updateLayout();
    }
}

class ModsLayerWatcher : public CCNode {
    CCMenuItemSpriteExtra* m_accountButton = nullptr;
    CCNode* m_updateBadge = nullptr;
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
                if (m_accountButton) {
                    m_accountButton->removeFromParent();
                    m_accountButton = nullptr;
                }
            }
            return;
        }

        m_inModsLayer = true;
        if (g_switchNotif) { g_switchNotif->cancel(); g_switchNotif = nullptr; }
        if (auto overlay = scene->getChildByID("switch-overlay"_spr)) overlay->removeFromParentAndCleanup(true);

        hideUpdatedModListButtons(listFrame);

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
                auto currentScene = CCDirector::sharedDirector()->getRunningScene();
                if (currentScene) ensureIndexSwitcherButton(currentScene);
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
                CircleButtonSprite::createWithSpriteFrameName("geode.loader/geode-logo-outline-gold.png", 0.85f, CircleBaseColor::Blue),
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
        if (!backMenu || m_accountButton) return;
        if (getIndexUrl().empty()) return;

        auto sprite = createProfileButtonSprite();
        if (!sprite) return;
        m_accountButton = CCMenuItemSpriteExtra::create(sprite, this, menu_selector(ModsLayerWatcher::onAccount));
        m_accountButton->setScale(.8f);
        m_accountButton->m_baseScale = .8f;
        m_accountButton->setID("opengeode-account-button"_spr);
        backMenu->addChild(m_accountButton);
        backMenu->updateLayout();
    }

    void onAccount(CCObject*) { showAccountPopup(); }

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
