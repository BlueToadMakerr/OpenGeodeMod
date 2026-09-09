#include "VersionsPopup.hpp"
#include "InstalledMods.hpp"
#include "PopupSectionUtils.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/utils/web.hpp>

#include <algorithm>
#include <cctype>
#include <ctime>

using namespace geode::prelude;

namespace opengeode {
namespace {

struct VersionData {
    std::string version;
    std::string name;
    std::string status;
    std::string geode;
    std::string gd;
    std::vector<std::pair<std::string, std::string>> platforms;
    std::string date;
    int downloads = 0;
};

std::string getString(matjson::Value const& value, char const* key, std::string fallback = "") {
    return value[key].asString().unwrapOr(fallback);
}

int getInt(matjson::Value const& value, char const* key) {
    return value[key].asInt().unwrapOr(0);
}

std::string formatDate(matjson::Value const& version) {
    auto raw = version["created_at"];
    auto iso = raw.asString().unwrapOr("");
    if (!iso.empty()) {
        if (iso.size() >= 10) return iso.substr(0, 10);
        return iso;
    }

    auto timestamp = raw.asInt().unwrapOr(0);
    if (timestamp <= 0) return "Unknown";
    std::time_t time = static_cast<std::time_t>(timestamp);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    char buffer[32]{};
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &utc) == 0) return "Unknown";
    return buffer;
}

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

