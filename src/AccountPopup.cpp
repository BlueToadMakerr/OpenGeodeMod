#include "AccountPopup.hpp"
#include "Settings.hpp"
#include "PopupSectionUtils.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/MDTextArea.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/utils/web.hpp>

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <memory>
#include <string>

using namespace geode::prelude;

namespace opengeode {
namespace {
std::string trimSlash(std::string url) { while (!url.empty() && url.back() == '/') url.pop_back(); return url; }
std::string errorText(web::WebResponse const& response) {
    if (auto json = response.json()) {
        if ((*json).contains("detail")) return (*json)["detail"].asString().unwrapOr("Request failed");
        if ((*json).contains("error")) return (*json)["error"].asString().unwrapOr("Request failed");
    }
    return response.errorMessage().empty() ? fmt::format("HTTP {}", response.code()) : std::string(response.errorMessage());
}
std::string makeJsonString(std::string const& value) {
    std::string out = "\"";
    for (auto c : value) { if (c == '\\' || c == '"') out += '\\'; out += c; }
    out += '"'; return out;
}
ByteVector makeBody(std::string const& value) { return ByteVector(value.begin(), value.end()); }

class GdLoginPopup : public Popup {
protected:
    TextInput* m_code = nullptr;
    CCLabelBMFont* m_status = nullptr;
    std::function<void()> m_onLoggedIn;
    async::TaskHolder<web::WebResponse> m_task;

    bool init(std::function<void()> onLoggedIn) {
        if (!Popup::init(280.f, 175.f, getPopupBackground())) return false;
        m_onLoggedIn = std::move(onLoggedIn); setTitle("OpenGeode Login");
        if (auto close = createGeodeCloseButton()) setCloseButtonSpr(close, .875f);
        auto center = m_mainLayer->getContentWidth() / 2;
        auto label = CCLabelBMFont::create("Enter the 4-character code from the website", "chatFont.fnt"); label->setScale(.8f); label->setAlignment(kCCTextAlignmentCenter); label->setPosition({center,120.f}); m_mainLayer->addChild(label);
        m_code = TextInput::create(130.f, "AB12", "chatFont.fnt"); m_code->setPosition({center,73.f}); m_mainLayer->addChild(m_code);
        m_status = CCLabelBMFont::create("", "chatFont.fnt"); m_status->setScale(.5f); m_status->setPosition({center,48.f}); m_mainLayer->addChild(m_status);
        auto login = CCMenuItemExt::createSpriteExtra(ButtonSprite::create("Log In", "goldFont.fnt", getButtonTexture("GJ_button_01.png"), .6f), [this](auto){ submit(); });
        auto menu = CCMenu::create(); menu->addChild(login); menu->setPosition({center,20.f}); m_mainLayer->addChild(menu); return true;
    }

    void submit() {
        std::string code = m_code->getString().c_str();
        if (code.size() != 4) { m_status->setString("Enter exactly 4 characters."); return; }
        for (auto& c : code) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        auto req = web::WebRequest(); req.header("Content-Type", "application/json"); req.body(makeBody(fmt::format("{{\"code\":{}}}", makeJsonString(code)))); m_status->setString("Logging in...");
        m_task.spawn(req.post(trimSlash(getIndexUrl()) + "/OpenGeode/login-code/login"), [this](web::WebResponse res) {
            if (!res.ok()) { m_status->setString(errorText(res).c_str()); return; }
            auto payload = res.json().unwrapOr(matjson::Value())["payload"];
            auto access = payload["access_token"].asString().unwrapOr(""); auto refresh = payload["refresh_token"].asString().unwrapOr("");
            if (access.empty() || refresh.empty()) { m_status->setString("The server returned invalid login tokens."); return; }
            setAuthTokens(access, refresh); if (m_onLoggedIn) m_onLoggedIn(); onClose(nullptr);
        });
    }

public:
    static GdLoginPopup* create(std::function<void()> onLoggedIn) { auto ret = new GdLoginPopup(); if (ret && ret->init(std::move(onLoggedIn))) { ret->autorelease(); return ret; } delete ret; return nullptr; }
};

class MyModsPopup : public Popup {
protected:
    async::TaskHolder<web::WebResponse> m_requestTask;
    async::TaskHolder<web::WebResponse> m_refreshTask;
    bool m_refreshing = false;
    MDTextArea* m_modArea = nullptr;

