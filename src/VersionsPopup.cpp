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

class VersionsPopup;

class VersionContainer : public CCNode {
    std::string m_modID;
    std::string m_version;
    CCNode* m_modPopup = nullptr;
    VersionsPopup* m_versionsPopup = nullptr;

    void installVersion(CCObject*);

public:
    static VersionContainer* create(
        std::string modID, std::string version,
        CCNode* modPopup, VersionsPopup* versionsPopup
    ) {
        auto ret = new VersionContainer();
        if (ret && ret->init(std::move(modID), std::move(version), modPopup, versionsPopup)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    bool init(
        std::string modID, std::string version,
        CCNode* modPopup, VersionsPopup* versionsPopup
    );
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

        // Follow the same scroll/background construction used by Mod Profiles.
        const float widthCS = 290.f;
        const float heightCS = 310.f;
        auto scrollSize = CCSize{widthCS - 17.5f, heightCS - 120.f};

        auto scrollBG = CCScale9Sprite::create(getSectionBackground());
        scrollBG->setContentSize(scrollSize);
        scrollBG->setAnchorPoint({0.5f, 0.5f});
        scrollBG->ignoreAnchorPointForPosition(false);
        scrollBG->setPosition({widthCS / 2.f, (heightCS / 2.f) - 15.f});
        scrollBG->setColor({0, 0, 0});
        scrollBG->setOpacity(100);
        m_mainLayer->addChild(scrollBG);

        auto scrollLayerLayout = ColumnLayout::create()
            ->setAxisAlignment(AxisAlignment::Start)
            ->setAutoGrowAxis(scrollSize.height - 12.5f)
            ->setGrowCrossAxis(false)
            ->setGap(5.f);

        auto scrollLayerSize = CCSize{scrollSize.width - 12.5f, scrollSize.height - 12.5f};
        auto scroll = ScrollLayer::create(scrollLayerSize);
        scroll->setAnchorPoint({0.f, 0.f});
        scroll->ignoreAnchorPointForPosition(true);
        scroll->setPosition({
            scrollBG->getPositionX() - scrollLayerSize.width / 2.f,
            scrollBG->getPositionY() - scrollLayerSize.height / 2.f
        });
        scroll->m_contentLayer->setLayout(scrollLayerLayout);
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
                        version["version"].asString().unwrapOr("unknown"),
                        modPopup,
                        this
                    );
                    if (row) scroll->m_contentLayer->addChild(row);
                }

                scroll->m_contentLayer->updateLayout(true);
                scroll->scrollToTop();
            }
        );

        return true;
    }
};

bool VersionContainer::init(
    std::string modID, std::string version,
    CCNode* modPopup, VersionsPopup* versionsPopup
) {
    if (!CCNode::init()) return false;

    m_modID = std::move(modID);
    m_version = std::move(version);
    m_modPopup = modPopup;
    m_versionsPopup = versionsPopup;

    // Same simple row model as Mod Profiles: content width + a fixed row height.
    auto width = 260.f - 12.5f;
    setContentSize({width, 40.f});

    auto versionLabel = CCLabelBMFont::create(
        fmt::format("Version {}", m_version).c_str(), "bigFont.fnt"
    );
    versionLabel->setScale(.42f);
    versionLabel->setAnchorPoint({0.f, .5f});
    versionLabel->setPosition({5.f, getContentSize().height / 2.f});
    addChild(versionLabel);

    auto installed = getInstalledModSource(m_modID);
    bool isInstalled = installed && installed->version == m_version;

    auto buttonSprite = ButtonSprite::create(
        isInstalled ? "Installed" : "Install",
        "bigFont.fnt",
        getButtonTexture("GJ_button_01.png"),
        .4f
    );
    buttonSprite->setScale(.8f);

    auto button = CCMenuItemSpriteExtra::create(
        buttonSprite, this, menu_selector(VersionContainer::installVersion)
    );
    if (isInstalled) button->setEnabled(false);

    auto itemMenu = CCMenu::create();
    itemMenu->setPosition({getContentSize().width - 45.f, getContentSize().height / 2.f});
    itemMenu->addChild(button);
    addChild(itemMenu);

    return true;
}

void VersionContainer::installVersion(CCObject*) {
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
