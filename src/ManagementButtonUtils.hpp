#pragma once

#include <Geode/Geode.hpp>

namespace opengeode {

// Adds a feature-owned button to the native mod management menu.
// Returns false if the menu/button cannot be found or the button already exists.
bool addManagementButton(
    cocos2d::CCNode* popup,
    char const* id,
    cocos2d::CCNode* sprite,
    std::function<void(CCMenuItemSpriteExtra*)> callback
);

}