    struct VersionInfo { std::string name; std::string status; std::string reason; };
    struct ModInfo { std::string id; std::map<std::string, VersionInfo> versions; };

    bool init() {
        if (!Popup::init(370.f, 285.f, getPopupBackground())) return false;
        setTitle("My Mods");
        if (auto close = createGeodeCloseButton()) setCloseButtonSpr(close, .8f);
        m_modArea = MDTextArea::create("Loading mods...", {330.f, 210.f});
        if (!m_modArea) return false;
        m_modArea->setAnchorPoint({0.f, 1.f});
        m_modArea->setPosition({20.f, 245.f});
        m_mainLayer->addChild(m_modArea);
        loadMods();
        return true;
    }

    void request(std::string method, std::string path, std::string body, std::function<void(web::WebResponse)> callback, bool allowRefresh = true) {
        auto token = getAuthAccessToken();
        if (token.empty()) { clearAuthTokens(); onClose(nullptr); return; }
        auto req = web::WebRequest(); req.header("Authorization", "Bearer " + token);
        if (!body.empty()) { req.header("Content-Type", "application/json"); req.body(makeBody(body)); }
        m_requestTask.spawn(req.send(method, trimSlash(getIndexUrl()) + path), [this, method, path, body, callback = std::move(callback), allowRefresh](web::WebResponse res) mutable {
            if (res.code() == 401 && allowRefresh && !m_refreshing && !getAuthRefreshToken().empty()) { refreshAndRetry(method, path, body, std::move(callback)); return; }
            callback(std::move(res));
        });
    }

    void refreshAndRetry(std::string method, std::string path, std::string body, std::function<void(web::WebResponse)> callback) {
        m_refreshing = true;
        auto req = web::WebRequest(); req.header("Content-Type", "application/json"); req.body(makeBody(fmt::format("{{\"refresh_token\":{}}}", makeJsonString(getAuthRefreshToken()))));
        m_refreshTask.spawn(req.post(trimSlash(getIndexUrl()) + "/v1/login/refresh"), [this, method, path, body, callback = std::move(callback)](web::WebResponse res) mutable {
            m_refreshing = false;
            if (!res.ok()) { clearAuthTokens(); onClose(nullptr); return; }
            auto payload = res.json().unwrapOr(matjson::Value())["payload"];
            auto access = payload["access_token"].asString().unwrapOr(""); auto refresh = payload["refresh_token"].asString().unwrapOr("");
            if (access.empty() || refresh.empty()) { clearAuthTokens(); onClose(nullptr); return; }
            setAuthTokens(access, refresh); request(method, path, body, std::move(callback), false);
        });
    }

