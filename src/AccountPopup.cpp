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
        if ((*json).contains("error")) { auto v = (*json)["error"].asString().unwrapOr(""); if (!v.empty()) return v; }
        if ((*json).contains("detail")) { auto v = (*json)["detail"].asString().unwrapOr(""); if (!v.empty()) return v; }
        if ((*json).contains("message")) { auto v = (*json)["message"].asString().unwrapOr(""); if (!v.empty()) return v; }
    }
    if (response.code() > 0) return fmt::format("HTTP {}", response.code());
    if (!response.errorMessage().empty()) return std::string(response.errorMessage());
    return "Request failed.";
}
void showAlert(std::string const& title, std::string const& message) { FLAlertLayer::create(title.c_str(), message.c_str(), "OK")->show(); }
std::string makeJsonString(std::string const& value) { std::string out = "\""; for (auto c : value) { if (c == '\\' || c == '"') out += '\\'; out += c; } out += '"'; return out; }
ByteVector makeBody(std::string const& value) { return ByteVector(value.begin(), value.end()); }
bool versionIsNewer(std::string const& lhs, std::string const& rhs) {
    size_t l = 0, r = 0; while (l < lhs.size() || r < rhs.size()) { while (l < lhs.size() && !std::isdigit(static_cast<unsigned char>(lhs[l]))) ++l; while (r < rhs.size() && !std::isdigit(static_cast<unsigned char>(rhs[r]))) ++r; unsigned long long lv = 0, rv = 0; while (l < lhs.size() && std::isdigit(static_cast<unsigned char>(lhs[l]))) lv = lv * 10 + (lhs[l++] - '0'); while (r < rhs.size() && std::isdigit(static_cast<unsigned char>(rhs[r]))) rv = rv * 10 + (rhs[r++] - '0'); if (lv != rv) return lv > rv; } return lhs > rhs;
}

