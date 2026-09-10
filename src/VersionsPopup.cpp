#include "VersionsPopup.hpp"
#include "InstalledMods.hpp"
#include "PopupSectionUtils.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/utils/web.hpp>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <tuple>

using namespace geode::prelude;

namespace opengeode {
namespace {

struct VersionData {
    std::string version, name, status, geode, gd, date;
    std::vector<std::pair<std::string, std::string>> platforms;
    int downloads = 0;
};

std::string getString(matjson::Value const& v, char const* key, std::string fallback = "") { return v[key].asString().unwrapOr(fallback); }
int getInt(matjson::Value const& v, char const* key) { return v[key].asInt().unwrapOr(0); }

std::string errorText(web::WebResponse const& response) {
    if (auto json = response.json()) {
        if ((*json).contains("error")) {
            if (auto value = (*json)["error"].asString()) return value.unwrap();
        }
        if ((*json).contains("detail")) {
            if (auto value = (*json)["detail"].asString()) return value.unwrap();
        }
        if ((*json).contains("message")) {
            if (auto value = (*json)["message"].asString()) return value.unwrap();
        }
    }

    if (response.code() > 0)
        return fmt::format("HTTP {}", response.code());
    if (!response.errorMessage().empty())
        return std::string(response.errorMessage());
    return "Request failed.";
}

std::string formatDate(matjson::Value const& v) {
    auto raw = v["created_at"];
    auto iso = raw.asString().unwrapOr("");
    if (!iso.empty()) return iso.size() >= 10 ? iso.substr(0, 10) : iso;
    auto timestamp = raw.asInt().unwrapOr(0);
    if (timestamp <= 0) return "Unknown";
    std::time_t t = static_cast<std::time_t>(timestamp);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &t);
#else
    gmtime_r(&t, &utc);
#endif
    char buffer[32]{};
    return std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &utc) ? buffer : "Unknown";
}