    void loadMods() {
        auto mods = std::make_shared<std::map<std::string, ModInfo>>();
        auto step = std::make_shared<std::function<void(int)>>();
        *step = [this, mods, step](int index) {
            static const char* statuses[] = {"accepted", "pending", "rejected"};
            if (index >= 3) {
                std::string text;
                for (auto const& [id, mod] : *mods) {
                    text += fmt::format("<mod:{}>\n", id);
                    for (auto const& [version, info] : mod.versions) {
                        text += fmt::format("{} | v{} | {}\n", info.name.empty() ? id : info.name, version, info.status);
                        if (info.status == "rejected" && !info.reason.empty()) text += fmt::format("with the reason: {}\n", info.reason);
                    }
                    text += "\n";
                }
                if (text.empty()) text = "No submitted mods found..";
                m_modArea->setString(text.c_str());
                return;
            }

            request("GET", fmt::format("/v1/me/mods?status={}", statuses[index]), "", [this, mods, step, index](web::WebResponse res) {
                if (!res.ok()) { m_modArea->setString(errorText(res).c_str()); return; }
                auto payload = res.json().unwrapOr(matjson::Value())["payload"];
                if (payload.isArray()) for (auto const& mod : payload) {
                    auto id = mod["id"].asString().unwrapOr("");
                    if (id.empty()) continue;
                    auto& entry = (*mods)[id]; entry.id = id;
                    auto versions = mod["versions"];
                    if (versions.isArray()) for (auto const& version : versions) {
                        auto versionID = version["version"].asString().unwrapOr("");
                        if (versionID.empty()) versionID = fmt::format("unknown-{}", entry.versions.size());
                        auto& info = entry.versions[versionID];
                        info.name = version["name"].asString().unwrapOr(id);
                        info.status = version["status"].asString().unwrapOr(statuses[index]);
                        info.reason = version["info"].asString().unwrapOr("");
                    }
                }
                (*step)(index + 1);
            });
        };
        (*step)(0);
    }

public:
    static MyModsPopup* create() { auto ret = new MyModsPopup(); if (ret && ret->init()) { ret->autorelease(); return ret; } delete ret; return nullptr; }
};

class AccountPopup : public Popup {
protected:
    CCLabelBMFont* m_name = nullptr;
    CCLabelBMFont* m_id = nullptr;
    CCLabelBMFont* m_badges = nullptr;
    TextInput* m_displayName = nullptr;
    TextInput* m_modUrl = nullptr;
    CCLabelBMFont* m_status = nullptr;
    CCLabelBMFont* m_modStatus = nullptr;
    async::TaskHolder<web::WebResponse> m_requestTask;
    async::TaskHolder<web::WebResponse> m_refreshTask;
    bool m_refreshing = false;

