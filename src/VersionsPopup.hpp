#pragma once

#include <Geode/Geode.hpp>

namespace opengeode {

void showVersionsPopup(std::string const& modID, cocos2d::CCNode* modPopup);
void ensureVersionsButton(cocos2d::CCNode* modPopup);

} // namespace opengeode
