#include "AddIndexPopup.hpp"
#include "PopupSectionUtils.hpp"
#include "Settings.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>

using namespace geode::prelude;

namespace opengeode {

class AddIndexPopup : public Popup {
protected:
    TextInput* m_nameInput = nullptr;
    TextInput* m_urlInput = nullptr;
    std::function<void()> m_onAdded;

    bool init(std::function<void()> onAdded) {
        if (!Popup::init(280.f, 160.f, getPopupBackground())) return false;
        m_onAdded = std::move(onAdded);

        this->setTitle("Add Index");

        float centerX = m_mainLayer->getContentWidth() / 2;
        float top = m_mainLayer->getContentHeight() - 30.f;

        auto nameLabel = CCLabelBMFont::create("Name", "bigFont.fnt");
        nameLabel->setScale(0.3f);
        nameLabel->setPosition({centerX, top});
        m_mainLayer->addChild(nameLabel);

        m_nameInput = TextInput::create(220.f, "My Custom Index", "chatFont.fnt");
        m_nameInput->setPosition({centerX, top - 20.f});
        m_mainLayer->addChild(m_nameInput);

        auto urlLabel = CCLabelBMFont::create("URL", "bigFont.fnt");
        urlLabel->setScale(0.3f);
        urlLabel->setPosition({centerX, top - 50.f});
        m_mainLayer->addChild(urlLabel);

        m_urlInput = TextInput::create(220.f, "https://example.com", "chatFont.fnt");
        m_urlInput->setPosition({centerX, top - 70.f});
        m_mainLayer->addChild(m_urlInput);

        auto addBtn = CCMenuItemExt::createSpriteExtra(
            ButtonSprite::create("Add", "goldFont.fnt", "GJ_button_01.png", 0.6f),
            [this](auto) {
                auto name = m_nameInput->getString();
                auto url = m_urlInput->getString();
                if (name.empty() || url.empty()) {
                    FLAlertLayer::create("Error", "Both a name and a URL are required.", "OK")->show();
                    return;
                }
                if (!addCustomIndex(name, url)) {
                    FLAlertLayer::create("Error", "An index with this URL already exists.", "OK")->show();
                    return;
                }
                if (m_onAdded) m_onAdded();
                this->onClose(nullptr);
            }
        );

        auto menu = CCMenu::create();
        menu->addChild(addBtn);
        menu->setPosition({centerX, 20.f});
        m_mainLayer->addChild(menu);

        return true;
    }

public:
    static AddIndexPopup* create(std::function<void()> onAdded) {
        auto ret = new AddIndexPopup();
        if (ret && ret->init(std::move(onAdded))) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

void showAddIndexPopup(std::function<void()> onAdded) {
    AddIndexPopup::create(std::move(onAdded))->show();
}

} // namespace opengeode
