#include "AccountPopup.hpp"
#include "InstalledMods.hpp"
#include "Settings.hpp"
#include "PopupSectionUtils.hpp"
#include <Geode/modify/ModsLayer.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/base64.hpp>
#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <memory>
#include <string>

using namespace geode::prelude;

namespace opengeode {

std::string getPopupModID(CCNode* popup) {
    auto label = typeinfo_cast<CCLabelBMFont*>(popup->getChildByIDRecursive("mod-id-label"));
    if (!label) return "";
    std::string value = label->getString();
    auto prefix = std::string("(ID: ");
    if (!value.starts_with(prefix) || value.size() < prefix.size() + 1) return "";
    value.erase(0, prefix.size());
    if (!value.empty() && value.back() == ')') value.pop_back();
    return value;
}

void showInstallSource(std::string const& modID) {
    auto source = getInstalledModSource(modID);
    if (!source) return;

    auto description = fmt::format(
        "Installed from <cy>{}</c>\nVersion: <cg>{}</c>\n{}",
        source->indexName,
        source->version,
        source->indexUrl
    );
    createQuickPopup(
        "Install Source",
        description,
        "OK",
        "",
        [](FLAlertLayer*, bool) {}
    );
}

class MoreManagePopup : public Popup {
protected:
    CCNode* m_modPopup = nullptr;

    bool init(CCNode* modPopup) {
        if (!Popup::init(190.f, 255.f, getPopupBackground())) return false;
        m_modPopup = modPopup;
        setTitle("More");
        if (auto close = createGeodeCloseButton()) setCloseButtonSpr(close, .8f);

        auto menu = CCMenu::create();
        menu->setContentSize({150.f, 190.f});
        menu->setPosition({20.f, 35.f});
        menu->setLayout(
            ColumnLayout::create()
                ->setGap(6.f)
                ->setAxisAlignment(AxisAlignment::Center)
        );
        this->addChild(menu);

        if (auto modID = getPopupModID(modPopup); !modID.empty()) {
            if (getInstalledModSource(modID)) {
                auto item = CCMenuItemExt::createSpriteExtra(
                    ButtonSprite::create("From", 40, true, "goldFont.fnt", "GJ_button_01.png", 25.f, .6f),
                    [modID](CCMenuItemSpriteExtra*) { showInstallSource(modID); }
                );
                menu->addChild(item);
            }
        }

        struct NativeAction {
            char const* id;
            char const* text;
        };
        for (auto const& action : {
            NativeAction{"update-button", "Update"},
            NativeAction{"enable-button", "Enable"},
            NativeAction{"reenable-button", "Re-Enable"},
            NativeAction{"unavailable-button", "Unavailable"},
            NativeAction{"install-button", "Install"},
            NativeAction{"uninstall-button", "Uninstall"},
            NativeAction{"cancel-button", "Cancel"}
        }) {
            auto native = m_modPopup->getChildByIDRecursive(action.id);
            auto menuItem = typeinfo_cast<CCMenuItem*>(native);
            if (!menuItem || !menuItem->isVisible()) continue;

            auto item = CCMenuItemExt::createSpriteExtra(
                ButtonSprite::create(action.text, 40, true, "goldFont.fnt", "GJ_button_01.png", 25.f, .6f),
                [this, menuItem](CCMenuItemSpriteExtra*) {
                    this->onClose(nullptr);
                    menuItem->activate();
                }
            );
            menu->addChild(item);
        }

        menu->updateLayout();
        return true;
    }

public:
    static MoreManagePopup* create(CCNode* modPopup) {
        auto ret = new MoreManagePopup;
        if (ret->init(modPopup)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

void ensureModPopupExtras(CCNode* popup) {
    auto manageTitle = popup->getChildByIDRecursive("manage-title");
    if (!manageTitle) return;

    if (popup->getChildByID("opengeode-manage-extras")) return;

    auto menu = CCMenu::create();
    menu->setID("opengeode-manage-extras");
    menu->setContentSize({150.f, 50.f});
    menu->setAnchorPoint({.5f, .5f});
    menu->setPosition({popup->getContentSize().width / 2.f, 35.f});
    menu->setLayout(
        RowLayout::create()
            ->setGap(8.f)
            ->setAxisAlignment(AxisAlignment::Center)
    );
    popup->addChild(menu);

    auto modID = getPopupModID(popup);
    if (!modID.empty() && getInstalledModSource(modID)) {
        auto item = CCMenuItemExt::createSpriteExtra(
            ButtonSprite::create("From", 40, true, "goldFont.fnt", "GJ_button_01.png", 25.f, .6f),
            [modID](CCMenuItemSpriteExtra*) { showInstallSource(modID); }
        );
        menu->addChild(item);
    }

    auto more = CCMenuItemExt::createSpriteExtra(
        ButtonSprite::create("More", 40, true, "goldFont.fnt", "GJ_button_01.png", 25.f, .6f),
        [popup](CCMenuItemSpriteExtra*) {
            MoreManagePopup::create(popup)->show();
        }
    );
    menu->addChild(more);
    menu->updateLayout();
}

void ensureOpenGeodeModPopupExtras(CCNode* scene) {
    auto manageTitle = scene->getChildByIDRecursive("manage-title");
    if (!manageTitle) return;

    for (auto node = manageTitle->getParent(); node; node = node->getParent()) {
        if (node->getChildByIDRecursive("mod-id-label")) {
            ensureModPopupExtras(node);
            return;
        }
    }
}

class ModsLayerWatcher : public CCNode {
public:
    bool init() {
        if (!CCNode::init()) return false;
        schedule(schedule_selector(ModsLayerWatcher::check), 0.2f);
        return true;
    }

    void check(float) {
        auto scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene) return;
        ensureOpenGeodeModPopupExtras(scene);
    }

    static ModsLayerWatcher* create() {
        auto ret = new ModsLayerWatcher;
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

class $modify(ModsLayer) {
    bool init() {
        if (!ModsLayer::init()) return false;

        auto watcher = ModsLayerWatcher::create();
        watcher->setID("opengeode-mods-watcher");
        this->addChild(watcher);
        return true;
    }
};

} // namespace opengeode
