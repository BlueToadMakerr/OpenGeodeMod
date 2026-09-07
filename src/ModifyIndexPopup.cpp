#include "ModifyIndexPopup.hpp"
#include "PopupSectionUtils.hpp"
#include "StatsFetcher.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>

using namespace geode::prelude;

namespace opengeode {

class ModifyIndexPopup : public Popup {
protected:
    TextInput* m_nameInput = nullptr;
    TextInput* m_urlInput = nullptr;
    StatsFetcher m_stats;
    std::string m_id;
    std::function<void()> m_onSaved;

    bool init(IndexEntry entry, std::function<void()> onSaved) {
        m_id = entry.id;
        m_onSaved = std::move(onSaved);

        if (!Popup::init(300.f, 220.f, getPopupBackground())) return false;
        this->setTitle("Modify Index");
        applyPopupTheme(this);

        float centerX = m_mainLayer->getContentWidth() / 2;
        float top = m_mainLayer->getContentHeight() - 40.f;

        auto nameLbl = CCLabelBMFont::create("Name", "bigFont.fnt");
        nameLbl->setScale(0.35f);
        nameLbl->setPosition({centerX, top});
        m_mainLayer->addChild(nameLbl);

        m_nameInput = TextInput::create(220.f, "Name", "chatFont.fnt");
        m_nameInput->setString(entry.name);
        m_nameInput->setPosition({centerX, top - 20.f});
        m_mainLayer->addChild(m_nameInput);

        auto urlLbl = CCLabelBMFont::create("URL", "bigFont.fnt");
        urlLbl->setScale(0.35f);
        urlLbl->setPosition({centerX, top - 50.f});
        m_mainLayer->addChild(urlLbl);

        m_urlInput = TextInput::create(220.f, "https://example.com", "chatFont.fnt");
        m_urlInput->setString(entry.url);
        m_urlInput->setPosition({centerX, top - 70.f});
        m_mainLayer->addChild(m_urlInput);

        m_stats.label = CCLabelBMFont::create("Fetching index stats...", "bigFont.fnt");
        m_stats.label->setScale(0.35f);
        m_stats.label->setPosition({centerX, top - 100.f});
        m_mainLayer->addChild(m_stats.label);

        auto saveBtn = CCMenuItemExt::createSpriteExtra(
            ButtonSprite::create("Save", "goldFont.fnt", getButtonTexture("GJ_button_02.png"), 0.6f),
            [this](auto) {
                auto name = m_nameInput->getString();
                auto url = m_urlInput->getString();
                if (name.empty() || url.empty()) {
                    FLAlertLayer::create("Error", "Both a name and a URL are required.", "OK")->show();
                    return;
                }
                if (!updateCustomIndex(m_id, name, url)) {
                    FLAlertLayer::create("Error", "Another index with this URL already exists.", "OK")->show();
                    return;
                }
                if (m_onSaved) m_onSaved();
                this->onClose(nullptr);
            }
        );

        auto menu = CCMenu::create();
        menu->addChild(saveBtn);
        menu->setPosition({centerX, 20.f});
        m_mainLayer->addChild(menu);

        m_stats.fetch(entry.url);
        return true;
    }

public:
    static ModifyIndexPopup* create(IndexEntry entry, std::function<void()> onSaved) {
        auto ret = new ModifyIndexPopup();
        if (ret && ret->init(std::move(entry), std::move(onSaved))) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

void showModifyIndexPopup(IndexEntry entry, std::function<void()> onSaved) {
    ModifyIndexPopup::create(std::move(entry), std::move(onSaved))->show();
}

} // namespace opengeode