std::string versionMajor(std::string value) {
    if (!value.empty() && value.front() == 'v') value.erase(value.begin());
    auto dot = value.find('.');
    return dot == std::string::npos ? value : value.substr(0, dot);
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

CCLabelBMFont* makeLabel(
    std::string const& text,
    float scale,
    ccColor3B color,
    CCNode* parent,
    CCPoint position
) {
    auto label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    label->setScale(scale);
    label->setColor(color);
    label->setAnchorPoint({0.f, .5f});
    label->setPosition(position);
    parent->addChild(label);
    return label;
}

class VersionsPopup;

class VersionPage : public CCNode {
    VersionsPopup* m_popup = nullptr;
    std::vector<VersionData> m_versions;
    size_t m_start = 0;

    void rebuild();

public:
    static VersionPage* create(VersionsPopup* popup, std::vector<VersionData> versions) {
        auto ret = new VersionPage();
        if (ret && ret->init(popup, std::move(versions))) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    bool init(VersionsPopup* popup, std::vector<VersionData> versions) {
        if (!CCNode::init()) return false;
        m_popup = popup;
        m_versions = std::move(versions);
        setContentSize({270.f, 218.f});
        rebuild();
        return true;
    }

    void setStart(size_t start) {
        m_start = start;
        rebuild();
    }
};

class VersionsPopup : public Popup {
    async::TaskHolder<web::WebResponse> m_requestTask;
    CCLabelBMFont* m_loadingLabel = nullptr;
    CCLabelBMFont* m_errorLabel = nullptr;
    CCNode* m_content = nullptr;
    std::vector<VersionData> m_versions;
    std::string m_modID;
    std::string m_modName;
    size_t m_page = 0;
    VersionPage* m_pageNode = nullptr;
    CCLabelBMFont* m_pageLabel = nullptr;
    CCMenuItemSpriteExtra* m_prevButton = nullptr;
    CCMenuItemSpriteExtra* m_nextButton = nullptr;
    CCNode* m_modPopup = nullptr;

    size_t pageCount() const {
        return std::max<size_t>(1, (m_versions.size() + 4) / 5);
    }

    void rebuildPage() {
        if (!m_pageNode) return;
        auto count = pageCount();
        if (m_page >= count) m_page = count - 1;
        m_pageNode->setStart(m_page * 5);
        m_pageLabel->setString(fmt::format("{}/{}", m_page + 1, count).c_str());
        if (m_prevButton) m_prevButton->setVisible(m_page > 0);
        if (m_nextButton) m_nextButton->setVisible(m_page + 1 < count);
    }

    void nextPage(CCObject*) {
        if (m_page + 1 >= pageCount()) return;
        ++m_page;
        rebuildPage();
    }

    void previousPage(CCObject*) {
        if (m_page == 0) return;
        --m_page;
        rebuildPage();
    }

    void showError(std::string const& reason) {
        if (m_loadingLabel) m_loadingLabel->setVisible(false);
        if (m_errorLabel) {
            m_errorLabel->setString(reason.c_str());
            m_errorLabel->setVisible(true);
        }
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
        if (ret && ret->init(std::move(modID), modPopup)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    bool init(std::string modID, CCNode* modPopup) {
        m_modID = std::move(modID);
        m_modPopup = modPopup;

        if (!Popup::init(300.f, 292.f, getPopupBackground())) return false;
        if (auto close = createGeodeCloseButton()) setCloseButtonSpr(close, .8f);

        const float width = 300.f;
        const float contentWidth = 270.f;
        const float contentHeight = 218.f;

        m_content = CCNode::create();
        m_content->setContentSize({contentWidth, contentHeight});
        m_content->setAnchorPoint({.5f, .5f});
        m_content->setPosition({width / 2.f, 153.f});
        m_mainLayer->addChild(m_content);

        m_loadingLabel = CCLabelBMFont::create("Loading...", "goldFont.fnt");
        m_loadingLabel->setScale(.32f);
        m_content->addChildAtPosition(m_loadingLabel, Anchor::Center);

        m_errorLabel = CCLabelBMFont::create("", "goldFont.fnt");
        m_errorLabel->setScale(.24f);
        m_errorLabel->setVisible(false);
        m_content->addChildAtPosition(m_errorLabel, Anchor::Center);

        auto prevMenu = CCMenu::create();
        prevMenu->setContentSize({32.f, 32.f});
        prevMenu->setPosition({-9.f, 131.f});
        m_mainLayer->addChild(prevMenu);

        auto prevSprite = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
        if (prevSprite) {
            prevSprite->setScale(.65f);
            m_prevButton = CCMenuItemExt::createSpriteExtra(prevSprite, [this](CCObject* obj) { previousPage(obj); });
            prevMenu->addChild(m_prevButton);
        }

        auto nextMenu = CCMenu::create();
        nextMenu->setContentSize({32.f, 32.f});
        nextMenu->setPosition({width + 9.f, 131.f});
        m_mainLayer->addChild(nextMenu);

        auto nextSprite = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
        if (nextSprite) {
            nextSprite->setScale(.65f);
            nextSprite->setRotation(180.f);
            m_nextButton = CCMenuItemExt::createSpriteExtra(nextSprite, [this](CCObject* obj) { nextPage(obj); });
            nextMenu->addChild(m_nextButton);
        }

        auto pageMenu = CCMenu::create();
        pageMenu->setContentSize({width, 30.f});
        pageMenu->setPosition({width / 2.f, 22.f});
        m_mainLayer->addChild(pageMenu);

        m_pageLabel = CCLabelBMFont::create("1/1", "bigFont.fnt");
        m_pageLabel->setScale(.5f);
        m_pageLabel->setPosition({width / 2.f, 15.f});
        pageMenu->addChild(m_pageLabel);

        m_requestTask.spawn(
            "OpenGeode version list",
            web::WebRequest().get(fmt::format("https://api.geode-sdk.org/v1/mods/{}", m_modID)),
            [this](web::WebResponse response) {
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
                    for (auto const& [key, value] : data.platforms) {
                        if (key == currentKey) {
                            data.gd = value;
                            break;
                        }
                    }

                    m_versions.push_back(std::move(data));
                    if (m_modName.empty()) m_modName = m_versions.back().name;
                }

                std::stable_sort(m_versions.begin(), m_versions.end(), [](VersionData const& a, VersionData const& b) {
                    return a.date > b.date;
                });

                if (m_versions.empty()) {
                    showError("No versions found.");
                    return;
                }

                m_modName = m_modName.empty() ? m_modID : m_modName;
                setTitle(fmt::format("{} Versions", m_modName));
                m_loadingLabel->setVisible(false);

                m_pageNode = VersionPage::create(this, m_versions);
                if (m_pageNode) {
                    m_pageNode->setPosition({0.f, 0.f});
                    m_content->addChild(m_pageNode);
                    rebuildPage();
                }
            }
        );

        return true;
    }
};

void VersionPage::rebuild() {
    removeAllChildrenWithCleanup(true);

    const auto currentGD = Loader::get()->getGameVersion();
    const auto currentGeode = Loader::get()->getVersion().toNonVString();
    const auto currentGeodeMajor = versionMajor(currentGeode);
    const auto currentKey = currentPlatformKey();

    auto end = std::min(m_start + 5, m_versions.size());
    float y = 197.f;

    for (size_t i = m_start; i < end; ++i) {
        auto const& version = m_versions[i];
        auto row = CCNode::create();
        row->setContentSize({270.f, 40.f});
        row->setAnchorPoint({.5f, .5f});
        row->setPosition({135.f, y});
        addChild(row);

        auto bg = NineSlice::create(getSectionBackground());
        bg->setColor({0, 0, 0});
        bg->setOpacity(65);
        bg->setScale(.3f);
        bg->setContentSize(row->getContentSize() / bg->getScale());
        row->addChildAtPosition(bg, Anchor::Center);

        auto versionText = version.version.starts_with("v") ? version.version : "v" + version.version;
        makeLabel(versionText, .38f, {255, 255, 255}, row, {6.f, 31.f});

        auto statusText = version.status.empty() ? "Unknown" : version.status;
        std::transform(statusText.begin(), statusText.end(), statusText.begin(), [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        });
        auto status = makeLabel(statusText, .24f, statusColor(version.status), row, {0.f, 31.f});
        status->setAnchorPoint({1.f, .5f});
        status->setPosition({204.f, 31.f});

        auto downloadsIcon = CCSprite::createWithSpriteFrameName("GJ_downloadsIcon_001.png");
        if (downloadsIcon) {
            downloadsIcon->setScale(.32f);
            downloadsIcon->setPosition({8.f, 19.f});
            row->addChild(downloadsIcon);
        }
        makeLabel(fmt::format("{}", version.downloads), .21f, {205, 205, 205}, row, {17.f, 19.f});

        auto timeIcon = CCSprite::createWithSpriteFrameName("GJ_timeIcon_001.png");
        if (timeIcon) {
            timeIcon->setScale(.30f);
            timeIcon->setPosition({52.f, 19.f});
            row->addChild(timeIcon);
        }
        makeLabel(version.date, .19f, {205, 205, 205}, row, {62.f, 19.f});

        bool gdCompatible = !version.gd.empty() && (version.gd == "*" || version.gd == currentGD);
        bool geodeCompatible = version.geode == "*" || versionMajor(version.geode) == currentGeodeMajor;

        auto gdText = version.gd.empty() ? "GD ?" : fmt::format("GD {}", version.gd);
        makeLabel(gdText, .18f, gdCompatible ? ccColor3B{100, 255, 100} : ccColor3B{255, 70, 70}, row, {6.f, 7.f});

        if (!currentKey.empty()) {
            bool hasPlatform = false;
            for (auto const& [key, value] : version.platforms) {
                if (key == currentKey) {
                    hasPlatform = true;
                    break;
                }
            }

            if (!hasPlatform && !version.platforms.empty()) {
                std::string supported;
                for (auto const& [key, value] : version.platforms) {
                    if (!supported.empty()) supported += ", ";
                    supported += platformLabel(key);
                }
                makeLabel(supported, .14f, {255, 70, 70}, row, {70.f, 7.f});
            }
        }

        makeLabel(
            fmt::format("Geode {}", version.geode),
            .18f,
            geodeCompatible ? ccColor3B{100, 255, 100} : ccColor3B{255, 70, 70},
            row,
            {135.f, 7.f}
        );

        auto installed = getInstalledModSource(m_popup->getModID());
        bool isInstalled = installed && installed->version == version.version;
        auto installText = isInstalled ? "Installed" : "Install";
        auto installSprite = ButtonSprite::create(installText, "bigFont.fnt", getButtonTexture("GJ_button_01.png"), .28f);
        installSprite->setScale(.65f);
        auto install = CCMenuItemExt::createSpriteExtra(installSprite, [this, version](CCObject*) {
            m_popup->installVersion(version.version);
        });
        if (isInstalled) install->setEnabled(false);

        auto menu = CCMenu::create();
        menu->setPosition({246.f, 20.f});
        menu->addChild(install);
        row->addChild(menu);

        y -= 44.f;
    }
}

} // namespace

void showVersionsPopup(std::string const& modID, cocos2d::CCNode* modPopup) {
    if (modID.empty() || !modPopup) return;
    VersionsPopup::create(modID, modPopup)->show();
}

} // namespace opengeode