std::tuple<int, int, int> parseVersion(std::string value) {
    if (!value.empty() && value.front() == 'v') value.erase(value.begin());
    int parts[3] = {0, 0, 0};
    size_t start = 0;
    for (int i = 0; i < 3 && start <= value.size(); ++i) {
        auto end = value.find('.', start);
        auto part = value.substr(start, end == std::string::npos ? std::string::npos : end - start);
        size_t numberEnd = 0;
        while (numberEnd < part.size() && std::isdigit(static_cast<unsigned char>(part[numberEnd]))) ++numberEnd;
        if (numberEnd > 0) parts[i] = std::stoi(part.substr(0, numberEnd));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return {parts[0], parts[1], parts[2]};
}

std::string versionMajor(std::string value) { return std::to_string(std::get<0>(parseVersion(std::move(value)))); }

std::string currentPlatformKey() {
#ifdef GEODE_IS_WINDOWS
    return "win";
#elif defined(GEODE_IS_MACOS)
    #ifdef GEODE_IS_ARM_MAC
    return "mac-arm";
    #else
    return "mac-intel";
    #endif
#elif defined(GEODE_IS_IOS)
    return "ios";
#elif defined(GEODE_IS_ANDROID64)
    return "android64";
#elif defined(GEODE_IS_ANDROID32)
    return "android32";
#else
    return "";
#endif
}

std::string platformLabel(std::string const& key) {
    if (key == "win") return "Windows";
    if (key == "mac-arm") return "Mac ARM";
    if (key == "mac-intel") return "Mac Intel";
    if (key == "ios") return "iOS";
    if (key == "android64") return "Android64";
    if (key == "android32") return "Android32";
    return key;
}

ccColor3B statusColor(std::string const& status) {
    if (status == "accepted") return {100, 255, 100};
    if (status == "rejected") return {255, 80, 80};
    if (status == "pending") return {255, 220, 70};
    if (status == "unlisted") return {190, 190, 190};
    return {255, 255, 255};
}

CCLabelBMFont* makeLabel(std::string const& text, float scale, ccColor3B color, CCNode* parent, CCPoint position) {
    auto label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    label->setScale(scale);
    label->setColor(color);
    label->setAnchorPoint({0.f, .5f});
    label->setPosition(position);
    parent->addChild(label);
    return label;
}

class VersionsPopup;
class VersionRow;
void installVersionFromRow(VersionRow* row, CCObject* sender);

class VersionRow : public CCNode {
    VersionsPopup* m_popup = nullptr;
    std::string m_version;
    CCLabelBMFont* m_versionLabel = nullptr;
    CCLabelBMFont* m_statusLabel = nullptr;
    CCLabelBMFont* m_downloadLabel = nullptr;
    CCLabelBMFont* m_dateLabel = nullptr;
    CCLabelBMFont* m_gdLabel = nullptr;
    CCLabelBMFont* m_platformLabel = nullptr;
    CCLabelBMFont* m_geodeLabel = nullptr;
    CCMenuItemSpriteExtra* m_installButton = nullptr;
public:
    static VersionRow* create(VersionsPopup* popup);
    bool init(VersionsPopup* popup);
    void setVersion(VersionData const* data, std::string const& currentGD, std::string const& currentGeodeMajor, std::string const& currentPlatform);
    void installCurrentVersion();
};

class VersionsPopup : public Popup {
    async::TaskHolder<web::WebResponse> m_requestTask;
    CCLabelBMFont* m_loadingLabel = nullptr;
    CCLabelBMFont* m_errorLabel = nullptr;
    CCNode* m_content = nullptr;
    std::vector<VersionData> m_versions;
    std::string m_modID, m_modName;
    size_t m_page = 0;
    std::vector<VersionRow*> m_rows;
    CCLabelBMFont* m_pageLabel = nullptr;
    CCMenuItemSpriteExtra* m_prevButton = nullptr;
    CCMenuItemSpriteExtra* m_nextButton = nullptr;
    CCNode* m_modPopup = nullptr;

    size_t pageCount() const { return std::max<size_t>(1, (m_versions.size() + 4) / 5); }

    void rebuildPage() {
        if (!m_pageLabel) return;
        auto count = pageCount();
        if (m_page >= count) m_page = count - 1;
        auto currentGD = Loader::get()->getGameVersion();
        auto currentGeodeMajor = versionMajor(Loader::get()->getVersion().toNonVString());
        auto currentPlatform = currentPlatformKey();
        auto start = m_page * 5;
        for (size_t i = 0; i < m_rows.size(); ++i) {
            auto index = start + i;
            m_rows[i]->setVersion(index < m_versions.size() ? &m_versions[index] : nullptr, currentGD, currentGeodeMajor, currentPlatform);
        }
        m_pageLabel->setString(fmt::format("{}/{}", m_page + 1, count).c_str());
        if (m_prevButton) m_prevButton->setVisible(m_page > 0);
        if (m_nextButton) m_nextButton->setVisible(m_page + 1 < count);
    }

    void nextPage(CCObject*) { if (m_page + 1 < pageCount()) { ++m_page; rebuildPage(); } }
    void previousPage(CCObject*) { if (m_page > 0) { --m_page; rebuildPage(); } }
    void showError(std::string const& reason) {
        if (m_loadingLabel) m_loadingLabel->setVisible(false);
        if (m_errorLabel) { m_errorLabel->setString(reason.c_str()); m_errorLabel->setVisible(true); }
    }

public:
    std::string const& getModID() const { return m_modID; }
    void installVersion(std::string const& version) {
        if (!m_modPopup) return;
        auto install = m_modPopup->getChildByIDRecursive("install-button");
        auto action = typeinfo_cast<CCMenuItem*>(install);
        if (!action) return;
        setPendingVersionInstall(m_modID, version);
        action->activate();
        onClose(nullptr);
    }

    static VersionsPopup* create(std::string modID, CCNode* modPopup) {
        auto ret = new VersionsPopup();
        if (ret && ret->init(std::move(modID), modPopup)) { ret->autorelease(); return ret; }
        delete ret;
        return nullptr;
    }

    bool init(std::string modID, CCNode* modPopup) {
        m_modID = std::move(modID);
        m_modPopup = modPopup;
        if (!Popup::init(300.f, 292.f, getPopupBackground())) return false;
        if (auto close = createGeodeCloseButton()) setCloseButtonSpr(close, .8f);

        const float width = 300.f, contentWidth = 270.f, contentHeight = 218.f;
        m_content = CCNode::create();
        m_content->setContentSize({contentWidth, contentHeight});
        m_content->setAnchorPoint({.5f, .5f});
        m_content->setPosition({width / 2.f, 153.f});
        m_mainLayer->addChild(m_content);

        m_loadingLabel = CCLabelBMFont::create("Loading...", "chatFont.fnt");
        m_loadingLabel->setScale(.26f);
        m_content->addChildAtPosition(m_loadingLabel, Anchor::Center);
        m_errorLabel = CCLabelBMFont::create("", "chatFont.fnt");
        m_errorLabel->setScale(.26f);
        m_errorLabel->setAnchorPoint({.5f, .5f});
        m_errorLabel->setPosition({contentWidth / 2.f, contentHeight / 2.f - 8.f});
        m_errorLabel->setVisible(false);
        m_content->addChild(m_errorLabel);

        for (size_t i = 0; i < 5; ++i) {
            auto row = VersionRow::create(this);
            if (!row) continue;
            row->setPosition({contentWidth / 2.f, contentHeight - 21.f - static_cast<float>(i) * 44.f});
            m_content->addChild(row);
            m_rows.push_back(row);
        }

        auto prevMenu = CCMenu::create();
        prevMenu->setContentSize({32.f, 32.f});
        prevMenu->setPosition({-9.f, 146.f});
        m_mainLayer->addChild(prevMenu);
        if (auto sprite = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png")) {
            sprite->setScale(.65f);
            m_prevButton = CCMenuItemExt::createSpriteExtra(sprite, [this](CCObject* o) { previousPage(o); });
            prevMenu->addChild(m_prevButton);
        }

        auto nextMenu = CCMenu::create();
        nextMenu->setContentSize({32.f, 32.f});
        nextMenu->setPosition({width + 9.f, 146.f});
        m_mainLayer->addChild(nextMenu);
        if (auto sprite = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png")) {
            sprite->setScale(.65f);
            sprite->setRotation(180.f);
            m_nextButton = CCMenuItemExt::createSpriteExtra(sprite, [this](CCObject* o) { nextPage(o); });
            nextMenu->addChild(m_nextButton);
        }

        m_pageLabel = CCLabelBMFont::create("1/1", "bigFont.fnt");
        m_pageLabel->setScale(.5f);
        m_pageLabel->setAnchorPoint({.5f, .5f});
        m_pageLabel->setPosition({width / 2.f, 22.f});
        m_mainLayer->addChild(m_pageLabel);

        auto request = web::WebRequest();
        auto token = getAuthAccessToken();
        if (!token.empty()) request.header("Authorization", "Bearer " + token);
        m_requestTask.spawn(
            "OpenGeode version list",
            request.get(fmt::format("https://api.geode-sdk.org/v1/mods/{}", m_modID)),
            [this](web::WebResponse response) {
                if (!response.ok()) {
                    showError(errorText(response));
                    return;
                }
                auto json = response.json();
                if (!json) { showError("The server returned invalid JSON."); return; }
                auto payload = (*json)["payload"];
                auto versions = payload["versions"];
                if (!payload.isObject() || !versions.isArray()) { showError("The server response did not contain a versions list."); return; }

                for (auto const& version : versions) {
                    if (!version.isObject()) continue;
                    VersionData data;
                    data.version = getString(version, "version", "unknown");
                    data.name = getString(version, "name", m_modID);
                    data.status = getString(version, "status", "unknown");
                    data.geode = getString(version, "geode", "unknown");
                    data.downloads = getInt(version, "download_count");
                    data.date = formatDate(version);
                    auto gd = version["gd"];
                    if (gd.isObject()) {
                        for (auto const& key : {"win", "mac-arm", "mac-intel", "ios", "android32", "android64"}) {
                            auto value = gd[key].asString().unwrapOr("");
                            if (!value.empty()) data.platforms.emplace_back(key, value);
                        }
                    }
                    auto currentKey = currentPlatformKey();
                    for (auto const& [key, value] : data.platforms) if (key == currentKey) { data.gd = value; break; }
                    m_versions.push_back(std::move(data));
                    if (m_modName.empty()) m_modName = m_versions.back().name;
                }

                std::stable_sort(m_versions.begin(), m_versions.end(), [](VersionData const& a, VersionData const& b) {
                    auto av = parseVersion(a.version);
                    auto bv = parseVersion(b.version);
                    if (av != bv) return av > bv;
                    return a.date > b.date;
                });

                if (m_versions.empty()) { showError("No versions found."); return; }
                m_modName = m_modName.empty() ? m_modID : m_modName;
                setTitle(fmt::format("{} Versions", m_modName));
                m_loadingLabel->setVisible(false);
                rebuildPage();
            }
        );
        return true;
    }
};

VersionRow* VersionRow::create(VersionsPopup* popup) {
    auto ret = new VersionRow();
    if (ret && ret->init(popup)) { ret->autorelease(); return ret; }
    delete ret;
    return nullptr;
}

bool VersionRow::init(VersionsPopup* popup) {
    if (!CCNode::init()) return false;
    m_popup = popup;
    setContentSize({270.f, 40.f});
    setAnchorPoint({.5f, .5f});

    auto bg = NineSlice::create(getSectionBackground());
    bg->setColor({0, 0, 0});
    bg->setOpacity(65);
    bg->setScale(.3f);
    bg->setContentSize(getContentSize() / bg->getScale());
    addChildAtPosition(bg, Anchor::Center);

    m_versionLabel = makeLabel("", .38f, {255, 255, 255}, this, {6.f, 31.f});
    m_statusLabel = makeLabel("", .24f, {255, 255, 255}, this, {204.f, 31.f});
    m_statusLabel->setAnchorPoint({1.f, .5f});

    if (auto icon = CCSprite::createWithSpriteFrameName("GJ_downloadsIcon_001.png")) { icon->setScale(.32f); icon->setPosition({8.f, 19.f}); addChild(icon); }
    m_downloadLabel = makeLabel("", .26f, {205, 205, 205}, this, {14.f, 19.f});
    if (auto icon = CCSprite::createWithSpriteFrameName("GJ_timeIcon_001.png")) { icon->setScale(.30f); icon->setPosition({49.f, 19.f}); addChild(icon); }
    m_dateLabel = makeLabel("", .26f, {205, 205, 205}, this, {59.f, 19.f});

    m_gdLabel = makeLabel("", .26f, {255, 255, 255}, this, {6.f, 7.f});
    m_platformLabel = makeLabel("", .20f, {255, 70, 70}, this, {50.f, 7.f});
    m_geodeLabel = makeLabel("", .26f, {255, 255, 255}, this, {165.f, 7.f});

    auto installSprite = ButtonSprite::create("Install", "bigFont.fnt", getButtonTexture("GJ_button_01.png"), .36f);
    m_installButton = CCMenuItemExt::createSpriteExtra(installSprite, [this](CCObject* sender) { installVersionFromRow(this, sender); });
    m_installButton->setID("opengeode-version-install-button");
    auto menu = CCMenu::create();
    menu->setPosition({244.f, 20.f});
    menu->addChild(m_installButton);
    addChild(menu);
    setVisible(false);
    return true;
}

void VersionRow::setVersion(VersionData const* data, std::string const& currentGD, std::string const& currentGeodeMajor, std::string const& currentPlatform) {
    if (!data) { setVisible(false); m_version.clear(); return; }
    setVisible(true);
    m_version = data->version;
    m_versionLabel->setString((data->version.starts_with("v") ? data->version : "v" + data->version).c_str());
    auto statusText = data->status.empty() ? "Unknown" : data->status;
    std::transform(statusText.begin(), statusText.end(), statusText.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    m_statusLabel->setString(statusText.c_str());
    m_statusLabel->setColor(statusColor(data->status));
    m_downloadLabel->setString(fmt::format("{}", data->downloads).c_str());
    m_dateLabel->setString(data->date.c_str());

    bool gdCompatible = !data->gd.empty() && (data->gd == "*" || data->gd == currentGD);
    bool geodeCompatible = data->geode == "*" || versionMajor(data->geode) == currentGeodeMajor;
    auto gdText = data->gd.empty() ? std::string("GD ?") : fmt::format("GD {}", data->gd);
    m_gdLabel->setString(gdText.c_str());
    m_gdLabel->setColor(gdCompatible ? ccColor3B{100, 255, 100} : ccColor3B{255, 70, 70});

    m_platformLabel->setString("");
    if (!currentPlatform.empty()) {
        bool hasPlatform = false;
        for (auto const& [key, value] : data->platforms) if (key == currentPlatform) { hasPlatform = true; break; }
        if (!hasPlatform && !data->platforms.empty()) {
            std::string supported;
            for (auto const& [key, value] : data->platforms) { if (!supported.empty()) supported += ", "; supported += platformLabel(key); }
            m_platformLabel->setString(supported.c_str());
        }
    }

    auto geodeText = fmt::format("Geode {}", data->geode);
    m_geodeLabel->setString(geodeText.c_str());
    m_geodeLabel->setColor(geodeCompatible ? ccColor3B{100, 255, 100} : ccColor3B{255, 70, 70});

    auto installed = getInstalledModSource(m_popup->getModID());
    bool isInstalled = installed && installed->version == data->version;
    auto installSprite = typeinfo_cast<ButtonSprite*>(m_installButton->getNormalImage());
    if (installSprite) installSprite->setString(isInstalled ? "Installed" : "Install");
    m_installButton->setEnabled(!isInstalled);
}

void VersionRow::installCurrentVersion() {
    if (m_popup && !m_version.empty()) m_popup->installVersion(m_version);
}

void installVersionFromRow(VersionRow* row, CCObject*) {
    if (row) row->installCurrentVersion();
}

}
