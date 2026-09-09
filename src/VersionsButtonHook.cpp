#include "MoreManagePopup.hpp"
#include "PopupSectionUtils.hpp"
#include "VersionsPopup.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/IconButtonSprite.hpp>
#include <Geode/ui/SceneEvent.hpp>

using namespace geode::prelude;

namespace opengeode {
namespace {

std::string getPopupModID(CCNode* popup) {
    auto label = typeinfo_cast<CCLabelBMFont*>(popup->getChildByIDRecursive("mod-id-label"));
    if (!label) return "";
    std::string value = label->getString();
    constexpr std::string_view prefix = "(ID: ";
    if (!value.starts_with(prefix) || value.size() <= prefix.size()) return "";
    value.erase(0, prefix.size());
    if (!value.empty() && value.back() == ')') value.pop_back();
    return value;
}

CCMenu* getManagementMenu(CCNode* popup) {
    for (auto const& id : {
        "update-button", "enable-button", "reenable-button", "unavailable-button",
        "install-button", "uninstall-button", "cancel-button"
    }) {
        if (auto node = popup->getChildByIDRecursive(id)) {
            if (auto action = typeinfo_cast<CCMenuItem*>(node)) {
                if (auto menu = typeinfo_cast<CCMenu*>(action->getParent())) return menu;
            }
        }
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

bool ensureVersionsButton(CCNode* popup) {
    auto menu = getManagementMenu(popup);
    if (!menu) return false;
    if (menu->getChildByID("opengeode-versions-button"_spr)) return false;

    auto modID = getPopupModID(popup);
    if (modID.empty()) return false;
    auto sprite = createVersionsButtonSprite();
    if (!sprite) return false;

    auto button = CCMenuItemExt::createSpriteExtra(sprite, [modID](CCMenuItemSpriteExtra*) {
        showVersionsPopup(modID);
    });
    button->setID("opengeode-versions-button"_spr);
    menu->addChild(button);
    menu->updateLayout();
    return true;
}

class VersionsButtonWatcher : public CCNode {
protected:
    bool init() {
        if (!CCNode::init()) return false;
        setID("OpenGeode.versions-button-watcher"_spr);
        schedule(schedule_selector(VersionsButtonWatcher::check), .25f);
        return true;
    }

    void check(float) {
        auto scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene || scene != getParent()) return;
        auto listFrame = scene->getChildByIDRecursive("mod-list-frame");
        if (!listFrame) return;
        auto modList = listFrame->getChildByID("ModList");
        if (!modList || !modList->getChildByID("top-container")) return;

        if (ensureVersionsButton(scene)) {
            ensureModPopupExtras(scene);
        }
    }

public:
    static VersionsButtonWatcher* create() {
        auto ret = new VersionsButtonWatcher();
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
        if (!scene) return ListenerResult::Propagate;
        if (scene->getChildByID("OpenGeode.versions-button-watcher"_spr)) return ListenerResult::Propagate;
        if (auto watcher = VersionsButtonWatcher::create()) scene->addChild(watcher);
        return ListenerResult::Propagate;
    }).leak();
}

} // namespace opengeode
