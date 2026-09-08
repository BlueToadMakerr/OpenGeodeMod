#include "MoreManagePopup.hpp"
#include "InstalledMods.hpp"
#include "PopupSectionUtils.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/IconButtonSprite.hpp>
#include <Geode/ui/Popup.hpp>

using namespace geode::prelude;

namespace opengeode {

namespace {

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
    CCNode* visual = nullptr;
};

NativeAction getVisibleNativeAction(CCNode* popup, char const* id) {
    auto node = popup->getChildByIDRecursive(id);
    if (!node || !node->isVisible()) return {};

    if (auto toggler = typeinfo_cast<CCMenuItemToggler*>(node)) {
        if (toggler->m_onButton && toggler->m_onButton->isVisible()) {
            return {toggler, toggler->m_onButton};
        }
        if (toggler->m_offButton && toggler->m_offButton->isVisible()) {
            return {toggler, toggler->m_offButton};
        }
        return {};
    }

    auto item = typeinfo_cast<CCMenuItemSpriteExtra*>(node);
    if (!item) return {};
    return {item, item->getNormalImage()};
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

void showInstallSource(std::string const& modID) {
    auto source = getInstalledModSource(modID);
    if (!source) return;
    auto description = fmt::format(
        "Installed from <cy>{}</c>\nVersion: <cg>{}</c>\n{}",
        source->indexName.empty() ? source->indexUrl : source->indexName,
        source->version,
        source->indexUrl
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

void logMoreNodeDetails(char const* label, CCNode* node) {
    if (!node) {
        log::debug("[OpenGeode][More] {}: null", label);
        return;
    }

    auto size = node->getContentSize();
    auto anchor = node->getAnchorPoint();
    log::debug(
        "[OpenGeode][More] {}: ptr={} id='{}' sprite={} iconButton={} visible={} children={} size={}x{} anchor=({}, {}) scale=({}, {}) parent={}",
        label,
        static_cast<void*>(node),
        node->getID(),
        typeinfo_cast<CCSprite*>(node) != nullptr,
        typeinfo_cast<IconButtonSprite*>(node) != nullptr,
        node->isVisible(),
        node->getChildrenCount(),
        size.width,
        size.height,
        anchor.x,
        anchor.y,
        node->getScaleX(),
        node->getScaleY(),
        static_cast<void*>(node->getParent())
    );

    auto index = 0;
    for (auto child : CCArrayExt<CCNode*>(node->getChildren())) {
        if (!child) continue;
        auto childSize = child->getContentSize();
        log::debug(
            "[OpenGeode][More] {} child[{}]: ptr={} id='{}' sprite={} iconButton={} visible={} children={} size={}x{} scale=({}, {})",
            label,
            index,
            static_cast<void*>(child),
            child->getID(),
            typeinfo_cast<CCSprite*>(child) != nullptr,
            typeinfo_cast<IconButtonSprite*>(child) != nullptr,
            child->isVisible(),
            child->getChildrenCount(),
            childSize.width,
            childSize.height,
            child->getScaleX(),
            child->getScaleY()
        );
        ++index;
    }
}

void logIconButtonDetails(char const* label, CCNode* node) {
    auto button = typeinfo_cast<IconButtonSprite*>(node);
    if (!button) {
        log::debug("[OpenGeode][More] {}: not an IconButtonSprite", label);
        return;
    }

    auto bg = button->getBg();
    auto text = button->getLabel();
    auto icon = button->getIcon();

    log::debug(
        "[OpenGeode][More] {} IconButtonSprite: ptr={} string='{}' bg={} label={} icon={} content={}x{} scale=({}, {}) position=({}, {})",
        label,
        static_cast<void*>(button),
        button->getString(),
        static_cast<void*>(bg),
        static_cast<void*>(text),
        static_cast<void*>(icon),
        button->getContentSize().width,
        button->getContentSize().height,
        button->getScaleX(),
        button->getScaleY(),
        button->getPositionX(),
        button->getPositionY()
    );

    if (bg) {
        auto insets = bg->getInsets();
        log::debug(
            "[OpenGeode][More] {} background: ptr={} size={}x{} scale=({}, {}) position=({}, {}) multiplier={} repeat={} insets=(top={}, right={}, bottom={}, left={})",
            label,
            static_cast<void*>(bg),
            bg->getContentSize().width,
            bg->getContentSize().height,
            bg->getScaleX(),
            bg->getScaleY(),
            bg->getPositionX(),
            bg->getPositionY(),
            bg->getScaleMultiplier(),
            bg->getRepeatCenter(),
            insets.top,
            insets.right,
            insets.bottom,
            insets.left
        );
        logMoreNodeDetails("IconButton background", bg);
    }

    if (text) {
        log::debug(
            "[OpenGeode][More] {} label: ptr={} string='{}' size={}x{} scale=({}, {}) position=({}, {}) anchor=({}, {})",
            label,
            static_cast<void*>(text),
            text->getString(),
            text->getContentSize().width,
            text->getContentSize().height,
            text->getScaleX(),
            text->getScaleY(),
            text->getPositionX(),
            text->getPositionY(),
            text->getAnchorPoint().x,
            text->getAnchorPoint().y
        );
    }

    if (icon) {
        logMoreNodeDetails("IconButton icon", icon);
        if (auto sprite = typeinfo_cast<CCSprite*>(icon)) {
            log::debug(
                "[OpenGeode][More] {} icon sprite: texture={} displayFrame={} textureRect={} rectRotated={}",
                label,
                static_cast<void*>(sprite->getTexture()),
                static_cast<void*>(sprite->getDisplayFrame()),
                sprite->getTextureRect().size.width,
                sprite->getTextureRect().size.height,
                sprite->isTextureRectRotated()
            );
        }
    }
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
        if (!managementMenu) {
            log::debug("[OpenGeode][More] Cannot build More: native management menu was not found");
            return true;
        }

        log::debug(
            "[OpenGeode][More] Management menu: ptr={} children={} size={}x{} position=({}, {}) scale=({}, {})",
            static_cast<void*>(managementMenu),
            managementMenu->getChildrenCount(),
            managementMenu->getContentSize().width,
            managementMenu->getContentSize().height,
            managementMenu->getPositionX(),
            managementMenu->getPositionY(),
            managementMenu->getScaleX(),
            managementMenu->getScaleY()
        );

        for (auto child : CCArrayExt<CCNode*>(managementMenu->getChildren())) {
            if (!child) continue;
            auto id = child->getID();

            if (id == "opengeode-more-button") continue;

            auto action = typeinfo_cast<CCMenuItem*>(child);
            if (!action) {
                log::debug("[OpenGeode][More] Skip id='{}': child is not a CCMenuItem", id);
                logMoreNodeDetails("non-action child", child);
                continue;
            }

            log::debug(
                "[OpenGeode][More] Candidate id='{}': ptr={} toggler={} spriteExtra={} visible={} children={}",
                id,
                static_cast<void*>(action),
                typeinfo_cast<CCMenuItemToggler*>(action) != nullptr,
                typeinfo_cast<CCMenuItemSpriteExtra*>(action) != nullptr,
                action->isVisible(),
                action->getChildrenCount()
            );

            CCNode* visual = nullptr;
            if (auto toggler = typeinfo_cast<CCMenuItemToggler*>(action)) {
                logMoreNodeDetails("toggler on", toggler->m_onButton);
                logMoreNodeDetails("toggler off", toggler->m_offButton);
                if (toggler->m_onButton && toggler->m_onButton->isVisible()) visual = toggler->m_onButton;
                else if (toggler->m_offButton && toggler->m_offButton->isVisible()) visual = toggler->m_offButton;

                if (visual) {
                    for (auto nested : CCArrayExt<CCNode*>(visual->getChildren())) {
                        if (auto iconButton = typeinfo_cast<IconButtonSprite*>(nested)) {
                            logIconButtonDetails("visible toggler IconButtonSprite", iconButton);
                        }
                    }
                }
            }
            else if (auto spriteItem = typeinfo_cast<CCMenuItemSpriteExtra*>(action)) {
                visual = spriteItem->getNormalImage();
                logMoreNodeDetails("sprite-extra normal", visual);
                logMoreNodeDetails("sprite-extra selected", spriteItem->getSelectedImage());
                logIconButtonDetails("sprite-extra normal", visual);
                logIconButtonDetails("sprite-extra selected", spriteItem->getSelectedImage());
            }

            if (!visual) {
                log::debug("[OpenGeode][More] id='{}': no visual could be selected", id);
                continue;
            }

            logMoreNodeDetails("selected visual", visual);
            logIconButtonDetails("selected visual", visual);

            log::debug(
                "[OpenGeode][More] COPY TEST id='{}': attempting copyWithZone(nullptr) on visual={}",
                id,
                static_cast<void*>(visual)
            );
            auto copy = typeinfo_cast<CCNode*>(visual->copyWithZone(nullptr));
            if (!copy) {
                log::debug(
                    "[OpenGeode][More] COPY FAILED id='{}': copyWithZone(nullptr) returned null",
                    id
                );
                continue;
            }

            logMoreNodeDetails("copied visual", copy);

            copy->setPosition({0.f, 0.f});
            copy->setScale(.5f);

            auto item = CCMenuItemExt::createSpriteExtra(
                copy,
                [action, this](CCMenuItemSpriteExtra*) {
                    action->activate();
                    this->onClose(nullptr);
                }
            );
            menu->addChild(item);
            log::debug("[OpenGeode][More] Added id='{}' to More", id);
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
    if (!managementMenu->getChildByID("opengeode-from-button"_spr) &&
        !modID.empty() && getInstalledModSource(modID) && isPopupInstalled(popup)) {
        if (auto sprite = createThemedManageButton("From", "GJ_downloadsIcon_001.png")) {
            auto from = CCMenuItemExt::createSpriteExtra(
                sprite,
                [modID](CCMenuItemSpriteExtra*) { showInstallSource(modID); }
            );
            from->setID("opengeode-from-button"_spr);
            managementMenu->addChild(from);
        }
    }

    if (!managementMenu->getChildByID("opengeode-more-button"_spr)) {
        if (auto sprite = createThemedManageButton("More", "GJ_filterIcon_001.png")) {
            auto more = CCMenuItemExt::createSpriteExtra(
                sprite,
                [popup](CCMenuItemSpriteExtra*) {
                    MoreManagePopup::create(popup)->show();
                }
            );
            more->setID("opengeode-more-button"_spr);
            managementMenu->addChild(more);
        }
    }

    managementMenu->updateLayout();
}

} // namespace opengeode
