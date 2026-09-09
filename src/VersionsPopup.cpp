#include "VersionsPopup.hpp"
#include "InstalledMods.hpp"
#include "PopupSectionUtils.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/utils/web.hpp>

using namespace geode::prelude;

namespace opengeode {
namespace {

std::string versionStatusText(std::string const& status) {
    if (status == "accepted" || status.empty()) return "";
    return fmt::format("({})", status);
}

class VersionRow : public CCNode {
    std::string m_modID;
    std::string m_version;
    CCNode* m_modPopup = nullptr;
    Popup* m_versionsPopup = nullptr;
    CCMenuItemSpriteExtra* m_installButton = nullptr;

    void installVersion(CCObject*) {
        if (!m_modPopup) return;
        auto install = m_modPopup->getChildByIDRecursive("install-button");
        auto action = typeinfo_cast<CCMenuItem*>(install);
        if (!action) return;

        setPendingVersionInstall(m_modID, m_version);
        action->activate();
        if (m_versionsPopup) m_versionsPopup->onClose(nullptr);
    }

public:
    static VersionRow* create(
        std::string modID,
        std::string name,
        std::string version,
        int downloadCount,
        std::string status,
        std::string createdAt,
        CCNode* modPopup,
        Popup* versionsPopup
    ) {
        auto ret = new VersionRow();
        if (ret && ret->init(
            std::move(modID), std::move(name), std::move(version), downloadCount,
            std::move(status), std::move(createdAt), modPopup, versionsPopup
        )) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    bool init(
        std::string modID,
        std::string name,
        std::string version,
        int downloadCount,
        std::string status,
        std::string createdAt,
        CCNode* modPopup,
        Popup* versionsPopup
    ) {
        if (!CCNode::init()) return false;
        m_modID = std::move(modID);
        m_version = std::move(version);
        m_modPopup = modPopup;
        m_versionsPopup = versionsPopup;
        setContentSize({245.f, 72.f});

        auto title = CCLabelBMFont::create(
            fmt::format("{}  Downloads: {}", name, downloadCount).c_str(), "bigFont.fnt"
        );
        title->setScale(.40f);
        title->setAnchorPoint({0.f, .5f});
        addChildAtPosition(title, Anchor::Left, ccp(4.f, 48.f));

        auto versionLabel = CCLabelBMFont::create(
            fmt::format("{} {}", m_version, versionStatusText(status)).c_str(), "goldFont.fnt"
        );
        versionLabel->setScale(.30f);
        versionLabel->setAnchorPoint({0.f, .5f});
        addChildAtPosition(versionLabel, Anchor::Left, ccp(4.f, 29.f));

        auto released = CCLabelBMFont::create(
            fmt::format("Released: {}", createdAt).c_str(), "goldFont.fnt"
        );
        released->setScale(.28f);
        released->setAnchorPoint({0.f, .5f});
        addChildAtPosition(released, Anchor::Left, ccp(4.f, 11.f));

        auto installed = getInstalledModSource(m_modID);
        bool isInstalled = installed && installed->version == m_version;
        auto buttonSprite = ButtonSprite::create(
            isInstalled ? "Installed" : "Install",
            "bigFont.fnt", getButtonTexture("GJ_button_01.png"), .65f
        );
        buttonSprite->setScale(.50f);
        m_installButton = CCMenuItemSpriteExtra::create(
            buttonSprite,
            this,
            menu_selector(VersionRow::installVersion)
        );
        if (isInstalled) m_installButton->setEnabled(false);

        auto menu = CCMenu::create();
        menu->addChild(m_installButton);
        menu->setContentSize({66.f, 45.f});
        addChildAtPosition(menu, Anchor::Right, ccp(-2.f, 0.f));

        return true;
    }
};

class VersionsPopup : public Popup {
    async::TaskHolder<web::WebResponse> m_requestTask;
    CCLabelBMFont* m_loadingLabel = nullptr;
    CCLabelBMFont* m_errorLabel = nullptr;

    void showError(std::string const& reason) {
        if (m_loadingLabel) m_loadingLabel->setVisible(false);
        if (m_errorLabel) {
            m_errorLabel->setString(reason.c_str());
            m_errorLabel->setVisible(true);
        }
    }

public:
    static VersionsPopup* create(std::string modID, CCNode* modPopup) {
        auto ret = new VersionsPopup();
        if (ret && ret->init(std::move(modID), modPopup)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    bool init(std::string modID, CCNode* modPopup) {
        if (!Popup::init(290.f, 310.f, getPopupBackground())) return false;
        setTitle("Versions");
        if (auto close = createGeodeCloseButton()) setCloseButtonSpr(close, .8f);

        auto scrollBackground = CCScale9Sprite::create(
            isGeodeTheme() ? "geode.loader/GE_square02.png" : "square02b_001.png",
            CCRect(12.f, 12.f, 26.f, 26.f)
        );
        if (scrollBackground) {
            scrollBackground->setContentSize({268.f, 250.f});
            m_mainLayer->addChildAtPosition(scrollBackground, Anchor::Center, ccp(0.f, -3.f));
        }

        auto scroll = ScrollLayer::create({260.f, 242.f});
        scroll->setPosition({15.f, 30.f});
        scroll->m_contentLayer->setLayout(ScrollLayer::createDefaultListLayout(4.f));
        m_mainLayer->addChild(scroll);

        m_loadingLabel = CCLabelBMFont::create("Loading...", "goldFont.fnt");
        m_loadingLabel->setScale(.32f);
        m_mainLayer->addChildAtPosition(m_loadingLabel, Anchor::Center, ccp(0.f, -3.f));

        m_errorLabel = CCLabelBMFont::create("", "goldFont.fnt");
        m_errorLabel->setScale(.26f);
        m_errorLabel->setVisible(false);
        m_mainLayer->addChildAtPosition(m_errorLabel, Anchor::Center, ccp(0.f, -22.f));

        m_requestTask.spawn(
            "OpenGeode version list",
            web::WebRequest().get(fmt::format("https://api.geode-sdk.org/v1/mods/{}", modID)),
            [this, scroll, modID, modPopup](web::WebResponse response) {
                if (!response.ok()) {
                    auto reason = response.errorMessage();
                    showError(reason.empty() ? "Request failed." : reason);
                    return;
                }

                auto json = response.json();
                if (!json) {
                    showError("The server returned invalid JSON.");
                    return;
                }
                auto payload = (*json)["payload"];
                if (!payload.isObject()) {
                    showError("The server response did not contain version data.");
                    return;
                }
                auto versions = payload["versions"];
                if (!versions.isArray()) {
                    showError("The server response did not contain a versions list.");
                    return;
                }

                m_loadingLabel->setVisible(false);
                for (auto const& version : versions) {
                    if (!version.isObject()) continue;
                    auto row = VersionRow::create(
                        modID,
                        version["name"].asString().unwrapOr(modID),
                        version["version"].asString().unwrapOr("unknown"),
                        version["download_count"].asInt().unwrapOr(0),
                        version["status"].asString().unwrapOr("accepted"),
                        version["created_at"].asString().unwrapOr("unknown"),
                        modPopup,
                        this
                    );
                    if (row) scroll->m_contentLayer->addChild(row);
                }
                scroll->m_contentLayer->updateLayout();
                scroll->scrollToTop();
            }
        );
        return true;
    }
};

} // namespace

void showVersionsPopup(std::string const& modID, CCNode* modPopup) {
    if (modID.empty() || !modPopup) return;
    VersionsPopup::create(modID, modPopup)->show();
}

} // namespace opengeode
