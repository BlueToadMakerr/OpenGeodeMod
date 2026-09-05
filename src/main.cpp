#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/async.hpp>
#include <alphalaneous.alphas_geode_utils/include/ObjectModify.hpp>

using namespace geode::prelude;

std::string getIndexUrl() {
    return Mod::get()->getSavedValue<std::string>("custom-index-url", "https://api.geode-sdk.org");
}

void setIndexUrl(std::string url) {
    if (!url.empty() && url.back() == '/') url.pop_back();
    Mod::get()->setSavedValue<std::string>("custom-index-url", url);
}

// Geode v5 non-templated Popup
class IndexSwitchPopup : public Popup {
protected:
    TextInput* m_input = nullptr;
    CCLabelBMFont* m_statsLabel = nullptr;
    async::TaskHolder<web::WebResponse> m_listener;

    bool init(std::string const& currentUrl) {
        if (!Popup::init(320.f, 210.f)) return false;

        this->setTitle("Index Selector");

        m_input = TextInput::create(260.f, "https://api.geode-sdk.org", "chatFont.fnt");
        m_input->setString(currentUrl);
        m_input->setPosition({m_mainLayer->getContentWidth() / 2, m_mainLayer->getContentHeight() / 2 + 25.f});
        m_mainLayer->addChild(m_input);

        m_statsLabel = CCLabelBMFont::create("Fetching index stats...", "bigFont.fnt");
        m_statsLabel->setScale(0.35f);
        m_statsLabel->setPosition({m_mainLayer->getContentWidth() / 2, m_mainLayer->getContentHeight() / 2 - 15.f});
        m_mainLayer->addChild(m_statsLabel);

        auto presetBtn = CCMenuItemExt::createSpriteExtra(
            ButtonSprite::create("Default API", "goldFont.fnt", "GJ_button_01.png", 0.6f),
            [this](auto) {
                std::string defaultUrl = "https://api.geode-sdk.org";
                m_input->setString(defaultUrl);
                this->fetchStats(defaultUrl);
            }
        );

        auto saveBtn = CCMenuItemExt::createSpriteExtra(
            ButtonSprite::create("Apply", "goldFont.fnt", "GJ_button_02.png", 0.6f),
            [this](auto) {
                setIndexUrl(m_input->getString());
                FLAlertLayer::create("Success", "Index URL updated successfully!", "OK")->show();
                this->onClose(nullptr);
            }
        );

        auto actionMenu = CCMenu::create();
        actionMenu->addChild(presetBtn);
        actionMenu->addChild(saveBtn);
        actionMenu->alignItemsHorizontallyWithPadding(12.f);
        actionMenu->setPosition({m_mainLayer->getContentWidth() / 2, 35.f});
        m_mainLayer->addChild(actionMenu);

        fetchStats(currentUrl);
        return true;
    }

    void fetchStats(std::string url) {
        m_statsLabel->setString("Loading info...");
        if (!url.empty() && url.back() == '/') url.pop_back();

        m_listener.spawn(
            web::WebRequest().get(url + "/v1/stats"),
            [this](web::WebResponse res) {
                if (res.ok()) {
                    auto json = res.json().unwrapOr(matjson::Value());
                    if (json.contains("payload")) {
                        auto payload = json["payload"];
                        int totalMods = payload["total_mod_count"].asInt().unwrapOr(0);
                        int totalDownloads = payload["total_mod_downloads"].asInt().unwrapOr(0);

                        m_statsLabel->setString(fmt::format("Mods: {} | Downloads: {}", totalMods, totalDownloads).c_str());
                        return;
                    }
                }
                m_statsLabel->setString("Could not retrieve info from endpoint.");
            }
        );
    }

public:
    static IndexSwitchPopup* create(std::string const& currentUrl) {
        auto ret = new IndexSwitchPopup();
        if (ret && ret->init(currentUrl)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

// Using alphas_geode_utils to hook the internal ModsLayer
class $nodeModify(IndexSwitcherModsLayer, ModsLayer) {
    void modify() {
        if (CCMenu* actionsMenu = typeinfo_cast<CCMenu*>(getChildByID("actions-menu"))) {
            // Prevent duplicate buttons if the layer is reloaded
            if (!actionsMenu->getChildByID("index-switcher-button"_spr)) {
                auto indexBtn = CCMenuItemExt::createSpriteExtra(
                    CircleButtonSprite::createWithSpriteFrameName(
                        "geode.loader/geode-logo.png", 
                        0.85f, 
                        CircleBaseColor::Blue
                    ),
                    [](auto) {
                        IndexSwitchPopup::create(getIndexUrl())->show();
                    }
                );
                indexBtn->setScale(0.8f);
                indexBtn->setID("index-switcher-button"_spr);

                actionsMenu->addChild(indexBtn);
                actionsMenu->updateLayout();
            }
        }
    }
};

$on_mod(Loaded) {
    web::WebRequestInterceptEvent().listen(
        [](std::string_view id, web::WebRequest& req) {
            std::string givenUrl = req.getUrl().data();
            std::string targetIndex = getIndexUrl();

            if (string::contains(givenUrl, "api.geode-sdk.org")) {
                givenUrl = string::replace(givenUrl, "https://api.geode-sdk.org", targetIndex);
                req.url(givenUrl);
            }

            return ListenerResult::Propagate;
        }, Priority::Stub
    ).leak();
}