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
#include <Geode/ui/IconButtonSprite.hpp>
#include <algorithm>
#include <string>
#include <vector>
using namespace geode::prelude;
namespace opengeode {
    Notification * g_switchNotif = nullptr;
    namespace {
        CCNode * createProfileButtonSprite() {
            auto profile = CCSprite::createWithSpriteFrameName("GJ_profileButton_001.png");
            if (!profile)
                return nullptr;
            constexpr float targetSize = 40.f;
            auto width = profile->getContentSize().width;
            auto height = profile->getContentSize().height;
            if (width <= 0.f || height <= 0.f)
                return nullptr;
            auto root = CCNode::create();
            if (!root)
                return nullptr;
            profile->setPosition( {
                targetSize / 2.f, targetSize / 2.f
            }
            );
            profile->setScale(targetSize / std::max(width, height));
            root->setContentSize( {
                targetSize, targetSize
            }
            );
            root->setAnchorPoint( {
                .5f,.5f
            }
            );
            root->addChild(profile);
            return root;
        }
        std::string getTextureCacheKey(CCTexture2D * texture) {
            if (!texture)
                return "";
            auto cache = CCTextureCache::sharedTextureCache();
            auto textures = cache ? cache->snapshotTextures(): nullptr;
            if (!textures)
                return "";
            for (auto key: CCArrayExt < CCString * >(textures->allKeys())) if (key && textures->objectForKey(key->getCString()) == texture) return key->getCString();
            return "";
        }
        std::string getModIDFromModItem(CCNode * modItem) {
            auto logo = modItem->getChildByIDRecursive("logo-sprite");
            auto lazySprite = typeinfo_cast < LazySprite * >(logo);
            if (!lazySprite || !lazySprite->getTexture()) return "";
            auto key = getTextureCacheKey(lazySprite->getTexture());
            constexpr std::string_view prefix = "/files/geode/unzipped/";
            auto start = key.find(prefix);
            if (start == std::string::npos)
                return "";
            start += prefix.size();
            auto end = key.find("/logo.png", start);
            if (end == std::string::npos || end <= start)
                return "";
            return key.substr(start, end - start);
        }
        void collectModItems(CCNode * node, std::vector < CCNode * > & out) {
            if (!node)
                return;
            auto viewMenu = node->getChildByID("view-menu");
            auto logo = node->getChildByIDRecursive("logo-sprite");
            if (viewMenu && logo) {
                out.push_back(node);
                return;
            }
            for (auto child: CCArrayExt < CCNode * >(node->getChildren())) collectModItems(child, out);
        }
        void showAlreadyUpdatedPopup(std::string const & modID, std::string const & currentIndexName) {
            auto source = getInstalledModSource(modID);
            if (!source)
                return;
            auto installedName = source->indexName.empty() ? source->indexId: source->indexName;
            auto through = wasModUpdatedFromGeode(modID) ? "Geode": "Open Geode";
            auto mod = Loader::get()->getInstalledMod(modID);
            auto modName = mod ? std::string(mod->getName()): modID;
            auto message = fmt::format("You already <cg>updated</c> <cy>{}</c> through <cj>{}</c> from <cy>{}</c>.\nn\nnYou are trying to install from <cy>{}</cy>. To change the updated through this index (or any other index), click the <cj>Open Geode</c> button and <cy>redownload</c> the update.",
            modName, through, installedName, currentIndexName);
            FLAlertLayer::create("Already Updated!", message.c_str(), "OK")->show();
        }
        IconButtonSprite * createUpdatedModListButton(std::string const & modID, std::string const & indexName) {
            auto icon = CCSprite::createWithSpriteFrameName("GJ_completesIcon_001.png");
            if (!icon)
                return nullptr;
            auto button = IconButtonSprite::create(getButtonTexture("GJ_button_01.png"), icon, "Updated", "bigFont.fnt");
            if (!button)
                return nullptr;
            button->setScale(.5f);
            return button;
        }
        void hideUpdatedModListButtons(CCNode * listFrame) {
            auto contentLayer = listFrame->getChildByIDRecursive("content-layer");
            if (!contentLayer)
                return;
            std::vector < CCNode * > modItems;
            collectModItems(contentLayer, modItems);
            for (auto modItem: modItems) {
                auto modID = getModIDFromModItem(modItem);
                if (modID.empty() || !wasModUpdatedThroughAnything(modID)) continue;
                auto viewMenu = modItem->getChildByID("view-menu");
                if (!viewMenu)
                    continue;
                if (auto updateButton = viewMenu->getChildByID("update-button" _spr)) updateButton->removeFromParentAndCleanup(true);
                if (!viewMenu->getChildByID("opengeode-updated-button" _spr)) {
                    auto indexName = getIndexUrl();
                    for (auto const & e: getAllIndexes()) if (e.url == getIndexUrl()) {
                        indexName = e.name;
                        break;
                    }
                    if (auto sprite = createUpdatedModListButton(modID, indexName)) {
                        auto updated = CCMenuItemExt::createSpriteExtra(sprite, [modID, indexName](CCMenuItemSpriteExtra *) {
                            showAlreadyUpdatedPopup(modID, indexName);
                        }
                        );
                        updated->setID("opengeode-updated-button" _spr);
                        viewMenu->addChild(updated, 1000);
                        if (auto menu = typeinfo_cast < CCMenu * >(viewMenu)) menu->reorderChild(updated, 1000);
                    }
                }
                if (auto menu = typeinfo_cast < CCMenu * >(viewMenu)) menu->updateLayout();
            }
        }
        CCMenu * getModsLayerActionsMenu(CCNode * scene, CCNode * modList) {
            if (auto menu = typeinfo_cast < CCMenu * >(scene->getChildByID("actions-menu" _spr))) return menu;
            if (modList)
                if (auto menu = typeinfo_cast < CCMenu * >(modList->getChildByID("actions-menu" _spr))) return menu;
            return nullptr;
        }
        class ModsLayerWatcher: public CCNode {
            CCMenuItemSpriteExtra * m_accountButton = nullptr;
            CCNode * m_updateBadge = nullptr;
            bool m_inModsLayer = false;
            protected: bool init() {
                if (!CCNode::init()) return false;
                setID("OpenGeode.mods-layer-watcher" _spr);
                schedule(schedule_selector(ModsLayerWatcher::check),.25f);
                return true;
            }
            void check(float) {
                auto scene = CCDirector::sharedDirector()->getRunningScene();
                if (!scene || scene != getParent()) return;
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
                if (g_switchNotif) {
                    g_switchNotif->cancel();
                    g_switchNotif = nullptr;
                }
                if (auto overlay = scene->getChildByID("switch-overlay" _spr)) overlay->removeFromParentAndCleanup(true);
                hideUpdatedModListButtons(listFrame);
                auto modList = listFrame->getChildByID("ModList");
                if (!modList)
                    return;
                auto topContainer = modList->getChildByID("top-container");
                if (!topContainer)
                    return;
                auto searchMenu = topContainer->getChildByID("search-menu");
                if (!searchMenu)
                    return;
                auto filtersMenu = typeinfo_cast < CCMenu * >(searchMenu->getChildByID("search-filters-menu"));
                if (!filtersMenu)
                    return;
                ensureIndexSwitcherButton(scene);
                if (!indexUpdatesLoading() && !hasFreshIndexUpdateCache()) fetchIndexUpdates([this] {
                    auto currentScene = CCDirector::sharedDirector()->getRunningScene(); if (currentScene) ensureIndexSwitcherButton(currentScene);
                }
                );
                ensureFilterButton(filtersMenu);
                ensureAccountButton(scene);
                ensureVersionsButton(scene);
                ensureModPopupExtras(scene);
            }
            void ensureIndexSwitcherButton(CCNode * scene) {
                auto actionsMenu = typeinfo_cast < CCMenu * >(scene->getChildByIDRecursive("actions-menu"));
                if (!actionsMenu)
                    return;
                auto indexBtn = typeinfo_cast < CCMenuItemSpriteExtra * >(actionsMenu->getChildByID("index-switcher-button" _spr));
                if (!indexBtn) {
                    indexBtn = CCMenuItemExt::createSpriteExtra(CircleButtonSprite::createWithSpriteFrameName("geode.loader/geode-logo-outline-gold.png",
                    .85f, CircleBaseColor::Blue), [](auto) {
                        showIndexListPopup();
                    }
                    );
                    indexBtn->setScale(.8f);
                    indexBtn->m_baseScale =.8f;
                    indexBtn->setID("index-switcher-button" _spr);
                    actionsMenu->addChild(indexBtn);
                }
                auto count = getTotalUpdateCount();
                if (m_updateBadge) {
                    m_updateBadge->removeFromParentAndCleanup(true);
                    m_updateBadge = nullptr;
                }
                if (count > 0) {
                    auto badge = CCNode::create();
                    badge->setContentSize( {
                        40.5f, 40.5f
                    }
                    );
                    badge->setAnchorPoint( {
                        .5f,.5f
                    }
                    );
                    auto icon = CCSprite::createWithSpriteFrameName("geode.loader/updates-available.png");
                    if (icon) {
                        icon->setScale(.825f);
                        icon->setPosition( {
                            20.25f, 20.25f
                        }
                        );
                        badge->addChild(icon);
                    }
                    auto label = CCLabelBMFont::create(std::to_string(count).c_str(), "bigFont.fnt");
                    label->setScale(.42f);
                    label->setAnchorPoint( {
                        .5f,.5f
                    }
                    );
                    label->setPosition( {
                        20.25f, 20.25f
                    }
                    );
                    badge->addChild(label, 2);
                    badge->setPosition( {
                        indexBtn->getContentWidth() - 7.f, indexBtn->getContentHeight() - 7.f
                    }
                    );
                    indexBtn->addChild(badge, 10);
                    m_updateBadge = badge;
                }
                actionsMenu->updateLayout();
            }
            void ensureFilterButton(CCMenu * filtersMenu) {
                if (auto existingBtn = filtersMenu->getChildByID("index-filter-button" _spr)) existingBtn->removeFromParent();
                auto filterBtn = CCMenuItemExt::createSpriteExtra(buildFilterButtonSprite(), [](auto) {
                    showFilterPopup();
                }
                );
                filterBtn->setID("index-filter-button" _spr);
                filtersMenu->addChild(filterBtn, - 100);
                filtersMenu->updateLayout();
            }
            void ensureAccountButton(CCNode * scene) {
                auto backMenu = typeinfo_cast < CCMenu * >(scene->getChildByIDRecursive("back-menu"));
                if (!backMenu || m_accountButton)
                    return;
                if (getIndexUrl().empty()) return;
                auto sprite = createProfileButtonSprite();
                if (!sprite)
                    return;
                m_accountButton = CCMenuItemSpriteExtra::create(sprite, this, menu_selector(ModsLayerWatcher::onAccount));
                m_accountButton->setScale(.8f);
                m_accountButton->m_baseScale =.8f;
                m_accountButton->setID("opengeode-account-button" _spr);
                backMenu->addChild(m_accountButton);
                backMenu->updateLayout();
            }
            void onAccount(CCObject *) {
                showAccountPopup();
            }
            public: static ModsLayerWatcher * create() {
                auto ret = new ModsLayerWatcher();
                if (ret && ret->init()) {
                    ret->autorelease();
                    return ret;
                }
                delete ret;
                return nullptr;
            }
        };
    }
    $ on_mod(Loaded) {
        ensurePresetsExist();
        SceneEvent().listen([](CCScene * scene) {
            if (!scene) return ListenerResult::Propagate; if (scene->getChildByID("OpenGeode.mods-layer-watcher" _spr)) return ListenerResult::Propagate; auto watcher = ModsLayerWatcher::create(); if (watcher) scene->addChild(watcher); return ListenerResult::Propagate;
        }
        ).leak();
    }
}
// namespace opengeode
