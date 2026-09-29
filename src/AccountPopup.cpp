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
#include <vector>

using namespace geode::prelude;

namespace opengeode {
namespace {
std::string trimSlash(std::string url) { while (!url.empty() && url.back() == '/') url.pop_back(); return url; }

std::string errorText(web::WebResponse const& response) {
    std::string detail;
    if (auto json = response.json()) {
        if ((*json).contains("error")) {
            auto value = (*json)["error"].asString().unwrapOr("");
            if (!value.empty()) detail = value;
        }
        if (detail.empty() && (*json).contains("detail")) {
            auto value = (*json)["detail"].asString().unwrapOr("");
            if (!value.empty()) detail = value;
        }
        if (detail.empty() && (*json).contains("message")) {
            auto value = (*json)["message"].asString().unwrapOr("");
            if (!value.empty()) detail = value;
        }
    }
    if (detail.empty() && !response.errorMessage().empty()) detail = std::string(response.errorMessage());
    if (detail.empty()) detail = response.code() > 0 ? fmt::format("HTTP {}", response.code()) : "Request failed.";
    if (response.code() > 0) return fmt::format("HTTP {}: {}", response.code(), detail);
    return detail;
}

std::string invalidCredentialsText(web::WebResponse const& response) {
    auto code = response.code();
    auto body = response.string().unwrapOr("");
    auto detail = errorText(response);
    if (!body.empty()) return fmt::format("HTTP {}\nServer response: {}", code, body);
    return detail;
}

void showAlert(std::string const& title, std::string const& message) {
    FLAlertLayer::create(title.c_str(), message.c_str(), "OK")->show();
}

std::string makeJsonString(std::string const& value) {
    std::string out = "\"";
    for (auto c : value) { if (c == '\\' || c == '"') out += '\\'; out += c; }
    out += '"'; return out;
}
ByteVector makeBody(std::string const& value) { return ByteVector(value.begin(), value.end()); }

class GithubLoginPopup : public Popup {
    CCLabelBMFont* m_codeLabel = nullptr;
    CCLabelBMFont* m_urlLabel = nullptr;
    CCLabelBMFont* m_statusLabel = nullptr;
    std::function<void()> m_onLoggedIn;
    async::TaskHolder<web::WebResponse> m_task;
    std::string m_uuid;
    int m_pollInterval = 5;
    bool m_finished = false;
    bool m_pollInFlight = false;

    bool init(std::function<void()> onLoggedIn) {
        if (!Popup::init(320.f, 190.f, getPopupBackground())) return false;
        m_onLoggedIn = std::move(onLoggedIn);
        setTitle("GitHub Login");
        if (auto close = createGeodeCloseButton()) setCloseButtonSpr(close, .875f);
        auto center = m_mainLayer->getContentWidth() / 2.f;
        auto instructions = CCLabelBMFont::create("Open GitHub and enter this code:", "chatFont.fnt");
        instructions->setScale(.65f); instructions->setPosition({center, 137.f}); m_mainLayer->addChild(instructions);
        m_codeLabel = CCLabelBMFont::create("Loading...", "bigFont.fnt");
        m_codeLabel->setScale(.7f); m_codeLabel->setPosition({center, 106.f}); m_mainLayer->addChild(m_codeLabel);
        m_urlLabel = CCLabelBMFont::create("https://github.com/login/device", "chatFont.fnt");
        m_urlLabel->setScale(.48f); m_urlLabel->setPosition({center, 78.f}); m_mainLayer->addChild(m_urlLabel);
        m_statusLabel = CCLabelBMFont::create("Starting login...", "chatFont.fnt");
        m_statusLabel->setScale(.55f); m_statusLabel->setPosition({center, 48.f}); m_mainLayer->addChild(m_statusLabel);
        startLogin();
        return true;
    }

    void fail(std::string const& message) {
        m_finished = true;
        unschedule(schedule_selector(GithubLoginPopup::poll));
        showAlert("Login Failed", message);
        onClose(nullptr);
    }

    void startLogin() {
        m_statusLabel->setString("Starting login...");
        auto req = web::WebRequest();
        req.header("Accept", "application/json");
        m_task.spawn(req.post(trimSlash(getIndexUrl()) + "/v1/login/github"), [this](web::WebResponse res) {
            if (!res.ok()) { fail(errorText(res)); return; }
            auto payload = res.json().unwrapOr(matjson::Value())["payload"];
            m_uuid = payload["uuid"].asString().unwrapOr("");
            auto uri = payload["uri"].asString().unwrapOr("https://github.com/login/device");
            auto code = payload["code"].asString().unwrapOr("");
            auto interval = payload["interval"].asInt().unwrapOr(5L);
            m_pollInterval = interval < 1L ? 1 : static_cast<int>(interval);
            if (m_uuid.empty() || code.empty()) { fail(invalidCredentialsText(res)); return; }
            m_codeLabel->setString(code.c_str());
            m_urlLabel->setString(uri.c_str());
            m_statusLabel->setString("Waiting for authorization...");
            schedulePoll();
        });
    }

    void schedulePoll() {
        if (m_finished || m_uuid.empty() || m_pollInFlight) return;
        this->scheduleOnce(schedule_selector(GithubLoginPopup::poll), static_cast<float>(m_pollInterval));
    }

