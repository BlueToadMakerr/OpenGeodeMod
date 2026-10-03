#include "ModifyIndexPopup.hpp"
#include "PopupSectionUtils.hpp"
#include "StatsFetcher.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextArea.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/utils/web.hpp>

using namespace geode::prelude;

namespace opengeode {

class IndexInfoPopup : public Popup {
    bool init(std::string message) {
        if (!Popup::init(360.f, 220.f, getPopupBackground())) return false;
        setTitle("Index Info");
        if (auto close = createGeodeCloseButton()) setCloseButtonSpr(close, .875f);

        auto area = SimpleTextArea::create(
            std::move(message),
            "chatFont.fnt",
            .5f,
            325.f
        );
        if (!area) return false;
        area->setWrappingMode(NO_WRAP);
        area->setAlignment(kCCTextAlignmentLeft);
        area->setPosition({18.f, 180.f});
        m_mainLayer->addChild(area);

        return true;
    }

public:
    static IndexInfoPopup* create(std::string message) {
        auto ret = new IndexInfoPopup();
        if (ret && ret->init(std::move(message))) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

class ModifyIndexPopup : public Popup {
protected:
    TextInput* m_nameInput = nullptr;
    TextInput* m_urlInput = nullptr;
    StatsFetcher m_stats;
    std::string m_id;
    std::function<void()> m_onSaved;
    async::TaskHolder<web::WebResponse> m_infoTask;

    bool init(IndexEntry entry, std::function<void()> onSaved) {
        m_id = entry.id;
        m_onSaved = std::move(onSaved);
        if (!Popup::init(300.f, 220.f, getPopupBackground())) return false;
        setTitle("Modify Index");
        if (auto close = createGeodeCloseButton()) setCloseButtonSpr(close, .875f);

        float centerX = m_mainLayer->getContentWidth() / 2.f;
        float top = m_mainLayer->getContentHeight() - 30.f;

        auto nameLbl = CCLabelBMFont::create("Name", "bigFont.fnt");
        nameLbl->setScale(.35f);
        nameLbl->setPosition({centerX, top});
        m_mainLayer->addChild(nameLbl);
        m_nameInput = TextInput::create(220.f, "Name", "chatFont.fnt");
        m_nameInput->setString(entry.name);
        m_nameInput->setPosition({centerX, top - 22.f});
        m_mainLayer->addChild(m_nameInput);

        auto urlLbl = CCLabelBMFont::create("URL", "bigFont.fnt");
        urlLbl->setScale(.35f);
        urlLbl->setPosition({centerX, top - 54.f});
        m_mainLayer->addChild(urlLbl);
        m_urlInput = TextInput::create(220.f, "https://example.com", "chatFont.fnt");
        m_urlInput->setString(entry.url);
        m_urlInput->setPosition({centerX, top - 76.f});
        m_mainLayer->addChild(m_urlInput);

        m_stats.label = CCLabelBMFont::create("Fetching index stats...", "bigFont.fnt");
        m_stats.label->setScale(.32f);
        m_stats.label->setPosition({centerX, top - 99.f});
        m_mainLayer->addChild(m_stats.label);
        m_stats.opengeodeLabel = CCLabelBMFont::create("Open Geode: Loading...", "bigFont.fnt");
        m_stats.opengeodeLabel->setScale(.32f);
        m_stats.opengeodeLabel->setPosition({centerX, top - 116.f});
        m_stats.opengeodeLabel->setVisible(false);
        m_mainLayer->addChild(m_stats.opengeodeLabel);

        auto infoIcon = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
        auto infoBtn = CCMenuItemExt::createSpriteExtra(
            infoIcon,
            [this](CCMenuItemSpriteExtra*) {
                m_infoTask.spawn(
                    web::WebRequest().get(m_urlInput->getString().c_str()),
                    [this](web::WebResponse res) {
                        if (!res.ok()) {
                            Loader::get()->queueInMainThread([code = res.code()] {
                                FLAlertLayer::create(
                                    "Index Info",
                                    fmt::format("Could not fetch the index message. (HTTP {})", code).c_str(),
                                    "OK"
                                )->show();
                            });
                            return;
                        }

                        auto message = res.string().unwrapOr("");
                        if (message.empty()) message = "This index did not provide a message.";

                        Loader::get()->queueInMainThread([message = std::move(message)]() mutable {
                            if (auto popup = IndexInfoPopup::create(std::move(message))) {
                                popup->show();
                            }
                        });
                    }
                );
            }
        );
        infoBtn->setScale(.8f);
        auto infoMenu = CCMenu::create();
        infoMenu->addChild(infoBtn);
        infoMenu->setPosition({m_mainLayer->getContentWidth() - 34.f, m_mainLayer->getContentHeight() - 20.f});
        infoMenu->updateLayout();
        m_mainLayer->addChild(infoMenu);

        auto saveBtn = CCMenuItemExt::createSpriteExtra(
            ButtonSprite::create("Save", "goldFont.fnt", getButtonTexture("GJ_button_02.png"), .6f),
            [this](auto) {
                auto name = std::string(m_nameInput->getString().c_str());
                auto url = std::string(m_urlInput->getString().c_str());
                if (name.empty() || url.empty()) {
                    FLAlertLayer::create("Error", "Both a name and a URL are required.", "OK")->show();
                    return;
                }
                if (!updateCustomIndex(m_id, name, url)) {
                    FLAlertLayer::create("Error", "Another index with this URL already exists.", "OK")->show();
                    return;
                }
                if (m_onSaved) m_onSaved();
                onClose(nullptr);
            }
        );
        auto menu = CCMenu::create();
        menu->addChild(saveBtn);
        menu->setPosition({centerX, 16.f});
        menu->updateLayout();
        m_mainLayer->addChild(menu);
        m_stats.fetch(entry.url);
        return true;
    }

public:
    static ModifyIndexPopup* create(IndexEntry entry, std::function<void()> onSaved) {
        auto ret = new ModifyIndexPopup();
        if (ret && ret->init(std::move(entry), std::move(onSaved))) { ret->autorelease(); return ret; }
        delete ret;
        return nullptr;
    }
};

void showModifyIndexPopup(IndexEntry entry, std::function<void()> onSaved) {
    if (auto popup = ModifyIndexPopup::create(std::move(entry), std::move(onSaved))) popup->show();
}

} // namespace opengeode