class GithubLoginPopup : public Popup {
    CCLabelBMFont* m_codeLabel = nullptr; CCLabelBMFont* m_urlLabel = nullptr; CCLabelBMFont* m_statusLabel = nullptr;
    std::function<void()> m_onLoggedIn; async::TaskHolder<web::WebResponse> m_task; std::string m_uuid;
    int m_pollInterval = 5; bool m_finished = false; bool m_pollInFlight = false;
    bool init(std::function<void()> onLoggedIn) {
        if (!Popup::init(320.f, 190.f, getPopupBackground())) return false; m_onLoggedIn = std::move(onLoggedIn); setTitle("GitHub Login");
        if (auto close = createGeodeCloseButton()) setCloseButtonSpr(close, .875f); auto center = m_mainLayer->getContentWidth() / 2.f;
        auto instructions = CCLabelBMFont::create("Open GitHub and enter this code:", "chatFont.fnt"); instructions->setScale(.65f); instructions->setPosition({center, 137.f}); m_mainLayer->addChild(instructions);
        m_codeLabel = CCLabelBMFont::create("Loading...", "bigFont.fnt"); m_codeLabel->setScale(.7f); m_codeLabel->setPosition({center, 106.f}); m_mainLayer->addChild(m_codeLabel);
        m_urlLabel = CCLabelBMFont::create("https://github.com/login/device", "chatFont.fnt"); m_urlLabel->setScale(.48f); m_urlLabel->setPosition({center, 78.f}); m_mainLayer->addChild(m_urlLabel);
        m_statusLabel = CCLabelBMFont::create("Starting login...", "chatFont.fnt"); m_statusLabel->setScale(.55f); m_statusLabel->setPosition({center, 48.f}); m_mainLayer->addChild(m_statusLabel); startLogin(); return true;
    }
    void startLogin() {
        auto req = web::WebRequest(); req.header("Accept", "application/json");
        m_task.spawn(req.post(trimSlash(getIndexUrl()) + "/v1/login/github"), [this](web::WebResponse res) {
            if (!res.ok()) { showAlert("Login Failed", errorText(res)); return; }
            auto payload = res.json().unwrapOr(matjson::Value())["payload"]; m_uuid = payload["uuid"].asString().unwrapOr("");
            auto uri = payload["uri"].asString().unwrapOr("https://github.com/login/device"); auto code = payload["code"].asString().unwrapOr("");
            auto interval = payload["interval"].asInt().unwrapOr(5L); m_pollInterval = interval < 1L ? 1 : static_cast<int>(interval);
            if (m_uuid.empty() || code.empty()) { showAlert("Login Failed", "The server returned an invalid GitHub login code."); return; }
            m_codeLabel->setString(code.c_str()); m_urlLabel->setString(uri.c_str()); m_statusLabel->setString(fmt::format("Waiting for authorization... (checking every {}s)", m_pollInterval).c_str()); schedulePoll();
        });
    }
    void schedulePoll() { if (!m_finished && !m_uuid.empty() && !m_pollInFlight) this->scheduleOnce(schedule_selector(GithubLoginPopup::poll), static_cast<float>(m_pollInterval)); }
    void poll(float) {
        if (m_finished || m_uuid.empty() || m_pollInFlight) return; m_pollInFlight = true; auto req = web::WebRequest(); req.header("Content-Type", "application/json"); req.body(makeBody(fmt::format("{{\"uuid\":{}}}", makeJsonString(m_uuid))));
        m_task.spawn(req.post(trimSlash(getIndexUrl()) + "/v1/login/github/poll"), [this](web::WebResponse res) {
            m_pollInFlight = false;
            if (res.ok()) { auto payload = res.json().unwrapOr(matjson::Value())["payload"]; auto access = payload["access_token"].asString().unwrapOr(""); auto refresh = payload["refresh_token"].asString().unwrapOr(""); if (access.empty() || refresh.empty()) { showAlert("Login Failed", "The server returned invalid login tokens."); return; } m_finished = true; unschedule(schedule_selector(GithubLoginPopup::poll)); setAuthTokens(access, refresh); if (m_onLoggedIn) m_onLoggedIn(); onClose(nullptr); return; }
            auto detail = errorText(res);
            if (res.code() == 400 && (detail == "Authorization pending" || detail == "authorization_pending" || detail == "Too fast" || detail == "too_fast")) { m_statusLabel->setString(fmt::format("Waiting for authorization... (checking every {}s)", m_pollInterval).c_str()); schedulePoll(); return; }
            showAlert("Login Failed", detail);
        });
    }
public: static GithubLoginPopup* create(std::function<void()> onLoggedIn) { auto ret = new GithubLoginPopup(); if (ret && ret->init(std::move(onLoggedIn))) { ret->autorelease(); return ret; } delete ret; return nullptr; }
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
    void loadMods() {
        auto mods = std::make_shared<std::map<std::string, ModInfo>>(); auto step = std::make_shared<std::function<void(int)>>();
        *step = [this, mods, step](int index) { static const char* statuses[] = {"accepted", "pending", "rejected"}; if (index >= 3) { std::string text; for (auto const& [id, mod] : *mods) { text += fmt::format("<mod:{}>  \n", id); for (auto const& [v, info] : mod.versions) { auto c = info.status == "accepted" ? "cg" : info.status == "pending" ? "cy" : "cr"; text += fmt::format("<{}>{} | v{} | {}", c, info.name.empty() ? id : info.name, v, info.status); if (info.status == "rejected" && !info.reason.empty()) text += fmt::format(" with the reason: {}", info.reason); text += "</c>  \n"; } text += "\n"; } if (text.empty()) text = "No submitted mods found.."; m_modArea->setString(text.c_str()); return; }
            request("GET", fmt::format("/v1/me/mods?status={}", statuses[index]), "", [this, mods, step, index](web::WebResponse res) { if (!res.ok()) { m_modArea->setString(errorText(res).c_str()); return; } auto payload = res.json().unwrapOr(matjson::Value())["payload"]; if (payload.isArray()) for (auto const& mod : payload) { auto id = mod["id"].asString().unwrapOr(""); if (id.empty()) continue; auto& entry = (*mods)[id]; entry.id = id; auto versions = mod["versions"]; if (versions.isArray()) for (auto const& version : versions) { auto v = version["version"].asString().unwrapOr(""); if (v.empty()) v = fmt::format("unknown-{}", entry.versions.size()); auto& info = entry.versions[v]; info.name = version["name"].asString().unwrapOr(id); info.status = version["status"].asString().unwrapOr(statuses[index]); info.reason = version["info"].asString().unwrapOr(""); } } (*step)(index + 1); }); };
        (*step)(0);
    }
public: static MyModsPopup* create() { auto ret = new MyModsPopup(); if (ret && ret->init()) { ret->autorelease(); return ret; } delete ret; return nullptr; }
};

