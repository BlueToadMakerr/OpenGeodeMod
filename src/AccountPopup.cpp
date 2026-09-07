#include "AccountPopup.hpp"
#include "Settings.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/ui/mods/list/ModItem.hpp>

#include <algorithm>
#include <cctype>

using namespace geode::prelude;

namespace opengeode {

namespace {

std::string trimSlash(std::string url) {
    while (!url.empty() && url.back() == '/') url.pop_back();
    return url;
}

std::string errorText(web::WebResponse const& response) {
    if (auto json = response.json()) {
        if ((*json).contains("detail")) return (*json)["detail"].asString().unwrapOr("Request failed");
        if ((*json).contains("error")) return (*json)["error"].asString().unwrapOr("Request failed");
    }
    return response.errorMessage().empty() ? fmt::format("HTTP {}", response.code()) : std::string(response.errorMessage());
}

std::string makeJsonString(std::string const& value) {
    std::string out = "\"";
    for (auto c : value) {
        if (c == '\\' || c == '"') out += '\\';
        out += c;
    }
    out += '"';
    return out;
}

class GdLoginPopup : public Popup {
protected:
    TextInput* m_code = nullptr;
    CCLabelBMFont* m_status = nullptr;
    std::function<void()> m_onLoggedIn;
    async::TaskHolder<web::WebResponse> m_task;

    bool init(std::function<void()> onLoggedIn) {
        if (!Popup::init(300.f, 190.f, getPopupBackground())) return false;
        m_onLoggedIn = std::move(onLoggedIn);
        this->setTitle("OpenGeode Login");
        if (auto close = createGeodeCloseButton()) this->setCloseButtonSpr(close, 0.875f);
        auto center = m_mainLayer->getContentWidth() / 2;
        auto label = CCLabelBMFont::create("Enter the 4-character code from the OpenGeode website", "chatFont.fnt");
        label->setScale(0.45f); label->setDimensions(250.f, 0.f); label->setAlignment(kCCTextAlignmentCenter); label->setPosition({center, 120.f}); m_mainLayer->addChild(label);
        m_code = TextInput::create(140.f, "AB12", "chatFont.fnt"); m_code->setPosition({center, 80.f}); m_mainLayer->addChild(m_code);
        m_status = CCLabelBMFont::create("", "chatFont.fnt"); m_status->setScale(0.42f); m_status->setPosition({center, 50.f}); m_mainLayer->addChild(m_status);
        auto login = CCMenuItemExt::createSpriteExtra(ButtonSprite::create("Log In", "goldFont.fnt", getButtonTexture("GJ_button_01.png"), 0.65f), [this](auto) { this->submit(); });
        auto menu = CCMenu::create(); menu->addChild(login); menu->setPosition({center, 22.f}); m_mainLayer->addChild(menu);
        return true;
    }

    void submit() {
        auto code = m_code->getString();
        if (code.size() != 4) { m_status->setString("Enter exactly 4 characters."); return; }
        for (auto& c : code) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        auto req = web::WebRequest(); req.header("Content-Type", "application/json"); req.body(fmt::format("{{\"code\":{}}}", makeJsonString(code)));
        m_status->setString("Logging in...");
        m_task.spawn(req.post(trimSlash(getIndexUrl()) + "/OpenGeode/login-code/login"), [this](web::WebResponse res) {
            if (!res.ok()) { m_status->setString(errorText(res).c_str()); return; }
            auto json = res.json().unwrapOr(matjson::Value()); auto payload = json["payload"];
            auto access = payload["access_token"].asString().unwrapOr(""); auto refresh = payload["refresh_token"].asString().unwrapOr("");
            if (access.empty() || refresh.empty()) { m_status->setString("The server returned invalid login tokens."); return; }
            setAuthTokens(access, refresh); if (m_onLoggedIn) m_onLoggedIn(); this->onClose(nullptr);
        });
    }

public:
    static GdLoginPopup* create(std::function<void()> onLoggedIn) {
        auto ret = new GdLoginPopup(); if (ret && ret->init(std::move(onLoggedIn))) { ret->autorelease(); return ret; } delete ret; return nullptr;
    }
};

class AccountPopup : public Popup {
protected:
    CCLabelBMFont* m_name = nullptr;
    CCLabelBMFont* m_badges = nullptr;
    TextInput* m_displayName = nullptr;
    CCLabelBMFont* m_status = nullptr;
    ScrollLayer* m_modScroll = nullptr;
    async::TaskHolder<web::WebResponse> m_requestTask;
    async::TaskHolder<web::WebResponse> m_refreshTask;
    bool m_refreshing = false;

