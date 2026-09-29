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
    size_t l = 0;
    size_t r = 0;
    while (l < lhs.size() || r < rhs.size()) {
        while (l < lhs.size() && !std::isdigit(static_cast<unsigned char>(lhs[l]))) ++l;
        while (r < rhs.size() && !std::isdigit(static_cast<unsigned char>(rhs[r]))) ++r;
        unsigned long long lv = 0;
        unsigned long long rv = 0;
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

    bool init(std::function<void()> onLoggedIn) {
        if (!Popup::init(320.f, 190.f, getPopupBackground())) return false;
        m_onLoggedIn = std::move(onLoggedIn);
        setTitle("GitHub Login");
        if (auto close = createGeodeCloseButton()) setCloseButtonSpr(close, .875f);

        auto center = m_mainLayer->getContentWidth() / 2.f;
        auto instructions = CCLabelBMFont::create("Open GitHub and enter this code:", "chatFont.fnt");
        instructions->setScale(.65f);
        instructions->setPosition({center, 137.f});
        m_mainLayer->addChild(instructions);

        m_codeLabel = CCLabelBMFont::create("Loading...", "bigFont.fnt");
        m_codeLabel->setScale(.7f);
        m_codeLabel->setPosition({center, 106.f});
        m_mainLayer->addChild(m_codeLabel);

        m_urlLabel = CCLabelBMFont::create("https://github.com/login/device", "chatFont.fnt");
        m_urlLabel->setScale(.48f);
        m_urlLabel->setPosition({center, 78.f});
        m_mainLayer->addChild(m_urlLabel);

        m_statusLabel = CCLabelBMFont::create("Starting login...", "chatFont.fnt");
        m_statusLabel->setScale(.55f);
        m_statusLabel->setPosition({center, 48.f});
        m_mainLayer->addChild(m_statusLabel);

        startLogin();
        return true;
    }

    void startLogin() {
        m_statusLabel->setString("Starting login...");
        auto req = web::WebRequest();
        req.header("Accept", "application/json");
        m_task.spawn(req.post(trimSlash(getIndexUrl()) + "/v1/login/github"), [this](web::WebResponse res) {
            if (!res.ok()) {
                showAlert("Login Failed", errorText(res));
                return;
            }
            auto payload = res.json().unwrapOr(matjson::Value())["payload"];
            m_uuid = payload["uuid"].asString().unwrapOr("");
            auto uri = payload["uri"].asString().unwrapOr("https://github.com/login/device");
            auto code = payload["code"].asString().unwrapOr("");
            m_pollInterval = payload["interval"].asInt().unwrapOr(5);
            if (m_uuid.empty() || code.empty()) {
                showAlert("Login Failed", "The server returned an invalid GitHub login code.");
                return;
            }
            m_codeLabel->setString(code.c_str());
            m_urlLabel->setString(uri.c_str());
            m_statusLabel->setString("Waiting for GitHub authorization...");
            schedulePoll();
        });
    }

    void schedulePoll() {
        if (m_finished || m_uuid.empty()) return;
        this->scheduleOnce(schedule_selector(GithubLoginPopup::poll), static_cast<float>(std::max(1, m_pollInterval)));
    }

    void poll(float) {
        if (m_finished || m_uuid.empty()) return;
        auto req = web::WebRequest();
        req.header("Content-Type", "application/json");
        req.body(makeBody(fmt::format("{{\"uuid\":{}}}", makeJsonString(m_uuid))));
        m_task.spawn(req.post(trimSlash(getIndexUrl()) + "/v1/login/github/poll"), [this](web::WebResponse res) {
            if (res.ok()) {
                auto payload = res.json().unwrapOr(matjson::Value())["payload"];
                auto access = payload["access_token"].asString().unwrapOr("");
                auto refresh = payload["refresh_token"].asString().unwrapOr("");
                if (access.empty() || refresh.empty()) {
                    showAlert("Login Failed", "The server returned invalid login tokens.");
                    return;
                }
                m_finished = true;
                unschedule(schedule_selector(GithubLoginPopup::poll));
                setAuthTokens(access, refresh);
                if (m_onLoggedIn) m_onLoggedIn();
                onClose(nullptr);
                return;
            }

            auto detail = errorText(res);
            if (res.code() == 400 && detail == "Authorization pending") {
                m_statusLabel->setString("Waiting for GitHub authorization...");
                schedulePoll();
                return;
            }

            showAlert("Login Failed", detail);
        });
    }

public:
    static GithubLoginPopup* create(std::function<void()> onLoggedIn) {
        auto ret = new GithubLoginPopup();
        if (ret && ret->init(std::move(onLoggedIn))) {
            ret->autorelease();
            return ret;
        }
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

    bool init() {
        if (!Popup::init(370.f, 285.f, getPopupBackground())) return false;
        setTitle("My Mods");
        if (auto close = createGeodeCloseButton()) setCloseButtonSpr(close, .8f);
        m_modArea = MDTextArea::create("Loading mods...", {330.f, 210.f});
        if (!m_modArea) return false;
        m_modArea->setAnchorPoint({0.f, 1.f}); m_modArea->setPosition({20.f, 245.f}); m_mainLayer->addChild(m_modArea); loadMods(); return true;
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
            m_refreshing = false; if (!res.ok()) { clearAuthTokens(); onClose(nullptr); return; }
            auto payload = res.json().unwrapOr(matjson::Value())["payload"]; auto access = payload["access_token"].asString().unwrapOr(""); auto refresh = payload["refresh_token"].asString().unwrapOr("");
            if (access.empty() || refresh.empty()) { clearAuthTokens(); onClose(nullptr); return; }
            setAuthTokens(access, refresh); request(method, path, body, std::move(callback), false);
        });
    }

    void loadMods() {
        auto mods = std::make_shared<std::map<std::string, ModInfo>>(); auto step = std::make_shared<std::function<void(int)>>();
        *step = [this, mods, step](int index) {
            static const char* statuses[] = {"accepted", "pending", "rejected"};
            if (index >= 3) {
                std::string text; auto statusColor = [](std::string const& status) { if (status == "accepted") return "cg"; if (status == "pending") return "cy"; if (status == "rejected") return "cr"; return "cw"; };
                for (auto const& [id, mod] : *mods) {
                    text += fmt::format("<mod:{}>  \n", id);
                    std::vector<std::pair<std::string, VersionInfo>> versions; versions.reserve(mod.versions.size());
                    for (auto const& [version, info] : mod.versions) versions.emplace_back(version, info);
                    std::sort(versions.begin(), versions.end(), [](auto const& a, auto const& b) { return versionIsNewer(a.first, b.first); });
                    for (auto const& [version, info] : versions) {
                        auto color = statusColor(info.status); text += fmt::format("<{}>{} | v{} | {}", color, info.name.empty() ? id : info.name, version, info.status);
                        if (info.status == "rejected" && !info.reason.empty()) text += fmt::format(" with the reason: {}", info.reason); text += "</c>  \n";
                    }
                    text += "\n";
                }
                if (text.empty()) text = "No submitted mods found.."; m_modArea->setString(text.c_str()); return;
            }
            request("GET", fmt::format("/v1/me/mods?status={}", statuses[index]), "", [this, mods, step, index](web::WebResponse res) {
                if (!res.ok()) { m_modArea->setString(errorText(res).c_str()); return; }
                auto payload = res.json().unwrapOr(matjson::Value())["payload"];
                if (payload.isArray()) for (auto const& mod : payload) {
                    auto id = mod["id"].asString().unwrapOr(""); if (id.empty()) continue; auto& entry = (*mods)[id]; entry.id = id;
                    auto versions = mod["versions"];
                    if (versions.isArray()) for (auto const& version : versions) {
                        auto versionID = version["version"].asString().unwrapOr(""); if (versionID.empty()) versionID = fmt::format("unknown-{}", entry.versions.size());
                        auto& info = entry.versions[versionID]; info.name = version["name"].asString().unwrapOr(id); info.status = version["status"].asString().unwrapOr(statuses[index]); info.reason = version["info"].asString().unwrapOr("");
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
    CCLabelBMFont* m_verifiedBadge = nullptr;
    CCLabelBMFont* m_adminBadge = nullptr;
    CCLabelBMFont* m_id = nullptr;
    TextInput* m_displayName = nullptr;
    TextInput* m_modUrl = nullptr;
    CCMenuItemSpriteExtra* m_saveButton = nullptr;
    CCMenuItemSpriteExtra* m_submitButton = nullptr;
    async::TaskHolder<web::WebResponse> m_requestTask;
    async::TaskHolder<web::WebResponse> m_refreshTask;
    bool m_refreshing = false;
    bool m_saving = false;
    bool m_submitting = false;

    void updateNameBadges(bool verified, bool admin) {
        m_verifiedBadge->setVisible(verified);
        m_adminBadge->setVisible(admin);
        m_name->setAnchorPoint({0.f, .5f});
        m_verifiedBadge->setAnchorPoint({0.f, .5f});
        m_adminBadge->setAnchorPoint({0.f, .5f});
        auto nameWidth = m_name->getScaledContentSize().width;
        float badgeWidth = 0.f;
        if (verified) badgeWidth += m_verifiedBadge->getScaledContentSize().width;
        if (admin) { if (verified) badgeWidth += 4.f; badgeWidth += m_adminBadge->getScaledContentSize().width; }
        float gap = (verified || admin) ? 5.f : 0.f;
        float totalWidth = nameWidth + (verified || admin ? gap + badgeWidth : 0.f);
        float left = m_mainLayer->getContentWidth() / 2.f - totalWidth / 2.f;
        m_name->setPosition({left, 251.f});
        float x = left + nameWidth + gap;
        if (verified) { m_verifiedBadge->setPosition({x, 251.f}); x += m_verifiedBadge->getScaledContentSize().width + 4.f; }
        if (admin) m_adminBadge->setPosition({x, 251.f});
    }

    bool init() {
        if (!Popup::init(370.f, 285.f, getPopupBackground())) return false;
        setTitle("Geode Account"); if (auto close = createGeodeCloseButton()) setCloseButtonSpr(close, .8f);
        auto center = m_mainLayer->getContentWidth() / 2;
        m_name = CCLabelBMFont::create("Loading...", "bigFont.fnt"); m_name->setScale(.48f); m_name->setAnchorPoint({0.f, .5f}); m_mainLayer->addChild(m_name);
        m_verifiedBadge = CCLabelBMFont::create("Verified", "bigFont.fnt"); m_verifiedBadge->setScale(.25f); m_verifiedBadge->setColor({100, 255, 100}); m_verifiedBadge->setAnchorPoint({0.f, .5f}); m_verifiedBadge->setVisible(false); m_mainLayer->addChild(m_verifiedBadge);
        m_adminBadge = CCLabelBMFont::create("Admin", "bigFont.fnt"); m_adminBadge->setScale(.25f); m_adminBadge->setColor({255, 80, 80}); m_adminBadge->setAnchorPoint({0.f, .5f}); m_adminBadge->setVisible(false); m_mainLayer->addChild(m_adminBadge);
        updateNameBadges(false, false);
        m_id = CCLabelBMFont::create("Account ID: -", "chatFont.fnt"); m_id->setScale(.36f); m_id->setAlignment(kCCTextAlignmentCenter); m_id->setAnchorPoint({.5f, .5f}); m_id->setPosition({center, 235.f}); m_mainLayer->addChild(m_id);
        auto displayLabel = CCLabelBMFont::create("Display Name", "goldFont.fnt"); displayLabel->setScale(.38f); displayLabel->setAlignment(kCCTextAlignmentCenter); displayLabel->setAnchorPoint({.5f, .5f}); displayLabel->setPosition({center, 220.f}); m_mainLayer->addChild(displayLabel);
        m_displayName = TextInput::create(190.f, "Display Name", "chatFont.fnt"); m_displayName->setPosition({center, 195.f}); m_mainLayer->addChild(m_displayName);

        auto saveSprite = ButtonSprite::create("Save", "goldFont.fnt", getButtonTexture("GJ_button_01.png"), .45f); m_saveButton = CCMenuItemExt::createSpriteExtra(saveSprite, [this](auto) { saveProfile(); });
        auto logout = CCMenuItemExt::createSpriteExtra(ButtonSprite::create("Logout", "goldFont.fnt", getButtonTexture("GJ_button_06.png"), .45f), [this](auto) { confirmLogout(); });
        auto mods = CCMenuItemExt::createSpriteExtra(ButtonSprite::create("My Mods", "goldFont.fnt", getButtonTexture("GJ_button_01.png"), .45f), [](auto) { MyModsPopup::create()->show(); });
        auto buttons = CCMenu::create(); buttons->addChild(m_saveButton); buttons->addChild(mods); buttons->addChild(logout); buttons->setLayout(RowLayout::create()->setGap(6.f)); buttons->setPosition({center, 156.f}); buttons->updateLayout(); m_mainLayer->addChild(buttons);

        auto submitLabel = CCLabelBMFont::create("Submit / Update Mod", "goldFont.fnt"); submitLabel->setScale(.38f); submitLabel->setAlignment(kCCTextAlignmentCenter); submitLabel->setAnchorPoint({.5f, .5f}); submitLabel->setPosition({center, 128.f}); m_mainLayer->addChild(submitLabel);
        m_modUrl = TextInput::create(225.f, "Mod .geode URL", "chatFont.fnt"); m_modUrl->setPosition({center, 103.f}); m_mainLayer->addChild(m_modUrl);
        auto submitSprite = ButtonSprite::create("Submit", "goldFont.fnt", getButtonTexture("GJ_button_01.png"), .42f); m_submitButton = CCMenuItemExt::createSpriteExtra(submitSprite, [this](auto) { submitMod(); });
        auto submitMenu = CCMenu::create(); submitMenu->addChild(m_submitButton); submitMenu->setPosition({center, 67.f}); m_mainLayer->addChild(submitMenu);
        loadProfile(); return true;
    }

    void request(std::string method, std::string path, std::string body, std::function<void(web::WebResponse)> callback, bool allowRefresh = true) {
        auto token = getAuthAccessToken(); if (token.empty()) { clearAuthTokens(); onClose(nullptr); return; }
        auto req = web::WebRequest(); req.header("Authorization", "Bearer " + token); if (!body.empty()) { req.header("Content-Type", "application/json"); req.body(makeBody(body)); }
        m_requestTask.spawn(req.send(method, trimSlash(getIndexUrl()) + path), [this, method, path, body, callback = std::move(callback), allowRefresh](web::WebResponse res) mutable {
            if (res.code() == 401 && allowRefresh && !m_refreshing && !getAuthRefreshToken().empty()) { refreshAndRetry(method, path, body, std::move(callback)); return; } callback(std::move(res));
        });
    }

    void refreshAndRetry(std::string method, std::string path, std::string body, std::function<void(web::WebResponse)> callback) {
        m_refreshing = true; auto req = web::WebRequest(); req.header("Content-Type", "application/json"); req.body(makeBody(fmt::format("{{\"refresh_token\":{}}}", makeJsonString(getAuthRefreshToken()))));
        m_refreshTask.spawn(req.post(trimSlash(getIndexUrl()) + "/v1/login/refresh"), [this, method, path, body, callback = std::move(callback)](web::WebResponse res) mutable {
            m_refreshing = false; if (!res.ok()) { clearAuthTokens(); showAlert("Session Expired", "Your session has expired. Please log in again."); onClose(nullptr); return; }
            auto payload = res.json().unwrapOr(matjson::Value())["payload"]; auto access = payload["access_token"].asString().unwrapOr(""); auto refresh = payload["refresh_token"].asString().unwrapOr("");
            if (access.empty() || refresh.empty()) { clearAuthTokens(); showAlert("Session Expired", "Your session could not be refreshed. Please log in again."); onClose(nullptr); return; }
            setAuthTokens(access, refresh); request(method, path, body, std::move(callback), false);
        });
    }

    void loadProfile() {
        request("GET", "/v1/me", "", [this](web::WebResponse res) {
            if (!res.ok()) { showAlert("Account Error", errorText(res)); onClose(nullptr); return; }
            auto p = res.json().unwrapOr(matjson::Value())["payload"];
            auto display = p["display_name"].asString().unwrapOr(""); auto username = p["username"].asString().unwrapOr("");
            auto verified = p["verified"].asBool().unwrapOr(false); auto admin = p["admin"].asBool().unwrapOr(false);
            auto plainName = display.empty() ? username : display;
            m_name->setString(plainName.c_str()); updateNameBadges(verified, admin);
            m_id->setString(fmt::format("Account ID: {}", p["id"].asInt().unwrapOr(0)).c_str()); m_displayName->setString(display.c_str());
        });
    }

    void setButtonText(CCMenuItemSpriteExtra* button, char const* text) {
        if (!button) return; if (auto sprite = typeinfo_cast<ButtonSprite*>(button->getNormalImage())) sprite->setString(text);
    }

    void saveProfile() {
        if (m_saving || m_submitting) return;
        std::string name = m_displayName->getString().c_str();
        if (name.size() < 2 || name.size() > 64) { showAlert("Save Failed", "Display name must be 2-64 characters."); return; }
        for (auto c : name) if (!std::isalnum(static_cast<unsigned char>(c)) || static_cast<unsigned char>(c) > 127) { showAlert("Save Failed", "Display name must be ASCII letters/numbers."); return; }
        m_saving = true; m_saveButton->setEnabled(false); setButtonText(m_saveButton, "Saving...");
        request("PUT", "/v1/me", fmt::format("{{\"display_name\":{}}}", makeJsonString(name)), [this](web::WebResponse res) {
            m_saving = false; m_saveButton->setEnabled(true); setButtonText(m_saveButton, "Save");
            if (!res.ok()) { showAlert("Save Failed", errorText(res)); return; } loadProfile();
        });
    }

    void submitMod() {
        if (m_submitting || m_saving) return;
        std::string url = m_modUrl->getString().c_str();
        if (url.empty()) { showAlert("Publish Failed", "Enter a .geode download URL."); return; }
        if (url.size() > 1024) { showAlert("Publish Failed", "URL is too long."); return; }
        m_submitting = true; m_submitButton->setEnabled(false); setButtonText(m_submitButton, "Publishing...");
        request("POST", "/v1/mods", fmt::format("{{\"download_link\":{}}}", makeJsonString(url)), [this](web::WebResponse res) {
            m_submitting = false; m_submitButton->setEnabled(true); setButtonText(m_submitButton, "Submit");
            if (!res.ok()) { showAlert("Publish Failed", errorText(res)); return; }
            auto payload = res.json().unwrapOr(matjson::Value())["payload"]; auto id = payload["id"].asString().unwrapOr(""); auto versions = payload["versions"]; std::string status;
            if (versions.isArray()) for (auto const& version : versions) { status = version["status"].asString().unwrapOr(""); break; }
            if (status.empty()) status = "submitted";
            showAlert("Mod Submitted", fmt::format("<cg>{}</c> <cy>{}</c> successfully.", id.empty() ? "Mod" : id, status)); m_modUrl->setString("");
        });
    }

    void confirmLogout() {
        createQuickPopup("Log Out", "Are you sure you want to log out of this account?", "Cancel", "Log Out", [this](FLAlertLayer*, bool confirmed) {
            if (confirmed) { clearAuthTokens(); onClose(nullptr); }
        });
    }

public:
    static AccountPopup* create() { auto ret = new AccountPopup(); if (ret && ret->init()) { ret->autorelease(); return ret; } delete ret; return nullptr; }
};
}

void showAccountPopup() { if (!hasAuthTokens()) { showGithubLoginPopup([] { showAccountPopup(); }); return; } AccountPopup::create()->show(); }
void showGithubLoginPopup(std::function<void()> onLoggedIn) { GithubLoginPopup::create(std::move(onLoggedIn))->show(); }
} // namespace opengeode
