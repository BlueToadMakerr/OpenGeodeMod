#include "ManagementButtonUtils.hpp"

using namespace geode::prelude;

namespace opengeode {
namespace {

CCMenuItem* getVisibleNativeAction(CCNode* popup, char const* id) {
    auto node = popup->getChildByIDRecursive(id);
    if (!node || !node->isVisible()) return nullptr;
    return typeinfo_cast<CCMenuItem*>(node);
}

CCMenu* getNativeManagementMenu(CCNode* popup) {
    if (!popup) return nullptr;

    for (auto const& id : {
        "update-button", "enable-button", "reenable-button", "unavailable-button",
        "install-button", "uninstall-button", "cancel-button"
    }) {
        auto action = getVisibleNativeAction(popup, id);
        if (!action) continue;
        if (auto menu = typeinfo_cast<CCMenu*>(action->getParent())) return menu;
    }
    return nullptr;
}

}

bool addManagementButton(
    CCNode* popup,
    char const* id,
    CCNode* sprite,
    std::function<void(CCMenuItemSpriteExtra*)> callback
) {
    if (!popup || !id || !sprite) return false;

    auto managementMenu = getNativeManagementMenu(popup);
    if (!managementMenu) return false;

    // Only check the native management menu. This makes repeated watcher passes
    // idempotent and avoids accidentally matching a button in another popup.
    if (managementMenu->getChildByID(id)) return false;

    auto button = CCMenuItemExt::createSpriteExtra(
        sprite,
        std::move(callback)
    );
    if (!button) return false;

    button->setID(id);
    managementMenu->addChild(button);
    managementMenu->updateLayout();
    return true;
}

}
