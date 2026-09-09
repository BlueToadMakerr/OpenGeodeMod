#include "VersionsPopup.hpp"
#include "InstalledMods.hpp"
#include "PopupSectionUtils.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/utils/web.hpp>

#include <algorithm>
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
    std::string platformGD;
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
    auto timestamp = version["created_at"].asInt().unwrapOr(0);
    if (timestamp <= 0) return "Unknown date";

    std::time_t time = static_cast<std::time_t>(timestamp);
    std::tm localTime{};
#ifdef _WIN32
    localtime_s(&localTime, &time);
#else
    localtime_r(&time, &localTime);
#endif

    char buffer[64]{};
    if (std::strftime(buffer, sizeof(buffer), "%b %d, %Y", &localTime) == 0)
        return "Unknown date";
    return buffer;
}

std::string currentPlatformName() {
#ifdef GEODE_IS_WINDOWS
    return "Windows";
#elif defined(GEODE_IS_MACOS)
    return "Mac";
#elif defined(GEODE_IS_IOS)
    return "iOS";
#elif defined(GEODE_IS_ANDROID64)
    return "Android64";
#elif defined(GEODE_IS_ANDROID32)
    return "Android32";
#else
    return GEODE_PLATFORM_NAME;
#endif
}

std::string platformGDKey() {
#ifdef GEODE_IS_WINDOWS
    return "win";
#elif defined(GEODE_IS_MACOS)
    #ifdef GEODE_IS_ARM_MAC
    return "mac_arm";
    #else
    return "mac_intel";
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

bool isCompatible(std::string const& required, std::string const& current) {
    return required.empty() || required == "*" || required == current;
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

    void installVersion(std::string const& version);
    void rebuild();

public:
    static VersionPage* create(
        VersionsPopup* popup,
        std::vector<VersionData> versions,
        size_t start
    ) {
        auto ret = new VersionPage();
        if (ret && ret->init(popup, std::move(versions), start)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    bool init(
        VersionsPopup* popup,
        std::vector<VersionData> versions,
        size_t start
    );

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

    void rebuildPage() {
        if (!m_pageNode) return;
        m_pageNode->setStart(m_page * 5);
        auto pageCount = std::max<size_t>(1, (m_versions.size() + 4) / 5);
        m_pageLabel->setString(fmt::format("{}/{}", m_page + 1, pageCount).c_str());
        m_prevButton->setEnabled(m_page > 0);
        m_nextButton->setEnabled(m_page + 1 < pageCount);
    }

    void nextPage(CCObject*) {
        if ((m_page + 1) * 5 >= m_versions.size()) return;
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

        if (!Popup::init(300.f, 315.f, getPopupBackground())) return false;
        if (auto close = createGeodeCloseButton()) setCloseButtonSpr(close, .8f);

        const float width = 300.f;
        const float contentWidth = 275.f;
        const float contentHeight = 245.f;

        auto section = createSectionContainer({contentWidth, contentHeight});
        section->setPosition({width / 2.f, 155.f});
        m_mainLayer->addChild(section);

        m_loadingLabel = CCLabelBMFont::create("Loading...", "goldFont.fnt");
        m_loadingLabel->setScale(.32f);
        section->addChildAtPosition(m_loadingLabel, Anchor::Center);

        m_errorLabel = CCLabelBMFont::create("", "goldFont.fnt");
        m_errorLabel->setScale(.25f);
        m_errorLabel->setVisible(false);
        section->addChildAtPosition(m_errorLabel, Anchor::Center);

        m_content = CCNode::create();
        m_content->setContentSize({contentWidth - 10.f, contentHeight - 10.f});
        m_content->setAnchorPoint({.5f, .5f});
        section->addChildAtPosition(m_content, Anchor::Center);

        auto navigation = CCMenu::create();
        navigation->setContentSize({contentWidth - 20.f, 28.f});
        navigation->setLayout(RowLayout::create()->setAxisAlignment(AxisAlignment::Center)->setGap(12.f));
        m_mainLayer->addChildAtPosition(navigation, Anchor::Bottom, ccp(0.f, 17.f));

        auto prevSprite = ButtonSprite::create("<", "bigFont.fnt", getButtonTexture("GJ_button_01.png"), .38f);
        auto nextSprite = ButtonSprite::create(">", "bigFont.fnt", getButtonTexture("GJ_button_01.png"), .38f);
        m_prevButton = CCMenuItemExt::createSpriteExtra(prevSprite, [this](CCObject* obj) { previousPage(obj); });
        m_nextButton = CCMenuItemExt::createSpriteExtra(nextSprite, [this](CCObject* obj) { nextPage(obj); });
        m_pageLabel = CCLabelBMFont::create("1/1", "bigFont.fnt");
        m_pageLabel->setScale(.38f);
        navigation->addChild(m_prevButton);
        navigation->addChild(m_pageLabel);
        navigation->addChild(m_nextButton);
        navigation->updateLayout();

        m_requestTask.spawn(
            "OpenGeode version list",
            web::WebRequest().get(fmt::format("https://api.geode-sdk.org/v1/mods/{}", m_modID)),
            [this, modPopup](web::WebResponse response) {
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
                    auto platformKey = platformGDKey();
                    if (gd.isObject() && !platformKey.empty()) {
                        data.platformGD = gd[platformKey].asString().unwrapOr("");
                    }
                    data.gd = data.platformGD;
                    if (data.gd.empty() && gd.isObject()) {
                        data.gd = gd["win"].asString().unwrapOr("");
                    }

                    m_versions.push_back(std::move(data));
                    if (m_modName.empty()) m_modName = m_versions.back().name;
                }

                if (m_versions.empty()) {
                    showError("No versions found.");
                    return;
                }

                // The API returns versions newest first. Keep that order for the pages.
                m_modName = m_modName.empty() ? m_modID : m_modName;
                setTitle(fmt::format("{} Versions", m_modName));
                m_loadingLabel->setVisible(false);

                m_pageNode = VersionPage::create(this, m_versions, 0);
                if (m_pageNode) {
                    m_pageNode->setPosition({5.f, 5.f});
                    m_content->addChild(m_pageNode);
                    rebuildPage();
                }
            }
        );

        return true;
    }
};

bool VersionPage::init(
    VersionsPopup* popup,
    std::vector<VersionData> versions,
    size_t start
) {
    if (!CCNode::init()) return false;
    m_popup = popup;
    m_versions = std::move(versions);
    m_start = start;
    setContentSize({265.f, 235.f});
    rebuild();
    return true;
}

void VersionPage::rebuild() {
    removeAllChildrenWithCleanup(true);

    const auto currentGD = Loader::get()->getGameVersion();
    const auto currentGeode = Loader::get()->getVersion().toNonVString();
    const auto platform = currentPlatformName();
    const auto platformKey = platformGDKey();

    auto end = std::min(m_start + 5, m_versions.size());
    float y = 222.f;

    for (size_t i = m_start; i < end; ++i) {
        auto const& version = m_versions[i];
        auto row = CCNode::create();
        row->setContentSize({265.f, 43.f});
        row->setAnchorPoint({.5f, .5f});
        row->setPosition({132.5f, y});
        addChild(row);

        auto bg = NineSlice::create(getSectionBackground());
        bg->setColor({0, 0, 0});
        bg->setOpacity(65);
        bg->setScale(.3f);
        bg->setContentSize(row->getContentSize() / bg->getScale());
        row->addChildAtPosition(bg, Anchor::Center);

        auto versionText = version.version.starts_with("v") ? version.version : "v" + version.version;
        makeLabel(versionText, .38f, {255, 255, 255}, row, {7.f, 33.f});

        auto statusColor = ccColor3B{255, 255, 255};
        if (version.status == "accepted") statusColor = {0, 255, 0};
        else if (version.status == "rejected") statusColor = {255, 80, 80};
        else if (version.status == "pending") statusColor = {255, 220, 70};
        else if (version.status == "unlisted") statusColor = {180, 180, 180};

        auto statusText = version.status.empty() ? "Unknown" : version.status;
        std::transform(statusText.begin(), statusText.end(), statusText.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        auto status = makeLabel(statusText, .29f, statusColor, row, {42.f, 33.f});
        status->setPositionX(42.f);

        auto downloadsIcon = CCSprite::createWithSpriteFrameName("GJ_downloadsIcon_001.png");
        if (downloadsIcon) {
            downloadsIcon->setScale(.42f);
            downloadsIcon->setPosition({8.f, 17.f});
            row->addChild(downloadsIcon);
        }
        makeLabel(fmt::format("{}", version.downloads), .25f, {210, 210, 210}, row, {18.f, 17.f});

        auto timeIcon = CCSprite::createWithSpriteFrameName("GJ_timeIcon_001.png");
        if (timeIcon) {
            timeIcon->setScale(.38f);
            timeIcon->setPosition({73.f, 17.f});
            row->addChild(timeIcon);
        }
        makeLabel(version.date, .23f, {210, 210, 210}, row, {84.f, 17.f});

        bool gdCompatible = isCompatible(version.platformGD, currentGD);
        if (!version.platformGD.empty() && !gdCompatible) {
            makeLabel(fmt::format("Not supported on {}", platform), .23f, {255, 70, 70}, row, {7.f, 4.f});
        } else if (!version.platformGD.empty()) {
            makeLabel(fmt::format("GD {}", version.platformGD), .23f, {100, 255, 100}, row, {7.f, 4.f});
        }

        auto geodeCompatible = isCompatible(version.geode, currentGeode);
        auto geodeLabel = makeLabel(
            fmt::format("Geode {}", version.geode),
            .23f,
            geodeCompatible ? ccColor3B{100, 255, 100} : ccColor3B{255, 70, 70},
            row,
            {version.platformGD.empty() ? 7.f : 125.f, 4.f}
        );
        geodeLabel->setAnchorPoint({0.f, .5f});

        auto installSprite = ButtonSprite::create("Install", "bigFont.fnt", getButtonTexture("GJ_button_01.png"), .32f);
        installSprite->setScale(.68f);
        auto install = CCMenuItemExt::createSpriteExtra(installSprite, [this, version](CCObject*) {
            installVersion(version.version);
        });
        auto installed = getInstalledModSource(m_popup->m_modID);
        if (installed && installed->version == version.version) install->setEnabled(false);

        auto menu = CCMenu::create();
        menu->setPosition({235.f, 21.5f});
        menu->addChild(install);
        row->addChild(menu);

        y -= 47.f;
    }
}

void VersionPage::installVersion(std::string const& version) {
    if (!m_popup) return;
    auto install = m_popup->m_mainLayer->getParent();
    (void)install;

    auto actionNode = m_popup->m_mainLayer->getChildByIDRecursive("install-button");
    if (!actionNode) {
        // The native install button lives in the original ModPopup, not this popup.
        return;
    }
}

} // namespace

void showVersionsPopup(std::string const& modID, cocos2d::CCNode* modPopup) {
    if (modID.empty() || !modPopup) return;
    VersionsPopup::create(modID, modPopup)->show();
}

} // namespace opengeode
