#include "PresetIndexPopup.hpp"
#include "PopupSectionUtils.hpp"
#include "Settings.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/Popup.hpp>

using namespace geode::prelude;

namespace opengeode {

class PresetIndexPopup : public Popup {
protected:
    std::function<void()> m_onAdded;

    bool init(std::function<void()> onAdded) {
        if (!Popup::init(300.f, 200.f, getPopupBackground())) return false;
        m_onAdded = std::move(onAdded);

        this->setTitle("Pre-made Indexes");
        if (auto close = createGeodeCloseButton())
            this->setCloseButtonSpr(close, 0.875f);

        float centerX = m_mainLayer->getContentWidth() / 2;
        float top = m_mainLayer->getContentHeight() - 50.f;

        auto infoLabel = CCLabelBMFont::create("Select a pre-made index to add:", "chatFont.fnt");
        infoLabel->setScale(0.45f);
        infoLabel->setPosition({centerX, top});
        m_mainLayer->addChild(infoLabel);

        auto menu = CCMenu::create();
        menu->setLayout(ColumnLayout::create()->setGap(5.f));
        menu->setPosition({centerX, top - 65.f});

        auto addPreset = [this](char const* name, char const* url) {
            return CCMenuItemExt::createSpriteExtra(
                ButtonSprite::create(name, "goldFont.fnt", getButtonTexture("GJ_button_01.png"), 0.6f),
                [this, name, url](auto) {
                    if (addCustomIndex(name, url)) {
                        if (m_onAdded) m_onAdded();
                        this->onClose(nullptr);
                    }
                    else {
                        FLAlertLayer::create("Error", "This index already exists in your list.", "OK")->show();
                    }
                }
            );
        };

        menu->addChild(addPreset("Geode Index API", "https://api.geode-sdk.org"));
        menu->addChild(addPreset("Open Geode Index", "https://open-geode.7m.pl"));
        menu->addChild(addPreset("Rejected Index", "http://drake-tableful.tun.ply.gg:21749"));
        menu->updateLayout();
        m_mainLayer->addChild(menu);

        return true;
    }

public:
    static PresetIndexPopup* create(std::function<void()> onAdded) {
        auto ret = new PresetIndexPopup();
        if (ret && ret->init(std::move(onAdded))) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

void showPresetIndexPopup(std::function<void()> onAdded) {
    PresetIndexPopup::create(std::move(onAdded))->show();
}

} // namespace opengeode