    bool init() {
        if (!Popup::init(370.f, 300.f, getPopupBackground())) return false;
        setTitle("OpenGeode Account");
        if (auto close = createGeodeCloseButton()) setCloseButtonSpr(close, .8f);
        auto center = m_mainLayer->getContentWidth() / 2;

        m_name = CCLabelBMFont::create("Loading...", "bigFont.fnt");
        m_name->setScale(.48f); m_name->setAlignment(kCCTextAlignmentCenter); m_name->setAnchorPoint({.5f, .5f}); m_name->setPosition({center, 256.f}); m_mainLayer->addChild(m_name);
        m_id = CCLabelBMFont::create("Account ID: -", "chatFont.fnt");
        m_id->setScale(.36f); m_id->setAlignment(kCCTextAlignmentCenter); m_id->setAnchorPoint({.5f, .5f}); m_id->setPosition({center, 237.f}); m_mainLayer->addChild(m_id);
        m_badges = CCLabelBMFont::create("", "goldFont.fnt");
        m_badges->setScale(.36f); m_badges->setAlignment(kCCTextAlignmentCenter); m_badges->setAnchorPoint({.5f, .5f}); m_badges->setPosition({center, 220.f}); m_mainLayer->addChild(m_badges);

        auto displayLabel = CCLabelBMFont::create("Display Name", "goldFont.fnt");
        displayLabel->setScale(.38f); displayLabel->setAlignment(kCCTextAlignmentCenter); displayLabel->setAnchorPoint({.5f, .5f}); displayLabel->setPosition({center, 199.f}); m_mainLayer->addChild(displayLabel);
        m_displayName = TextInput::create(190.f, "Display Name", "chatFont.fnt");
        m_displayName->setPosition({center, 174.f}); m_mainLayer->addChild(m_displayName);

        auto save = CCMenuItemExt::createSpriteExtra(ButtonSprite::create("Save", "goldFont.fnt", getButtonTexture("GJ_button_01.png"), .45f), [this](auto){ saveProfile(); });
        auto logout = CCMenuItemExt::createSpriteExtra(ButtonSprite::create("Logout", "goldFont.fnt", getButtonTexture("GJ_button_06.png"), .45f), [this](auto){ confirmLogout(); });
        auto mods = CCMenuItemExt::createSpriteExtra(ButtonSprite::create("My Mods", "goldFont.fnt", getButtonTexture("GJ_button_01.png"), .45f), [](auto){ MyModsPopup::create()->show(); });
        auto buttons = CCMenu::create(); buttons->addChild(save); buttons->addChild(mods); buttons->addChild(logout); buttons->setLayout(RowLayout::create()->setGap(6.f)); buttons->setPosition({center, 137.f}); buttons->updateLayout(); m_mainLayer->addChild(buttons);

        auto submitLabel = CCLabelBMFont::create("Submit / Update Mod", "goldFont.fnt");
        submitLabel->setScale(.38f); submitLabel->setAlignment(kCCTextAlignmentCenter); submitLabel->setAnchorPoint({.5f, .5f}); submitLabel->setPosition({center, 111.f}); m_mainLayer->addChild(submitLabel);
        m_modUrl = TextInput::create(225.f, "Mod .geode URL", "chatFont.fnt");
        m_modUrl->setPosition({center, 86.f}); m_mainLayer->addChild(m_modUrl);
        auto submit = CCMenuItemExt::createSpriteExtra(ButtonSprite::create("Submit", "goldFont.fnt", getButtonTexture("GJ_button_01.png"), .42f), [this](auto){ submitMod(); });
        auto submitMenu = CCMenu::create(); submitMenu->addChild(submit); submitMenu->setPosition({center, 45.f}); m_mainLayer->addChild(submitMenu);

        m_modStatus = CCLabelBMFont::create("", "chatFont.fnt");
        m_modStatus->setScale(.5f); m_modStatus->setAlignment(kCCTextAlignmentCenter); m_modStatus->setAnchorPoint({.5f, .5f}); m_modStatus->setPosition({center, 20.f}); m_mainLayer->addChild(m_modStatus);
        m_status = CCLabelBMFont::create("Loading profile...", "chatFont.fnt");
        m_status->setScale(.5f); m_status->setAlignment(kCCTextAlignmentCenter); m_status->setAnchorPoint({.5f, .5f}); m_status->setPosition({center, 10.f}); m_mainLayer->addChild(m_status);
        loadProfile(); return true;
    }

    void request(std::string method, std::string path, std::string body, std::function<void(web::WebResponse)> callback, bool allowRefresh = true) {
        auto token = getAuthAccessToken(); if (token.empty()) { clearAuthTokens(); onClose(nullptr); return; }
        auto req = web::WebRequest(); req.header("Authorization", "Bearer " + token);
        if (!body.empty()) { req.header("Content-Type", "application/json"); req.body(makeBody(body)); }
        m_requestTask.spawn(req.send(method, trimSlash(getIndexUrl()) + path), [this, method, path, body, callback = std::move(callback), allowRefresh](web::WebResponse res) mutable {
            if (res.code() == 401 && allowRefresh && !m_refreshing && !getAuthRefreshToken().empty()) { refreshAndRetry(method, path, body, std::move(callback)); return; }
            callback(std::move(res));
        });
    }

    void refreshAndRetry(std::string method, std::string path, std::string body, std::function<void(web::WebResponse)> callback) {
        m_refreshing = true; auto req = web::WebRequest(); req.header("Content-Type", "application/json"); req.body(makeBody(fmt::format("{{\"refresh_token\":{}}}", makeJsonString(getAuthRefreshToken()))));
        m_refreshTask.spawn(req.post(trimSlash(getIndexUrl()) + "/v1/login/refresh"), [this, method, path, body, callback = std::move(callback)](web::WebResponse res) mutable {
            m_refreshing = false;
            if (!res.ok()) { clearAuthTokens(); m_status->setString("Session expired. Please log in again."); return; }
            auto payload = res.json().unwrapOr(matjson::Value())["payload"];
            auto access = payload["access_token"].asString().unwrapOr(""); auto refresh = payload["refresh_token"].asString().unwrapOr("");
            if (access.empty() || refresh.empty()) { clearAuthTokens(); m_status->setString("Session refresh failed."); return; }
            setAuthTokens(access, refresh); request(method, path, body, std::move(callback), false);
        });
    }

