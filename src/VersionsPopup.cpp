#include "VersionsPopup.hpp"
#include "InstalledMods.hpp"
#include "PopupSectionUtils.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/utils/web.hpp>

#include <filesystem>

using namespace geode::prelude;

namespace opengeode {
namespace {

std::string versionStatusText(std::string const& status) {
    if (status == "accepted") return "";
    return fmt::format("Status: {}", status);
}

class VersionRow : public CCNode {
    std::string m_modID;
    std::string m_version;
    async::TaskHolder<web::WebResponse> m_downloadTask;
    CCMenuItemSpriteExtra* m_installButton = nullptr;

    void finishDownload(web::WebResponse response) {
        if (!response.ok()) {
            m_installButton->setEnabled(true);
            FLAlertLayer::create("Versions", fmt::format("Download failed: {}", response.errorMessage()), "OK")->show();
            return;
        }

        auto modsDir = dirs::getGameDir() / "geode" / "mods";
        std::error_code ec;
        std::filesystem::create_directories(modsDir, ec);
        if (ec) {
            m_installButton->setEnabled(true);
            FLAlertLayer::create("Versions", "Could not create the Geode mods folder.", "OK")->show();
            return;
        }

        auto path = modsDir / fmt::format("{}.geode", m_modID);
        auto result = response.into(path);
        if (!result) {
            m_installButton->setEnabled(true);
            FLAlertLayer::create("Versions", fmt::format("Could not install the mod: {}", result.unwrapErr()), "OK")->show();
            return;
        }

        setInstalledModSource(m_modID, m_version);
        m_installButton->setEnabled(true);
        FLAlertLayer::create("Versions", fmt::format("Installed {} {}.", m_modID, m_version), "OK")->show();
    }

    void startDownload(CCObject*) {
        auto url = fmt::format("https://api.geode-sdk.org/v1/mods/{}/versions/{}/download", m_modID, m_version);
        m_installButton->setEnabled(false);
        m_downloadTask.spawn("OpenGeode version download", web::WebRequest().get(url), [this](web::WebResponse response) {
            this->finishDownload(std::move(response));
        });
    }

public:
    static VersionRow* create(
        std::string modID,
        std::string name,
        std::string version,
        int downloadCount,
        std::string status,
        std::string createdAt
    ) {
        auto ret = new VersionRow();
        if (ret && ret->init(std::move(modID), std::move(name), std::move(version), downloadCount, std::move(status), std::move(createdAt))) {
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
        std::string createdAt
    ) {
        if (!CCNode::init()) return false;
        m_modID = std::move(modID);
        m_version = std::move(version);
        this->setContentSize({245.f, 62.f});

        auto title = CCLabelBMFont::create(fmt::format("{} {}", name, m_version).c_str(), "bigFont.fnt");
        title->setScale(.38f);
        title->setAnchorPoint({0.f, .5f});
        this->addChildAtPosition(title, Anchor::Left, ccp(4.f, 20.f));

        auto details = CCLabelBMFont::create(
            fmt::format("Downloads: {}  Released: {}{}", downloadCount, createdAt,
                versionStatusText(status).empty() ? "" : fmt::format("  {}", versionStatusText(status))).c_str(),
            "goldFont.fnt"
        );
        details->setScale(.28f);
        details->setAnchorPoint({0.f, .5f});
        this->addChildAtPosition(details, Anchor::Left, ccp(4.f, -3.f));

        auto installSprite = ButtonSprite::create("Install", "bigFont.fnt", getButtonTexture("GJ_button_01.png"), .65f);
        installSprite->setScale(.55f);
        m_installButton = CCMenuItemSpriteExtra::create(installSprite, this, menu_selector(VersionRow::startDownload));
        auto menu = CCMenu::create();
        menu->addChild(m_installButton);
        menu->setContentSize({62.f, 45.f});
        this->addChildAtPosition(menu, Anchor::Right, ccp(-2.f, 0.f));

        return true;
    }
};

class VersionsPopup : public Popup {
    async::TaskHolder<web::WebResponse> m_requestTask;

public:
    static VersionsPopup* create(std::string modID) {
        auto ret = new VersionsPopup();
        if (ret && ret->init(std::move(modID))) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    bool init(std::string modID) {
        if (!Popup::init(290.f, 310.f, getPopupBackground())) return false;
        setTitle("Versions");
        if (auto close = createGeodeCloseButton()) setCloseButtonSpr(close, .8f);

        auto scroll = ScrollLayer::create({260.f, 245.f});
        scroll->m_contentLayer->setLayout(ScrollLayer::createDefaultListLayout(3.f));
        m_mainLayer->addChildAtPosition(scroll, Anchor::Center, ccp(0.f, -3.f));

        auto task = web::WebRequest().get(fmt::format("https://api.geode-sdk.org/v1/mods/{}", modID));
        m_requestTask.spawn("OpenGeode version list", task, [scroll, modID](web::WebResponse response) {
            if (!response.ok()) return;
            auto json = response.json();
            if (!json) return;
            auto payload = (*json)["payload"];
            if (!payload.isObject()) return;
            auto versions = payload["versions"];
            if (!versions.isArray()) return;

            for (auto const& version : versions) {
                if (!version.isObject()) continue;
                auto row = VersionRow::create(
                    modID,
                    version["name"].asString().unwrapOr(modID),
                    version["version"].asString().unwrapOr("unknown"),
                    version["download_count"].asInt().unwrapOr(0),
                    version["status"].asString().unwrapOr("accepted"),
                    version["created_at"].asString().unwrapOr("unknown")
                );
                if (row) scroll->m_contentLayer->addChild(row);
            }
            scroll->m_contentLayer->updateLayout();
        });
        return true;
    }
};

} // namespace

void showVersionsPopup(std::string const& modID) {
    if (modID.empty()) return;
    VersionsPopup::create(modID)->show();
}

} // namespace opengeode