class AccountPopup : public Popup {
    CCLabelBMFont *m_name=nullptr,*m_verifiedBadge=nullptr,*m_adminBadge=nullptr,*m_id=nullptr; TextInput *m_displayName=nullptr,*m_modUrl=nullptr; CCMenuItemSpriteExtra *m_saveButton=nullptr,*m_submitButton=nullptr; async::TaskHolder<web::WebResponse> m_requestTask,m_refreshTask; bool m_refreshing=false,m_saving=false,m_submitting=false;
    void updateNameBadges(bool verified, bool admin) { m_verifiedBadge->setVisible(verified); m_adminBadge->setVisible(admin); auto nw=m_name->getScaledContentSize().width; float bw=0; if(verified) bw+=m_verifiedBadge->getScaledContentSize().width; if(admin){if(verified)bw+=4.f;bw+=m_adminBadge->getScaledContentSize().width;} float gap=(verified||admin)?5.f:0.f; float left=m_mainLayer->getContentWidth()/2.f-(nw+(verified||admin?gap+bw:0))/2.f; m_name->setPosition({left,251.f}); float x=left+nw+gap; if(verified){m_verifiedBadge->setPosition({x,251.f});x+=m_verifiedBadge->getScaledContentSize().width+4.f;} if(admin)m_adminBadge->setPosition({x,251.f}); }
    bool init() { if(!Popup::init(370.f,285.f,getPopupBackground()))return false;setTitle("Geode Account");if(auto close=createGeodeCloseButton())setCloseButtonSpr(close,.8f);auto c=m_mainLayer->getContentWidth()/2; m_name=CCLabelBMFont::create("Loading...","bigFont.fnt");m_name->setScale(.48f);m_mainLayer->addChild(m_name);m_verifiedBadge=CCLabelBMFont::create("Verified","bigFont.fnt");m_verifiedBadge->setScale(.25f);m_verifiedBadge->setColor({100,255,100});m_verifiedBadge->setVisible(false);m_mainLayer->addChild(m_verifiedBadge);m_adminBadge=CCLabelBMFont::create("Admin","bigFont.fnt");m_adminBadge->setScale(.25f);m_adminBadge->setColor({255,80,80});m_adminBadge->setVisible(false);m_mainLayer->addChild(m_adminBadge);m_id=CCLabelBMFont::create("Account ID: -","chatFont.fnt");m_id->setScale(.36f);m_id->setPosition({c,235.f});m_mainLayer->addChild(m_id);auto dl=CCLabelBMFont::create("Display Name","goldFont.fnt");dl->setScale(.38f);dl->setPosition({c,220.f});m_mainLayer->addChild(dl);m_displayName=TextInput::create(190.f,"Display Name","chatFont.fnt");m_displayName->setPosition({c,195.f});m_mainLayer->addChild(m_displayName);auto save=CCMenuItemExt::createSpriteExtra(ButtonSprite::create("Save","goldFont.fnt",getButtonTexture("GJ_button_01.png"),.45f),[this](auto){saveProfile();});m_saveButton=save;auto logout=CCMenuItemExt::createSpriteExtra(ButtonSprite::create("Logout","goldFont.fnt",getButtonTexture("GJ_button_06.png"),.45f),[this](auto){confirmLogout();});auto mods=CCMenuItemExt::createSpriteExtra(ButtonSprite::create("My Mods","goldFont.fnt",getButtonTexture("GJ_button_01.png"),.45f),[](auto){MyModsPopup::create()->show();});auto buttons=CCMenu::create();buttons->addChild(save);buttons->addChild(mods);buttons->addChild(logout);buttons->setLayout(RowLayout::create()->setGap(6.f));buttons->setPosition({c,156.f});buttons->updateLayout();m_mainLayer->addChild(buttons);auto sl=CCLabelBMFont::create("Submit / Update Mod","goldFont.fnt");sl->setScale(.38f);sl->setPosition({c,128.f});m_mainLayer->addChild(sl);m_modUrl=TextInput::create(225.f,"Mod .geode URL","chatFont.fnt");m_modUrl->setPosition({c,103.f});m_mainLayer->addChild(m_modUrl);m_submitButton=CCMenuItemExt::createSpriteExtra(ButtonSprite::create("Submit","goldFont.fnt",getButtonTexture("GJ_button_01.png"),.42f),[this](auto){submitMod();});auto sm=CCMenu::create();sm->addChild(m_submitButton);sm->setPosition({c,67.f});m_mainLayer->addChild(sm);updateNameBadges(false,false);loadProfile();return true; }
    void request(std::string method,std::string path,std::string body,std::function<void(web::WebResponse)> cb,bool allowRefresh=true){auto t=getAuthAccessToken();if(t.empty()){clearAuthTokens();onClose(nullptr);return;}auto req=web::WebRequest();req.header("Authorization","Bearer "+t);if(!body.empty()){req.header("Content-Type","application/json");req.body(makeBody(body));}m_requestTask.spawn(req.send(method,trimSlash(getIndexUrl())+path),[this,method,path,body,cb=std::move(cb),allowRefresh](web::WebResponse r)mutable{if(r.code()==401&&allowRefresh&&!m_refreshing&&!getAuthRefreshToken().empty()){refreshAndRetry(method,path,body,std::move(cb));return;}cb(std::move(r));});}
    void refreshAndRetry(std::string method,std::string path,std::string body,std::function<void(web::WebResponse)>cb){m_refreshing=true;auto req=web::WebRequest();req.header("Content-Type","application/json");req.body(makeBody(fmt::format("{{\"refresh_token\":{}}}",makeJsonString(getAuthRefreshToken()))));m_refreshTask.spawn(req.post(trimSlash(getIndexUrl())+"/v1/login/refresh"),[this,method,path,body,cb=std::move(cb)](web::WebResponse r)mutable{m_refreshing=false;if(!r.ok()){clearAuthTokens();onClose(nullptr);return;}auto p=r.json().unwrapOr(matjson::Value())["payload"];auto a=p["access_token"].asString().unwrapOr("");auto rt=p["refresh_token"].asString().unwrapOr("");if(a.empty()||rt.empty()){clearAuthTokens();onClose(nullptr);return;}setAuthTokens(a,rt);request(method,path,body,std::move(cb),false);});}
    void loadProfile(){request("GET","/v1/me","",[this](web::WebResponse r){if(!r.ok()){showAlert("Account Error",errorText(r));onClose(nullptr);return;}auto p=r.json().unwrapOr(matjson::Value())["payload"];auto d=p["display_name"].asString().unwrapOr("");auto u=p["username"].asString().unwrapOr("");m_name->setString((d.empty()?u:d).c_str());m_verifiedBadge->setVisible(p["verified"].asBool().unwrapOr(false));m_adminBadge->setVisible(p["admin"].asBool().unwrapOr(false));updateNameBadges(p["verified"].asBool().unwrapOr(false),p["admin"].asBool().unwrapOr(false));m_id->setString(fmt::format("Account ID: {}",p["id"].asInt().unwrapOr(0)).c_str());m_displayName->setString(d.c_str());});}
    void setButtonText(CCMenuItemSpriteExtra* b,char const* text){if(auto s=typeinfo_cast<ButtonSprite*>(b->getNormalImage()))s->setString(text);}
    void saveProfile(){if(m_saving||m_submitting)return;std::string n=m_displayName->getString().c_str();if(n.size()<2||n.size()>64){showAlert("Save Failed","Display name must be 2-64 characters.");return;}m_saving=true;m_saveButton->setEnabled(false);setButtonText(m_saveButton,"Saving...");request("PUT","/v1/me",fmt::format("{{\"display_name\":{}}}",makeJsonString(n)),[this](web::WebResponse r){m_saving=false;m_saveButton->setEnabled(true);setButtonText(m_saveButton,"Save");if(!r.ok()){showAlert("Save Failed",errorText(r));return;}loadProfile();});}
    void submitMod(){if(m_submitting||m_saving)return;auto u=std::string(m_modUrl->getString().c_str());if(u.empty()){showAlert("Publish Failed","Enter a .geode download URL.");return;}m_submitting=true;m_submitButton->setEnabled(false);setButtonText(m_submitButton,"Publishing...");request("POST","/v1/mods",fmt::format("{{\"download_link\":{}}}",makeJsonString(u)),[this](web::WebResponse r){m_submitting=false;m_submitButton->setEnabled(true);setButtonText(m_submitButton,"Submit");if(!r.ok()){showAlert("Publish Failed",errorText(r));return;}auto p=r.json().unwrapOr(matjson::Value())["payload"];showAlert("Mod Submitted",fmt::format("<cg>{}</c> successfully.",p["id"].asString().unwrapOr("Mod")));m_modUrl->setString("");});}
    void confirmLogout(){createQuickPopup("Log Out","Are you sure you want to log out of this Geode account?","Cancel","Log Out",[this](FLAlertLayer*,bool confirmed){if(confirmed){clearAuthTokens();onClose(nullptr);}});}
public: static AccountPopup* create(){auto ret=new AccountPopup();if(ret&&ret->init()){ret->autorelease();return ret;}delete ret;return nullptr;}
};
}
void showAccountPopup(){if(!hasAuthTokens()){showGithubLoginPopup([]{showAccountPopup();});return;}AccountPopup::create()->show();}
void showGithubLoginPopup(std::function<void()> onLoggedIn){GithubLoginPopup::create(std::move(onLoggedIn))->show();}
} // namespace opengeode
