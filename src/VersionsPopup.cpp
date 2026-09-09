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
    if (status.empty() || status == "accepted") return "";
    return fmt::format(" ({})", status);
}

class VersionsPopup;

class VersionContainer : public CCNode {
    std::string m_modID;
    std::string m_version;
    CCNode* m_modPopup = nullptr;
    VersionsPopup* m_versionsPopup = nullptr;

    void downloadVersion(CCObject*);

public:
    static VersionContainer* create(
        std::string modID, std::string name, std::string version,
        std::string status, std::string createdAt,
        CCNode* modPopup, VersionsPopup* versionsPopup
    ) {
        auto ret = new VersionContainer();
        if (ret && ret->init(std::move(modID), std::move(name), std::move(version),
            std::move(status), std::move(createdAt), modPopup, versionsPopup)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    bool init(std::string modID, std::string name, std::string version,
        std::string status, std::string createdAt,
        CCNode* modPopup, VersionsPopup* versionsPopup);
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
    void closeVersionsPopup() { onClose(nullptr); }

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

        auto scroll = ScrollLayer::create({260.f, 250.f});
        scroll->setPosition({15.f, 25.f});
        scroll->m_contentLayer->setLayout(ScrollLayer::createDefaultListLayout(4.f));
        m_mainLayer->addChild(scroll);

        m_loadingLabel = CCLabelBMFont::create("Loading...", "goldFont.fnt");
        m_loadingLabel->setScale(.32f);
        m_mainLayer->addChildAtPosition(m_loadingLabel, Anchor::Center, ccp(0.f, -5.f));

        m_errorLabel = CCLabelBMFont::create("", "goldFont.fnt");
        m_errorLabel->setScale(.26f);
        m_errorLabel->setVisible(false);
        m_mainLayer->addChildAtPosition(m_errorLabel, Anchor::Center, ccp(0.f, -25.f));

        m_requestTask.spawn(
            "OpenGeode version list",
            web::WebRequest().get(fmt::format("https://api.geode-sdk.org/v1/mods/{}", modID)),
            [this, scroll, modID, modPopup](web::WebResponse response) {
                if (!response.ok()) {
                    auto reason = response.errorMessage();
                    showError(reason.empty() ? std::string("Request failed.") : std::string(reason));
                    return;
                }

                auto json = response.json();
                if (!json) {
                    showError("The server returned invalid JSON.");
                    return;
                }

                auto payload = (*json)["payload"];
                auto versions = payload["versions"];
                if (!payload.isObject() || !versions.isArray()) {
                    showError("The server response did not contain a versions list.");
                    return;
                }

                m_loadingLabel->setVisible(false);
                for (auto const& version : versions) {
                    if (!version.isObject()) continue;
                    auto row = VersionContainer::create(
                        modID,
                        version["name"].asString().unwrapOr(modID),
                        version["version"].asString().unwrapOr("unknown"),
                        version["status"].asString().unwrapOr("accepted"),
                        version["created_at"].asString().unwrapOr("unknown"),
                        modPopup, this
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

bool VersionContainer::init(
    std::string modID, std::string name, std::string version,
    std::string status, std::string createdAt,
    CCNode* modPopup, VersionsPopup* versionsPopup
) {
    if (!CCNode::init()) return false;

    m_modID = std::move(modID);
    m_version = std::move(version);
    m_modPopup = modPopup;
    m_versionsPopup = versionsPopup;

    // One simple row matching the scroll layer width.
    setContentSize({260.f, 62.f});

    auto title = CCLabelBMFont::create(
        fmt::format("{}{}", name, versionStatusText(status)).c_str(), "bigFont.fnt"
    );
    title->setScale(.34f);
    title->setAnchorPoint({0.f, .5f});
    addChildAtPosition(title, Anchor::Left, ccp(6.f, 45.f));

    auto versionLabel = CCLabelBMFont::create(
        fmt::format("Version {}", m_version).c_str(), "goldFont.fnt"
    );
    versionLabel->setScale(.28f);
    versionLabel->setAnchorPoint({0.f, .5f});
    addChildAtPosition(versionLabel, Anchor::Left, ccp(6.f, 28.f));

    auto dateLabel = CCLabelBMFont::create(
        fmt::format("Released {}", createdAt).c_str(), "goldFont.fnt"
    );
    dateLabel->setScale(.25f);
    dateLabel->setAnchorPoint({0.f, .5f});
    addChildAtPosition(dateLabel, Anchor::Left, ccp(6.f, 12.f));

    auto installed = getInstalledModSource(m_modID);
    bool isInstalled = installed && installed->version == m_version;

    auto buttonSprite = ButtonSprite::create(
        isInstalled ? "Installed" : "Download",
        "bigFont.fnt", getButtonTexture("GJ_button_01.png"), .65f
    );
    buttonSprite->setScale(.48f);

    auto button = CCMenuItemSpriteExtra::create(
        buttonSprite, this, menu_selector(VersionContainer::downloadVersion)
    );
    if (isInstalled) button->setEnabled(false);

    auto menu = CCMenu::create();
    menu->addChild(button);
    menu->setContentSize({76.f, 45.f});
    addChildAtPosition(menu, Anchor::Right, ccp(-2.f, 0.f));

    return true;
}

void VersionContainer::downloadVersion(CCObject*) {
    if (!m_modPopup) return;

    auto install = m_modPopup->getChildByIDRecursive("install-button");
    auto action = typeinfo_cast<CCMenuItem*>(install);
    if (!action) return;

    setPendingVersionInstall(m_modID, m_version);
    action->activate();
    if (m_versionsPopup) m_versionsPopup->closeVersionsPopup();
}

} // namespace

void showVersionsPopup(std::string const& modID, cocos2d::CCNode* modPopup) {
    if (modID.empty() || !modPopup) return;
    VersionsPopup::create(modID, modPopup)->show();
}

} // namespace opengeode