    void loadProfile() {
        request("GET", "/v1/me", "", [this](web::WebResponse res) {
            if (!res.ok()) { m_status->setString(errorText(res).c_str()); return; }
            auto p = res.json().unwrapOr(matjson::Value())["payload"];
            auto display = p["display_name"].asString().unwrapOr(""); auto username = p["username"].asString().unwrapOr("");
            m_name->setString((display.empty() ? username : display).c_str());
            m_id->setString(fmt::format("Account ID: {}", p["id"].asInt().unwrapOr(0)).c_str());
            m_displayName->setString(display.c_str());
            std::string badges; if (p["verified"].asBool().unwrapOr(false)) badges += "Verified"; if (p["admin"].asBool().unwrapOr(false)) { if (!badges.empty()) badges += "  |  "; badges += "Admin"; }
            m_badges->setString(badges.c_str()); m_status->setString("Profile loaded.");
        });
    }

    void saveProfile() {
        std::string name = m_displayName->getString().c_str();
        if (name.size() < 2 || name.size() > 64) { m_status->setString("Display name must be 2-64 characters."); return; }
        for (auto c : name) if (!std::isalnum(static_cast<unsigned char>(c)) || static_cast<unsigned char>(c) > 127) { m_status->setString("Display name must be ASCII letters/numbers."); return; }
        m_status->setString("Saving..."); request("PUT", "/v1/me", fmt::format("{{\"display_name\":{}}}", makeJsonString(name)), [this](web::WebResponse res) { if (!res.ok()) { m_status->setString(errorText(res).c_str()); return; } m_status->setString("Profile saved."); loadProfile(); });
    }

    void submitMod() {
        std::string url = m_modUrl->getString().c_str();
        if (url.empty()) { m_modStatus->setString("Enter a .geode download URL."); return; }
        if (url.size() > 1024) { m_modStatus->setString("URL is too long."); return; }
        m_modStatus->setString("Submitting mod...");
        request("POST", "/v1/mods", fmt::format("{{\"download_link\":{}}}", makeJsonString(url)), [this](web::WebResponse res) {
            if (!res.ok()) { m_modStatus->setString(errorText(res).c_str()); return; }
            auto payload = res.json().unwrapOr(matjson::Value())["payload"];
            auto id = payload["id"].asString().unwrapOr("");
            auto versions = payload["versions"];
            std::string status;
            if (versions.isArray()) {
                for (auto const& version : versions) {
                    status = version["status"].asString().unwrapOr("");
                    break;
                }
            }
            if (status.empty()) status = "submitted";
            m_modStatus->setString(fmt::format("{} {} successfully.", id.empty() ? "Mod" : id, status).c_str());
            m_modUrl->setString("");
        });
    }

    void confirmLogout() {
        createQuickPopup(
            "Log Out",
            "Are you sure you want to log out of this OpenGeode account?",
            "Cancel",
            "Log Out",
            [this](FLAlertLayer*, bool confirmed) {
                if (confirmed) {
                    clearAuthTokens();
                    onClose(nullptr);
                }
            }
        );
    }

public:
    static AccountPopup* create() { auto ret = new AccountPopup(); if (ret && ret->init()) { ret->autorelease(); return ret; } delete ret; return nullptr; }
};
}

void showAccountPopup() { if (!hasAuthTokens()) { showGdLoginPopup([] { showAccountPopup(); }); return; } AccountPopup::create()->show(); }
void showGdLoginPopup(std::function<void()> onLoggedIn) { GdLoginPopup::create(std::move(onLoggedIn))->show(); }
}