    bool init() {
        if (!Popup::init(430.f, 330.f, getPopupBackground())) return false;
        this->setTitle("OpenGeode Account"); if (auto close = createGeodeCloseButton()) this->setCloseButtonSpr(close, 0.875f);
        auto center = m_mainLayer->getContentWidth() / 2;
        m_name = CCLabelBMFont::create("Loading...", "bigFont.fnt"); m_name->setScale(0.55f); m_name->setPosition({center, 280.f}); m_mainLayer->addChild(m_name);
        m_badges = CCLabelBMFont::create("", "goldFont.fnt"); m_badges->setScale(0.42f); m_badges->setPosition({center, 258.f}); m_mainLayer->addChild(m_badges);
        auto displayLabel = CCLabelBMFont::create("Display Name", "goldFont.fnt"); displayLabel->setScale(0.42f); displayLabel->setAnchorPoint({0.f, 0.5f}); displayLabel->setPosition({25.f, 230.f}); m_mainLayer->addChild(displayLabel);
        m_displayName = TextInput::create(220.f, "Display Name", "chatFont.fnt"); m_displayName->setPosition({145.f, 207.f}); m_mainLayer->addChild(m_displayName);
        auto save = CCMenuItemExt::createSpriteExtra(ButtonSprite::create("Save", "goldFont.fnt", getButtonTexture("GJ_button_01.png"), 0.5f), [this](auto) { saveProfile(); });
        auto logout = CCMenuItemExt::createSpriteExtra(ButtonSprite::create("Logout", "goldFont.fnt", getButtonTexture("GJ_button_02.png"), 0.5f), [this](auto) { clearAuthTokens(); this->onClose(nullptr); });
        auto buttons = CCMenu::create(); buttons->addChild(save); buttons->addChild(logout); buttons->setLayout(RowLayout::create()->setGap(8.f)); buttons->setPosition({center, 175.f}); buttons->updateLayout(); m_mainLayer->addChild(buttons);
        m_status = CCLabelBMFont::create("Loading profile...", "chatFont.fnt"); m_status->setScale(0.38f); m_status->setPosition({center, 150.f}); m_mainLayer->addChild(m_status);
        auto modsLabel = CCLabelBMFont::create("Published Mods", "goldFont.fnt"); modsLabel->setScale(0.45f); modsLabel->setPosition({center, 128.f}); m_mainLayer->addChild(modsLabel);
        m_modScroll = ScrollLayer::create({380.f, 105.f}); m_modScroll->setPosition({25.f, 22.f}); m_mainLayer->addChild(m_modScroll);
        loadProfile(); return true;
    }

    void request(std::string method, std::string path, std::string body, std::function<void(web::WebResponse)> callback, bool allowRefresh = true) {
        auto token = getAuthAccessToken(); if (token.empty()) { clearAuthTokens(); this->onClose(nullptr); return; }
        auto req = web::WebRequest(); req.header("Authorization", "Bearer " + token);
        if (!body.empty()) { req.header("Content-Type", "application/json"); req.body(body); }
        m_requestTask.spawn(req.send(method, trimSlash(getIndexUrl()) + path), [this, method, path, body, callback = std::move(callback), allowRefresh](web::WebResponse res) mutable {
            if (res.code() == 401 && allowRefresh && !m_refreshing && !getAuthRefreshToken().empty()) { refreshAndRetry(method, path, body, std::move(callback)); return; }
            callback(std::move(res));
        });
    }