    void poll(float) {
        if (m_finished || m_uuid.empty() || m_pollInFlight) return;
        m_pollInFlight = true;
        auto req = web::WebRequest();
        req.header("Content-Type", "application/json");
        req.body(makeBody(fmt::format("{{\"uuid\":{}}}", makeJsonString(m_uuid))));
        m_task.spawn(req.post(trimSlash(getIndexUrl()) + "/v1/login/github/poll"), [this](web::WebResponse res) {
            m_pollInFlight = false;
            if (res.ok()) {
                auto payload = res.json().unwrapOr(matjson::Value())["payload"];
                auto access = payload["access_token"].asString().unwrapOr("");
                auto refresh = payload["refresh_token"].asString().unwrapOr("");
                if (access.empty() || refresh.empty()) { fail(invalidCredentialsText(res)); return; }
                m_finished = true;
                unschedule(schedule_selector(GithubLoginPopup::poll));
                setAuthTokens(access, refresh);
                if (m_onLoggedIn) m_onLoggedIn();
                onClose(nullptr);
                return;
            }

            // This endpoint uses 401 for its server-specific "User auth pending" state.
            // Only 401 is ignored; every other error closes the login popup and is shown.
            if (res.code() == 401) {
                schedulePoll();
                return;
            }

            fail(errorText(res));
        });
    }

public:
    static GithubLoginPopup* create(std::function<void()> onLoggedIn) {
        auto ret = new GithubLoginPopup();
        if (ret && ret->init(std::move(onLoggedIn))) { ret->autorelease(); return ret; }
        delete ret;
        return nullptr;
    }
};

class MyModsPopup : public Popup {
    MDTextArea* m_modArea = nullptr; async::TaskHolder<web::WebResponse> m_requestTask; async::TaskHolder<web::WebResponse> m_refreshTask; bool m_refreshing = false;
    struct VersionInfo { std::string name, status, reason; }; struct ModInfo { std::string id; std::map<std::string, VersionInfo> versions; };
    bool init() { if (!Popup::init(370.f, 285.f, getPopupBackground())) return false; setTitle("My Mods"); if (auto close = createGeodeCloseButton()) setCloseButtonSpr(close, .8f); m_modArea = MDTextArea::create("Loading mods...", {330.f, 210.f}); if (!m_modArea) return false; m_modArea->setAnchorPoint({0.f, 1.f}); m_modArea->setPosition({20.f, 245.f}); m_mainLayer->addChild(m_modArea); loadMods(); return true; }
    void request(std::string method, std::string path, std::string body, std::function<void(web::WebResponse)> callback, bool allowRefresh = true) {
        auto token = getAuthAccessToken(); if (token.empty()) { clearAuthTokens(); onClose(nullptr); return; } auto req = web::WebRequest(); req.header("Authorization", "Bearer " + token); if (!body.empty()) { req.header("Content-Type", "application/json"); req.body(makeBody(body)); }
        m_requestTask.spawn(req.send(method, trimSlash(getIndexUrl()) + path), [this, method, path, body, callback = std::move(callback), allowRefresh](web::WebResponse res) mutable { if (res.code() == 401 && allowRefresh && !m_refreshing && !getAuthRefreshToken().empty()) { refreshAndRetry(method, path, body, std::move(callback)); return; } callback(std::move(res)); });
    }
    void refreshAndRetry(std::string method, std::string path, std::string body, std::function<void(web::WebResponse)> callback) {
        m_refreshing = true; auto req = web::WebRequest(); req.header("Content-Type", "application/json"); req.body(makeBody(fmt::format("{{\"refresh_token\":{}}}", makeJsonString(getAuthRefreshToken()))));
        m_refreshTask.spawn(req.post(trimSlash(getIndexUrl()) + "/v1/login/refresh"), [this, method, path, body, callback = std::move(callback)](web::WebResponse res) mutable { m_refreshing = false; if (!res.ok()) { clearAuthTokens(); onClose(nullptr); return; } auto p = res.json().unwrapOr(matjson::Value())["payload"]; auto a = p["access_token"].asString().unwrapOr(""); auto r = p["refresh_token"].asString().unwrapOr(""); if (a.empty() || r.empty()) { clearAuthTokens(); onClose(nullptr); return; } setAuthTokens(a, r); request(method, path, body, std::move(callback), false); });
    }
    void loadMods() {}
public: static MyModsPopup* create() { auto ret = new MyModsPopup(); if (ret && ret->init()) { ret->autorelease(); return ret; } delete ret; return nullptr; }
};

class AccountPopup : public Popup {
    CCLabelBMFont *m_name=nullptr,*m_verifiedBadge=nullptr,*m_adminBadge=nullptr,*m_id=nullptr; TextInput *m_displayName=nullptr,*m_modUrl=nullptr; CCMenuItemSpriteExtra *m_saveButton=nullptr,*m_submitButton=nullptr; async::TaskHolder<web::WebResponse> m_requestTask,m_refreshTask; bool m_refreshing=false,m_saving=false,m_submitting=false;
    bool init(); void updateNameBadges(bool, bool); void loadProfile(); void saveProfile(); void submitMod(); void confirmLogout();
    void request(std::string, std::string, std::string, std::function<void(web::WebResponse)>, bool = true);
    void refreshAndRetry(std::string, std::string, std::string, std::function<void(web::WebResponse)>);
public: static AccountPopup* create();
};
}

void showAccountPopup() { if (!hasAuthTokens()) { showGithubLoginPopup([] { showAccountPopup(); }); return; } AccountPopup::create()->show(); }
void showGithubLoginPopup(std::function<void()> onLoggedIn) { GithubLoginPopup::create(std::move(onLoggedIn))->show(); }
} // namespace opengeode
