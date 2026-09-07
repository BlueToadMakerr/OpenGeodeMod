#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/utils/ColorProvider.hpp>

using namespace geode::prelude;

namespace opengeode {

inline char const* getPopupBackground() {
    std::string theme;
    ThemeIDProvidingEvent().send(theme);
    return theme == "geometry-dash" ? "GJ_square01.png" : "GE_square01.png";
}

inline CCNode* createSectionContainer(CCSize size) {
    auto container = CCNode::create();
    container->setContentSize(size);
    container->setAnchorPoint({.5f, .5f});

    auto bg = NineSlice::create("square02b_001.png");
    bg->setColor({0, 0, 0});
    bg->setOpacity(75);
    bg->setScale(.3f);
    bg->setContentSize(size / bg->getScale());
    container->addChildAtPosition(bg, Anchor::Center);

    return container;
}

inline CCMenu* createSectionTitle(
    char const* title,
    CCNode* resetButton = nullptr,
    float width = 0.f
) {
    auto menu = CCMenu::create();
    menu->setContentSize({width, 22.f});
    menu->setAnchorPoint({.5f, .5f});

    auto label = CCLabelBMFont::create(title, "bigFont.fnt");
    label->setScale(.4f);
    label->setAnchorPoint({0.f, .5f});
    label->setPosition({3.f, 11.f});
    menu->addChild(label);

    if (resetButton) {
        resetButton->setPosition({width - 13.f, 11.f});
        menu->addChild(resetButton, 1);
    }

    return menu;
}

} // namespace opengeode