    void refreshAndRetry(std::string method, std::string path, std::string body, std::function<void(web::WebResponse)> callback) {
        m_refreshing = true; auto req = web::WebRequest(); req.header("Content-Type", "application/json"); req.body(fmt::format("{{\"refresh_token\":{}}}", makeJsonString(getAuthRefreshToken())));
        m_refreshTask.spawn(req.post(trimSlash(getIndexUrl()) + "/v1/login/refresh"), [this, method, path, body, callback = std::move(callback)](web::WebResponse res) mutable {
            m_refreshing = false; if (!res.ok()) { clearAuthTokens(); m_status->setString("Session expired. Please log in again."); return; }
            auto json = res.json().unwrapOr(matjson::Value()); auto payload = json["payload"];
            auto access = payload["access_token"].asString().unwrapOr(""); auto refresh = payload["refresh_token"].asString().unwrapOr("");
            if (access.empty() || refresh.empty()) { clearAuthTokens(); m_status->setString("Session refresh failed."); return; }
            setAuthTokens(access, refresh); request(method, path, body, std::move(callback), false);
        });
    }

    void loadProfile() {
        request("GET", "/v1/me", "", [this](web::WebResponse res) {
            if (!res.ok()) { m_status->setString(errorText(res).c_str()); return; }
            auto json = res.json().unwrapOr(matjson::Value()); auto p = json["payload"];
            auto display = p["display_name"].asString().unwrapOr(""); auto username = p["username"].asString().unwrapOr("");
            m_name->setString((display.empty() ? username : display).c_str()); m_displayName->setString(display.c_str());
            std::string badges; if (p["verified"].asBool().unwrapOr(false)) badges += "Verified"; if (p["admin"].asBool().unwrapOr(false)) { if (!badges.empty()) badges += "  |  "; badges += "Admin"; }
            m_badges->setString(badges.c_str()); m_status->setString("Profile loaded."); loadMods();
        });
    }

    void saveProfile() {
        auto name = m_displayName->getString(); if (name.size() < 2 || name.size() > 64) { m_status->setString("Display name must be 2-64 characters."); return; }
        for (auto c : name) if (!std::isalnum(static_cast<unsigned char>(c)) || static_cast<unsigned char>(c) > 127) { m_status->setString("Display name must be ASCII letters/numbers."); return; }
        m_status->setString("Saving..."); request("PUT", "/v1/me", fmt::format("{{\"display_name\":{}}}", makeJsonString(name)), [this](web::WebResponse res) { if (!res.ok()) { m_status->setString(errorText(res).c_str()); return; } m_status->setString("Profile saved."); loadProfile(); });
    }

    void loadMods() {
        request("GET", "/v1/me/mods?status=accepted", "", [this](web::WebResponse res) {
            if (!res.ok()) { m_status->setString(errorText(res).c_str()); return; }
            auto json = res.json().unwrapOr(matjson::Value()); auto payload = json["payload"];
            m_modScroll->m_contentLayer->removeAllChildren(); float y = 95.f; size_t count = 0;
            if (payload.isArray()) for (auto const& mod : payload) {
                auto id = mod["id"].asInt().unwrapOr(0); if (id <= 0) continue;
                auto item = AnyModItem::create(fmt::format("{}", id)); if (!item) continue;
                item->updateDisplay(365.f, ModListDisplay::SmallList); item->setPosition({190.f, y}); m_modScroll->m_contentLayer->addChild(item); y -= item->getContentSize().height + 6.f; ++count;
            }
            if (!count) { auto empty = CCLabelBMFont::create("No published mods.", "chatFont.fnt"); empty->setScale(0.4f); empty->setPosition({182.f, 52.f}); m_modScroll->m_contentLayer->addChild(empty); y = 105.f; }
            m_modScroll->m_contentLayer->setContentSize({365.f, std::max(105.f, 105.f - y + 20.f)}); m_modScroll->scrollToTop();
        });
    }

public:
    static AccountPopup* create() { auto ret = new AccountPopup(); if (ret && ret->init()) { ret->autorelease(); return ret; } delete ret; return nullptr; }
};

} // namespace

void showAccountPopup() { if (!hasAuthTokens()) { showGdLoginPopup([] { showAccountPopup(); }); return; } AccountPopup::create()->show(); }
void showGdLoginPopup(std::function<void()> onLoggedIn) { GdLoginPopup::create(std::move(onLoggedIn))->show(); }

} // namespace opengeode
