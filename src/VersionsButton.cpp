#include "VersionsPopup.hpp"
#include "InstalledMods.hpp"
#include "PopupSectionUtils.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/IconButtonSprite.hpp>

using namespace geode::prelude;

namespace opengeode {
namespace {

std::string getPopupModID(CCNode* popup) {
    auto label = typeinfo_cast<CCLabelBMFont*>(popup->getChildByIDRecursive("mod-id-label"));
    if (!label) return "";
    std::string value = label->getString();
    constexpr char const* prefix = "(ID: ";
    if (!value.starts_with(prefix) || value.size() <= 6) return "";
    value.erase(0, 6);
    if (!value.empty() && value.back() == ')') value.pop_back();
    return value;
}

CCMenuItem* getVisibleManagementButton(CCNode* popup, char const* id) {
    auto node = popup->getChildByIDRecursive(id);
    if (!node || !node->isVisible()) return nullptr;
    return typeinfo_cast<CCMenuItem*>(node);
}

CCMenu* getNativeManagementMenu(CCNode* popup) {
    for (auto const& id : {
        "update-button", "enable-button", "reenable-button", "unavailable-button",
        "install-button", "uninstall-button", "cancel-button"
    }) {
        auto action = getVisibleManagementButton(popup, id);
        if (!action) continue;
        if (auto menu = typeinfo_cast<CCMenu*>(action->getParent())) return menu;
    }
    return nullptr;
}

IconButtonSprite* createVersionsButtonSprite() {
    auto icon = CCSprite::createWithSpriteFrameName("GJ_timeIcon_001.png");
    if (!icon) return nullptr;
    auto button = IconButtonSprite::create(
        getButtonTexture("GJ_button_01.png"), icon, "Versions", "bigFont.fnt"
    );
    if (button) button->setScale(.5f);
    return button;
}

} // namespace

void ensureVersionsButton(CCNode* popup) {
    if (!popup || popup->getChildByIDRecursive("opengeode-versions-button")) return;

    auto managementMenu = getNativeManagementMenu(popup);
    if (!managementMenu) return;

    auto modID = getPopupModID(popup);
    if (modID.empty()) return;

    auto sprite = createVersionsButtonSprite();
    if (!sprite) return;

    auto button = CCMenuItemExt::createSpriteExtra(sprite, [modID, popup](CCMenuItemSpriteExtra*) {
        showVersionsPopup(modID, popup);
    });
    button->setID("opengeode-versions-button"_spr);
    managementMenu->addChild(button);
    managementMenu->updateLayout();
}

} // namespace opengeode
