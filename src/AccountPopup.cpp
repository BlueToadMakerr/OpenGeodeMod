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
    if (auto json = response.json()) {
        if ((*json).contains("error")) {
            auto value = (*json)["error"].asString().unwrapOr("");
            if (!value.empty()) return value;
        }
        if ((*json).contains("detail")) {
            auto value = (*json)["detail"].asString().unwrapOr("");
            if (!value.empty()) return value;
        }
        if ((*json).contains("message")) {
            auto value = (*json)["message"].asString().unwrapOr("");
            if (!value.empty()) return value;
        }
    }
    if (response.code() > 0) return fmt::format("HTTP {}", response.code());
    if (!response.errorMessage().empty()) return std::string(response.errorMessage());
    return "Request failed.";
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

bool versionIsNewer(std::string const& lhs, std::string const& rhs) {
    size_t l = 0, r = 0;
    while (l < lhs.size() || r < rhs.size()) {
        while (l < lhs.size() && !std::isdigit(static_cast<unsigned char>(lhs[l]))) ++l;
        while (r < rhs.size() && !std::isdigit(static_cast<unsigned char>(rhs[r]))) ++r;
        unsigned long long lv = 0, rv = 0;
        while (l < lhs.size() && std::isdigit(static_cast<unsigned char>(lhs[l]))) lv = lv * 10 + (lhs[l++] - '0');
        while (r < rhs.size() && std::isdigit(static_cast<unsigned char>(rhs[r]))) rv = rv * 10 + (rhs[r++] - '0');
        if (lv != rv) return lv > rv;
    }
    return lhs > rhs;
}

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

    void startLogin() {
        m_statusLabel->setString("Starting login...");
        auto req = web::WebRequest();
        req.header("Accept", "application/json");
        m_task.spawn(req.post(trimSlash(getIndexUrl()) + "/v1/login/github"), [this](web::WebResponse res) {
            if (!res.ok()) { showAlert("Login Failed", errorText(res)); return; }
            auto payload = res.json().unwrapOr(matjson::Value())["payload"];
            m_uuid = payload["uuid"].asString().unwrapOr("");
            auto uri = payload["uri"].asString().unwrapOr("https://github.com/login/device");
            auto code = payload["code"].asString().unwrapOr("");
            auto interval = payload["interval"].asInt().unwrapOr(5L);
            m_pollInterval = interval < 1L ? 1 : static_cast<int>(interval);
            if (m_uuid.empty() || code.empty()) { showAlert("Login Failed", "The server returned an invalid GitHub login code."); return; }
            m_codeLabel->setString(code.c_str());
            m_urlLabel->setString(uri.c_str());
            m_statusLabel->setString(fmt::format("Waiting for authorization... (checking every {}s)", m_pollInterval).c_str());
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
                if (access.empty() || refresh.empty()) { showAlert("Login Failed", "The server returned invalid login tokens."); return; }
                m_finished = true;
                unschedule(schedule_selector(GithubLoginPopup::poll));
                setAuthTokens(access, refresh);
                if (m_onLoggedIn) m_onLoggedIn();
                onClose(nullptr);
                return;
            }
            auto detail = errorText(res);
            if (res.code() == 400 && (detail == "Authorization pending" || detail == "authorization_pending")) {
                m_statusLabel->setString(fmt::format("Waiting for authorization... (checking every {}s)", m_pollInterval).c_str());
                schedulePoll();
                return;
            }
            if (res.code() == 400 && (detail == "Too fast" || detail == "too_fast")) {
                m_statusLabel->setString(fmt::format("Rate limited; waiting {}s...", m_pollInterval).c_str());
                schedulePoll();
                return;
            }
            showAlert("Login Failed", detail);
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
protected:
    async::TaskHolder<web::WebResponse> m_requestTask;
    async::TaskHolder<web::WebResponse> m_refreshTask;
    bool m_refreshing = false;
    MDTextArea* m_modArea = nullptr;
    struct VersionInfo { std::string name; std::string status; std::string reason; };
    struct ModInfo { std::string id; std::map<std::string, VersionInfo> versions; };
    bool init();
    void loadMods();
    void request(std::string, std::string, std::string, std::function<void(web::WebResponse)>, bool = true);
    void refreshAndRetry(std::string, std::string, std::string, std::function<void(web::WebResponse)>);
public:
    static MyModsPopup* create();
};

class AccountPopup : public Popup {
protected:
    CCLabelBMFont* m_name = nullptr; CCLabelBMFont* m_verifiedBadge = nullptr; CCLabelBMFont* m_adminBadge = nullptr; CCLabelBMFont* m_id = nullptr;
    TextInput* m_displayName = nullptr; TextInput* m_modUrl = nullptr; CCMenuItemSpriteExtra* m_saveButton = nullptr; CCMenuItemSpriteExtra* m_submitButton = nullptr;
    async::TaskHolder<web::WebResponse> m_requestTask; async::TaskHolder<web::WebResponse> m_refreshTask; bool m_refreshing = false; bool m_saving = false; bool m_submitting = false;
    bool init(); void updateNameBadges(bool, bool); void loadProfile(); void saveProfile(); void submitMod(); void confirmLogout();
    void request(std::string, std::string, std::string, std::function<void(web::WebResponse)>, bool = true);
    void refreshAndRetry(std::string, std::string, std::string, std::function<void(web::WebResponse)>);
public: static AccountPopup* create();
};
}

void showAccountPopup() { if (!hasAuthTokens()) { showGithubLoginPopup([] { showAccountPopup(); }); return; } AccountPopup::create()->show(); }
void showGithubLoginPopup(std::function<void()> onLoggedIn) { GithubLoginPopup::create(std::move(onLoggedIn))->show(); }
} // namespace opengeode
