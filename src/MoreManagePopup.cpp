#include "MoreManagePopup.hpp"
#include "InstalledMods.hpp"
#include "PopupSectionUtils.hpp"
#include "VersionsPopup.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/IconButtonSprite.hpp>
#include <Geode/ui/Popup.hpp>

using namespace geode::prelude;

namespace opengeode {

namespace {

constexpr char const* MORE_HIDDEN_MARKER_ID = "opengeode-more-hidden-marker";

std::string getPopupModID(CCNode* popup) {
    auto label = typeinfo_cast<CCLabelBMFont*>(popup->getChildByIDRecursive("mod-id-label"));
    if (!label) return "";
    std::string value = label->getString();
    auto prefix = std::string("(ID: ");
    if (!value.starts_with(prefix) || value.size() <= prefix.size()) return "";
    value.erase(0, prefix.size());
    if (!value.empty() && value.back() == ')') value.pop_back();
    return value;
}

struct NativeAction {
    CCMenuItem* action = nullptr;
    IconButtonSprite* visual = nullptr;
};

void debugTextureKey(char const* label, CCTexture2D* texture) {
    if (!texture) {
        log::info("[OpenGeode][MoreDebug] {}: no texture", label);
        return;
    }
    auto cache = CCTextureCache::sharedTextureCache();
    auto textures = cache ? cache->snapshotTextures() : nullptr;
    if (!textures) return;
    for (auto key : CCArrayExt<CCString*>(textures->allKeys())) {
        if (key && textures->objectForKey(key->getCString()) == texture) {
            log::info("[OpenGeode][MoreDebug] {}: texture={} cacheKey=\"{}\"", label, texture->getName(), key->getCString());
        }
    }
}

std::string getTextureCacheKey(CCTexture2D* texture) {
    if (!texture) return "";
    auto cache = CCTextureCache::sharedTextureCache();
    auto textures = cache ? cache->snapshotTextures() : nullptr;
    if (!textures) return "";
    for (auto key : CCArrayExt<CCString*>(textures->allKeys())) {
        if (key && textures->objectForKey(key->getCString()) == texture) return key->getCString();
    }
    return "";
}

void debugSprite(char const* label, CCSprite* sprite) {
    if (!sprite) return;
    auto rect = sprite->getTextureRect();
    log::info(
        "[OpenGeode][MoreDebug] {}: texture={} rect=({}, {}, {}, {}) rotated={} flipX={} flipY={}",
        label,
        sprite->getTexture() ? sprite->getTexture()->getName() : 0,
        rect.origin.x, rect.origin.y, rect.size.width, rect.size.height,
        sprite->isTextureRectRotated(), sprite->isFlipX(), sprite->isFlipY()
    );
    debugTextureKey(label, sprite->getTexture());
}

IconButtonSprite* getVisibleIconButton(CCMenuItem* item);

void debugNativeButton(char const* id, IconButtonSprite* button) {
    if (!button) return;
    log::info(
        "[OpenGeode][MoreDebug] {}: string=\"{}\" scale={} size=({}, {}) bg={} icon={}",
        id, button->getString(), button->getScale(),
        button->getContentSize().width, button->getContentSize().height,
        fmt::ptr(button->getBg()), fmt::ptr(button->getIcon())
    );
    debugSprite("  icon", typeinfo_cast<CCSprite*>(button->getIcon()));
    auto bg = button->getBg();
    if (!bg) return;
    debugSprite("  bg topLeft", bg->getTopLeft());
    debugSprite("  bg topRight", bg->getTopRight());
    debugSprite("  bg bottomLeft", bg->getBottomLeft());
    debugSprite("  bg bottomRight", bg->getBottomRight());
    debugSprite("  bg top", bg->getTop());
    debugSprite("  bg bottom", bg->getBottom());
    debugSprite("  bg left", bg->getLeft());
    debugSprite("  bg right", bg->getRight());
    debugSprite("  bg center", bg->getCenter());
}

void debugManagementChild(CCNode* child) {
    if (!child) return;
    auto id = child->getID();
    auto action = typeinfo_cast<CCMenuItem*>(child);
    log::info(
        "[OpenGeode][MoreDebug] management child id=\"{}\" visible={} enabled={} menuItem={} toggler={}",
        id, child->isVisible(), action ? action->isEnabled() : false,
        action != nullptr, typeinfo_cast<CCMenuItemToggler*>(action) != nullptr
    );
    if (!action) return;
    auto visual = getVisibleIconButton(action);
    if (visual) debugNativeButton(id.c_str(), visual);
}

IconButtonSprite* getVisibleIconButton(CCMenuItem* item) {
    if (auto toggler = typeinfo_cast<CCMenuItemToggler*>(item)) {
        auto wrapper = toggler->m_onButton && toggler->m_onButton->isVisible()
            ? toggler->m_onButton : toggler->m_offButton;
        if (!wrapper) return nullptr;
        for (auto child : CCArrayExt<CCNode*>(wrapper->getChildren())) {
            if (auto button = typeinfo_cast<IconButtonSprite*>(child)) return button;
        }
        return nullptr;
    }
    if (auto spriteItem = typeinfo_cast<CCMenuItemSpriteExtra*>(item)) {
        return typeinfo_cast<IconButtonSprite*>(spriteItem->getNormalImage());
    }
    return nullptr;
}

NativeAction getVisibleNativeAction(CCNode* popup, char const* id) {
    auto node = popup->getChildByIDRecursive(id);
    if (!node || !node->isVisible()) return {};
    auto action = typeinfo_cast<CCMenuItem*>(node);
    if (!action) return {};
    return {action, getVisibleIconButton(action)};
}

bool isPopupInstalled(CCNode* popup) {
    return getVisibleNativeAction(popup, "uninstall-button").action ||
        getVisibleNativeAction(popup, "update-button").action ||
        getVisibleNativeAction(popup, "enable-button").action ||
        getVisibleNativeAction(popup, "reenable-button").action;
}

CCMenu* getNativeManagementMenu(CCNode* popup) {
    for (auto const& id : {
        "update-button", "enable-button", "reenable-button", "unavailable-button",
        "install-button", "uninstall-button", "cancel-button"
    }) {
        auto native = getVisibleNativeAction(popup, id);
        if (!native.action) continue;
        auto menu = typeinfo_cast<CCMenu*>(native.action->getParent());
        if (menu) return menu;
    }
    return nullptr;
}

bool isMoreHidden(CCMenuItem* action) {
    return action && action->getChildByID(MORE_HIDDEN_MARKER_ID);
}

void setMoreHidden(CCMenuItem* action, bool hidden) {
    if (!action) return;
    auto marker = action->getChildByID(MORE_HIDDEN_MARKER_ID);
    if (hidden) {
        if (!marker) {
            marker = CCNode::create();
            marker->setID(MORE_HIDDEN_MARKER_ID);
            marker->setVisible(false);
            action->addChild(marker);
        }
        action->setVisible(false);
    }
    else if (marker) {
        marker->removeFromParentAndCleanup(true);
    }
}

void resetMoreHiddenButtons(CCMenu* managementMenu) {
    for (auto child : CCArrayExt<CCNode*>(managementMenu->getChildren())) {
        auto action = typeinfo_cast<CCMenuItem*>(child);
        if (!action || !isMoreHidden(action)) continue;
        setMoreHidden(action, false);
        action->setVisible(true);
    }
}

bool applyManagementButtonLimit(CCMenu* managementMenu) {
    resetMoreHiddenButtons(managementMenu);
    auto maxButtons = Mod::get()->getSettingValue<int>("max-management-buttons");
    std::vector<CCMenuItem*> visibleBeforeMore;
    for (auto child : CCArrayExt<CCNode*>(managementMenu->getChildren())) {
        if (!child || child->getID() == "opengeode-more-button") continue;
        auto action = typeinfo_cast<CCMenuItem*>(child);
        if (!action || !action->isVisible()) continue;
        visibleBeforeMore.push_back(action);
    }
    auto keepCount = maxButtons > 1 ? static_cast<size_t>(maxButtons - 1) : 0u;
    if (visibleBeforeMore.size() <= keepCount) return false;
    auto hiddenCount = visibleBeforeMore.size() - keepCount;
    for (size_t i = 0; i < hiddenCount; ++i) {
        setMoreHidden(visibleBeforeMore[visibleBeforeMore.size() - 1 - i], true);
    }
    return true;
}

void showInstallSource(std::string const& modID) {
    auto source = getInstalledModSource(modID);
    if (!source) return;
    auto description = fmt::format(
        "Installed from <cy>{}</c>\nVersion: <cg>{}</c>\n{}",
        source->indexName.empty() ? source->indexUrl : source->indexName,
        source->version, source->indexUrl
    );
    FLAlertLayer::create("Install Source", description, "OK")->show();
}

IconButtonSprite* createThemedManageButton(char const* text, char const* iconFrame) {
    auto icon = CCSprite::createWithSpriteFrameName(iconFrame);
    if (!icon) return nullptr;
    auto button = IconButtonSprite::create(getButtonTexture("GJ_button_01.png"), icon, text, "bigFont.fnt");
    if (button) button->setScale(.5f);
    return button;
}

std::string getNativeButtonTexture(IconButtonSprite* source) {
    if (!source || !source->getBg()) return "";
    return getTextureCacheKey(source->getBg()->getTopLeft()->getTexture());
}

CCNode* duplicateIcon(CCNode* icon) {
    auto sprite = typeinfo_cast<CCSprite*>(icon);
    if (!sprite || !sprite->getTexture()) return nullptr;
    auto rect = sprite->getTextureRect();
    auto rotated = sprite->isTextureRectRotated();
    auto untrimmedSize = rotated ? CCSize{rect.size.height, rect.size.width} : rect.size;
    auto duplicate = CCSprite::createWithTexture(sprite->getTexture(), rect);
    if (!duplicate) return nullptr;
    duplicate->setTextureRect(rect, rotated, untrimmedSize);
    duplicate->setFlipX(sprite->isFlipX());
    duplicate->setFlipY(sprite->isFlipY());
    return duplicate;
}

IconButtonSprite* recreateNativeButton(char const* id, IconButtonSprite* source) {
    if (!source) return nullptr;
    debugNativeButton(id, source);
    auto icon = duplicateIcon(source->getIcon());
    if (!icon) return nullptr;
    auto texture = getNativeButtonTexture(source);
    if (texture.empty()) return nullptr;
    auto button = IconButtonSprite::create(texture.c_str(), icon, source->getString(), "bigFont.fnt");
    if (!button) return nullptr;
    button->setScale(source->getScale());
    return button;
}

class MoreManagePopup : public Popup {
    CCNode* m_modPopup = nullptr;
protected:
    bool init(CCNode* modPopup) {
        if (!Popup::init(190.f, 255.f, getPopupBackground())) return false;
        m_modPopup = modPopup;
        setTitle("More");
        if (auto close = createGeodeCloseButton()) setCloseButtonSpr(close, .8f);
        auto menu = CCMenu::create();
        menu->setContentSize({150.f, 190.f});
        menu->setLayout(ColumnLayout::create()->setGap(6.f)->setAxisAlignment(AxisAlignment::Center));
        m_mainLayer->addChildAtPosition(menu, Anchor::Center);
        auto managementMenu = getNativeManagementMenu(modPopup);
        if (!managementMenu) return true;
        for (auto child : CCArrayExt<CCNode*>(managementMenu->getChildren())) {
            debugManagementChild(child);
            if (!child || child->getID() == "opengeode-more-button") continue;
            auto action = typeinfo_cast<CCMenuItem*>(child);
            if (!action || (!action->isVisible() && !isMoreHidden(action))) continue;
            auto source = getVisibleIconButton(action);
            if (!source || !source->isVisible()) continue;
            auto button = recreateNativeButton(child->getID().c_str(), source);
            if (!button) continue;
            auto item = CCMenuItemExt::createSpriteExtra(button, [action, this](CCMenuItemSpriteExtra*) {
                action->activate();
                this->onClose(nullptr);
            });
            menu->addChild(item);
        }
        menu->updateLayout();
        return true;
    }
public:
    static MoreManagePopup* create(CCNode* modPopup) {
        auto ret = new MoreManagePopup();
        if (ret && ret->init(modPopup)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

} // namespace

void ensureModPopupExtras(CCNode* popup) {
    auto manageTitle = popup->getChildByIDRecursive("manage-title");
    if (!manageTitle) return;
    auto managementMenu = getNativeManagementMenu(popup);
    if (!managementMenu) return;
    auto modID = getPopupModID(popup);

    if (!managementMenu->getChildByID("opengeode-versions-button"_spr) && !modID.empty()) {
        if (auto sprite = createThemedManageButton("Versions", "GJ_timeIcon_001.png")) {
            auto versions = CCMenuItemExt::createSpriteExtra(sprite, [modID, popup](CCMenuItemSpriteExtra*) {
                showVersionsPopup(modID, popup);
            });
            versions->setID("opengeode-versions-button"_spr);
            managementMenu->addChild(versions);
        }
    }

    if (!managementMenu->getChildByID("opengeode-from-button"_spr) &&
        !modID.empty() && getInstalledModSource(modID) && isPopupInstalled(popup)) {
        if (auto sprite = createThemedManageButton("From", "GJ_downloadsIcon_001.png")) {
            auto from = CCMenuItemExt::createSpriteExtra(sprite, [modID](CCMenuItemSpriteExtra*) { showInstallSource(modID); });
            from->setID("opengeode-from-button"_spr);
            managementMenu->addChild(from);
        }
    }

    if (auto more = managementMenu->getChildByID("opengeode-more-button"_spr)) {
        more->removeFromParentAndCleanup(true);
    }
    if (applyManagementButtonLimit(managementMenu)) {
        if (auto sprite = createThemedManageButton("More", "GJ_filterIcon_001.png")) {
            auto more = CCMenuItemExt::createSpriteExtra(sprite, [popup](CCMenuItemSpriteExtra*) {
                MoreManagePopup::create(popup)->show();
            });
            more->setID("opengeode-more-button"_spr);
            managementMenu->addChild(more);
        }
    }
    managementMenu->updateLayout();
}

} // namespace opengeode
