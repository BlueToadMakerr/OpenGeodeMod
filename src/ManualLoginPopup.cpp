#include "ManualLoginPopup.hpp"
#include "Settings.hpp"
#include "PopupSectionUtils.hpp"
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
using namespace geode::prelude;
namespace opengeode {
    class ManualLoginPopup: public Popup {
        TextInput * m_access = nullptr;
        TextInput * m_refresh = nullptr;
        IndexEntry m_entry;
        std::function < void() > m_onSaved;
        bool init(IndexEntry entry, std::function < void() > onSaved) {
            if (!Popup::init(320.f, 225.f, getPopupBackground())) return false;
            m_entry = std::move(entry);
            m_onSaved = std::move(onSaved);
            setTitle("Manual Login");
            if (auto close = createGeodeCloseButton()) setCloseButtonSpr(close,.875f);
            auto center = m_mainLayer->getContentWidth() / 2.f;
            auto top = m_mainLayer->getContentHeight() - 42.f;
            auto indexLabel = CCLabelBMFont::create(m_entry.name.c_str(), "bigFont.fnt");
            indexLabel->setScale(.38f);
            indexLabel->setPosition( {
                center, top
            }
            );
            m_mainLayer->addChild(indexLabel);
            auto accessLabel = CCLabelBMFont::create("Access Token", "bigFont.fnt");
            accessLabel->setScale(.32f);
            accessLabel->setPosition( {
                center, top - 27.f
            }
            );
            m_mainLayer->addChild(accessLabel);
            m_access = TextInput::create(260.f, "Access token", "chatFont.fnt");
            m_access->setString(getAuthAccessToken());
            m_access->setPosition( {
                center, top - 47.f
            }
            );
            m_mainLayer->addChild(m_access);
            auto refreshLabel = CCLabelBMFont::create("Refresh Token (Optional)", "bigFont.fnt");
            refreshLabel->setScale(.32f);
            refreshLabel->setPosition( {
                center, top - 77.f
            }
            );
            m_mainLayer->addChild(refreshLabel);
            m_refresh = TextInput::create(260.f, "Refresh token (optional)", "chatFont.fnt");
            m_refresh->setString(getAuthRefreshToken());
            m_refresh->setPosition( {
                center, top - 97.f
            }
            );
            m_mainLayer->addChild(m_refresh);
            auto save = CCMenuItemExt::createSpriteExtra(ButtonSprite::create("Save", "goldFont.fnt", getButtonTexture("GJ_button_01.png"),
            .6f), [this](auto) {
                auto access = std::string(m_access->getString().c_str()); auto refresh = std::string(m_refresh->getString().c_str()); if (access.empty()) {
                    FLAlertLayer::create("Invalid Credentials", "An access token is required.", "OK")->show(); return;
                }
                setAuthTokens(access, refresh); if (m_onSaved) m_onSaved(); onClose(nullptr);
            }
            );
            auto clear = CCMenuItemExt::createSpriteExtra(ButtonSprite::create("Clear", "goldFont.fnt", getButtonTexture("GJ_button_06.png"),
            .6f), [this](auto) {
                clearAuthTokens(); m_access->setString(""); m_refresh->setString(""); if (m_onSaved) m_onSaved();
            }
            );
            auto menu = CCMenu::create();
            menu->addChild(save);
            menu->addChild(clear);
            menu->setLayout(RowLayout::create()->setGap(10.f));
            menu->setPosition( {
                center, 20.f
            }
            );
            menu->updateLayout();
            m_mainLayer->addChild(menu);
            return true;
        }
        public: static ManualLoginPopup * create(IndexEntry entry, std::function < void() > onSaved) {
            auto ret = new ManualLoginPopup();
            if (ret && ret->init(std::move(entry), std::move(onSaved))) {
                ret->autorelease();
                return ret;
            }
            delete ret;
            return nullptr;
        }
    };
    void showManualLoginPopup(IndexEntry entry, std::function < void() > onSaved) {
        if (auto popup = ManualLoginPopup::create(std::move(entry), std::move(onSaved))) popup->show();
    }
}
// namespace opengeode
