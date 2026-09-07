#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>

#include "Settings.hpp"

using namespace geode::prelude;

namespace opengeode {

inline bool useDarkTheme() {
    auto loader = Loader::get()->getLoadedMod("geode.loader");
    if (!loader) return true;
    return loader->getSettingValue<std::string>("used-theme") != "Geometry Dash";
}

inline CCNode* buildFilterButtonSprite() {
    CCSprite* bgSprite = nullptr;

    if (isFilterActiveForCurrentTab()) {
        bgSprite = CCSprite::create("GJ_button_02.png");
    }
    else if (useDarkTheme()) {
        bgSprite = CCSprite::create("GE_button_05.png"_spr);
    }
    else {
        bgSprite = CCSprite::create("GJ_button_01.png");
        if (!bgSprite) {
            bgSprite = CCSprite::createWithSpriteFrameName("GJ_button_01.png");
        }
    }

    if (!bgSprite) bgSprite = CCSprite::create();

    auto icon = CCSprite::createWithSpriteFrameName("geode.loader/geode-logo-outline-gold.png");
    if (icon) {
        icon->setPosition({bgSprite->getContentSize().width / 2, bgSprite->getContentSize().height / 2});
        icon->setScale(0.8f);
        bgSprite->addChild(icon);
    }

    return bgSprite;
}

inline void triggerModsListReload() {
    if (auto scene = CCDirector::sharedDirector()->getRunningScene()) {
        if (auto reloadBtn = typeinfo_cast<CCMenuItemSpriteExtra*>(scene->getChildByIDRecursive("reload-button"))) {
            reloadBtn->activate();
        }
    }
}

} // namespace opengeode
