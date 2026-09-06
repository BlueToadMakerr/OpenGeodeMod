#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/async.hpp>
#include <alphalaneous.alphas_geode_utils/include/ObjectModify.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <vector>

using namespace geode::prelude;

// ---------------------------------------------------------------------------
// Plain-file persistence under this mod's save folder, deliberately NOT
// using Mod::getSavedValue/setSavedValue -- on some SDK/platform stub
// combinations (seen on android64) that path pulls in
// Mod::getSaveContainerConst()/getSaveContainerTemp(), which can be missing
// from the linked stub and fail the build with an undefined symbol error.
// A plain file per key sidesteps that entirely and needs nothing beyond the
// standard library.
// ---------------------------------------------------------------------------

namespace {
    std::filesystem::path settingPath(std::string const& key) {
        auto dir = Mod::get()->getSaveDir();
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return dir / (key + ".txt");
    }

    std::string readSetting(std::string const& key, std::string const& fallback) {
        auto path = settingPath(key);
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            log::debug("[OpenGeode] readSetting '{}' -> not found at {}, using fallback '{}'", key, path.string(), fallback);
            return fallback;
        }
        std::ostringstream ss;
        ss << file.rdbuf();
        auto value = ss.str();
        log::debug("[OpenGeode] readSetting '{}' -> '{}' (from {})", key, value, path.string());
        return value;
    }

    void writeSetting(std::string const& key, std::string const& value) {
        auto path = settingPath(key);
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << value;
        log::debug("[OpenGeode] writeSetting '{}' = '{}' (to {})", key, value, path.string());
    }

    void deleteSetting(std::string const& key) {
        std::error_code ec;
        std::filesystem::remove(settingPath(key), ec);
    }

    std::vector<std::string> splitCSV(std::string const& raw) {
        std::vector<std::string> out;
        std::stringstream ss(raw);
        std::string item;
        while (std::getline(ss, item, ',')) {
            if (!item.empty()) out.push_back(item);
        }
        return out;
    }

    std::string joinCSV(std::vector<std::string> const& items) {
        std::string out;
        for (size_t i = 0; i < items.size(); ++i) {
            if (i) out += ',';
            out += items[i];
        }
        return out;
    }
}

// ---------------------------------------------------------------------------
// Theme detection -- Geode's own loader has a "used-theme" setting; when
// it's not literally "Geometry Dash", we treat that as Geode's own (dark)
// theme and skin our buttons/popups to match instead of the GD-style
// defaults.
// ---------------------------------------------------------------------------

bool useDarkTheme() {
    auto loader = Loader::get()->getLoadedMod("geode.loader");
    if (!loader) return true;
    std::string geodeTheme = loader->getSettingValue<std::string>("used-theme");
    return geodeTheme != "Geometry Dash";
}

// ---------------------------------------------------------------------------
// Active index URL + browse overrides
// ---------------------------------------------------------------------------

std::string getIndexUrl() {
    return readSetting("custom-index-url", "https://api.geode-sdk.org");
}

void setIndexUrl(std::string url) {
    if (!url.empty() && url.back() == '/') url.pop_back();
    writeSetting("custom-index-url", url);
}

// Overrides for the platform / geode / gd query params Geode normally fills
// in automatically based on this device's real platform and loader/GD
// version. Empty means "leave it alone, use whatever Geode would normally
// send". Letting these be overridden just changes what shows up when
// *browsing* the index (downloads/featured/recent tabs all send
// platforms=/geode=/gd= already) -- Geode still refuses to actually install
// anything incompatible with this device, so there's nothing unsafe about
// unlocking them.
std::string getOverridePlatform() { return readSetting("override-platform", ""); }
void setOverridePlatform(std::string value) { writeSetting("override-platform", value); }

std::string getOverrideGeodeVersion() { return readSetting("override-geode-version", ""); }
void setOverrideGeodeVersion(std::string value) { writeSetting("override-geode-version", value); }

std::string getOverrideGDVersion() { return readSetting("override-gd-version", ""); }
void setOverrideGDVersion(std::string value) { writeSetting("override-gd-version", value); }

bool isFilterActive() {
    return !getOverridePlatform().empty() || !getOverrideGeodeVersion().empty() || !getOverrideGDVersion().empty();
}

// ---------------------------------------------------------------------------
// Saved index list. The "Geode Index API" default is not stored on disk as
// an entry -- it's hardcoded with an empty id. A separate flag tracks
// whether the user chose to hide it from the list (see the double-confirm
// delete flow below); everything else the user adds gets a small numeric id
// (so re-adding never collides) and two files: its name and its URL.
// ---------------------------------------------------------------------------

struct IndexEntry {
    std::string id;   // empty => the built-in default
    std::string name;
    std::string url;
};

static const IndexEntry DEFAULT_INDEX{"", "Geode Index API", "https://api.geode-sdk.org"};

bool isDefaultIndexHidden() { return readSetting("default-index-hidden", "") == "1"; }
void setDefaultIndexHidden(bool hidden) { writeSetting("default-index-hidden", hidden ? "1" : ""); }

std::vector<IndexEntry> getCustomIndexes() {
    std::vector<IndexEntry> out;
    for (auto const& id : splitCSV(readSetting("custom-index-ids", ""))) {
        IndexEntry entry;
        entry.id = id;
        entry.name = readSetting("custom-index-name-" + id, "");
        entry.url = readSetting("custom-index-url-" + id, "");
        if (!entry.url.empty()) out.push_back(entry);
    }
    return out;
}

std::vector<IndexEntry> getAllIndexes() {
    auto out = getCustomIndexes();
    if (!isDefaultIndexHidden()) {
        out.insert(out.begin(), DEFAULT_INDEX);
    }
    return out;
}

void addCustomIndex(std::string name, std::string url) {
    if (!url.empty() && url.back() == '/') url.pop_back();

    auto ids = splitCSV(readSetting("custom-index-ids", ""));
    int nextId = 0;
    for (auto const& id : ids) {
        nextId = std::max(nextId, std::atoi(id.c_str()) + 1);
    }
    std::string id = std::to_string(nextId);

    ids.push_back(id);
    writeSetting("custom-index-ids", joinCSV(ids));
    writeSetting("custom-index-name-" + id, name);
    writeSetting("custom-index-url-" + id, url);
}

void updateCustomIndex(std::string const& id, std::string name, std::string url) {
    if (!url.empty() && url.back() == '/') url.pop_back();
    bool wasActive = (readSetting("custom-index-url-" + id, "") == getIndexUrl());
    writeSetting("custom-index-name-" + id, name);
    writeSetting("custom-index-url-" + id, url);
    if (wasActive) setIndexUrl(url); // keep "active" pointing at the (possibly changed) URL
}

void deleteCustomIndex(std::string const& id) {
    auto ids = splitCSV(readSetting("custom-index-ids", ""));

    // If the entry being removed is the active one, fall back to the
    // default index instead of leaving "active" pointing at a URL that's
    // no longer in the list anywhere.
    auto url = readSetting("custom-index-url-" + id, "");
    if (!url.empty() && url == getIndexUrl()) {
        setIndexUrl(DEFAULT_INDEX.url);
    }

    ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
    writeSetting("custom-index-ids", joinCSV(ids));
    deleteSetting("custom-index-name-" + id);
    deleteSetting("custom-index-url-" + id);
}

// NOTE: gd/geode/platforms are NOT part of the URL string at all --
// WebRequest stores query params separately (see getUrlParams()/param() in
// <Geode/utils/web.hpp>) and only merges them into the final query string
// at actual dispatch time. Overriding them means calling req.param()
// directly on the intercepted request; see the interceptor below.

// ---------------------------------------------------------------------------
// Shared popup background theming. Geode's own Popup base tags its
// background node with id "background" in current SDK versions -- if yours
// differs, this just silently no-ops instead of failing to build, so swap
// in whatever your version exposes if you want it fully working.
// ---------------------------------------------------------------------------

void applyPopupTheme(Popup* popup) {
    if (!useDarkTheme()) return; // GD theme -- keep the default GJ_square01 look
    if (auto bg = typeinfo_cast<CCScale9Sprite*>(popup->getChildByID("background"))) {
        if (auto frame = CCSpriteFrameCache::sharedSpriteFrameCache()->spriteFrameByName("GE_square_01.png")) {
            bg->setSpriteFrame(frame);
        }
    }
}

// ---------------------------------------------------------------------------
// Fetches and displays /v1/stats for a given index URL into a label. Shared
// by the Modify/Info popup below.
// ---------------------------------------------------------------------------

struct StatsFetcher {
    CCLabelBMFont* label = nullptr;
    async::TaskHolder<web::WebResponse> listener;

    void fetch(std::string url) {
        if (!label) return;
        label->setString("Loading info...");
        if (!url.empty() && url.back() == '/') url.pop_back();

        listener.spawn(
            web::WebRequest().get(url + "/v1/stats"),
            [this](web::WebResponse res) {
                if (res.ok()) {
                    auto json = res.json().unwrapOr(matjson::Value());
                    if (json.contains("payload")) {
                        auto payload = json["payload"];
                        int totalMods = payload["total_mod_count"].asInt().unwrapOr(0);
                        int totalDownloads = payload["total_mod_downloads"].asInt().unwrapOr(0);
                        label->setString(fmt::format("Mods: {} | Downloads: {}", totalMods, totalDownloads).c_str());
                        return;
                    }
                }
                label->setString("Could not retrieve info from endpoint.");
            }
        );
    }
};

// ---------------------------------------------------------------------------
// Add Index popup -- name + URL, appends to the saved list.
// ---------------------------------------------------------------------------

class AddIndexPopup : public Popup {
protected:
    TextInput* m_nameInput = nullptr;
    TextInput* m_urlInput = nullptr;
    std::function<void()> m_onAdded;

    bool init(std::function<void()> onAdded) {
        if (!Popup::init(280.f, 160.f)) return false;
        applyPopupTheme(this);
        m_onAdded = onAdded;

        this->setTitle("Add Index");

        float centerX = m_mainLayer->getContentWidth() / 2;
        float top = m_mainLayer->getContentHeight() - 50.f;

        auto nameLabel = CCLabelBMFont::create("Name", "bigFont.fnt");
        nameLabel->setScale(0.35f);
        nameLabel->setPosition({centerX, top});
        m_mainLayer->addChild(nameLabel);

        m_nameInput = TextInput::create(220.f, "My Custom Index", "chatFont.fnt");
        m_nameInput->setPosition({centerX, top - 20.f});
        m_mainLayer->addChild(m_nameInput);

        auto urlLabel = CCLabelBMFont::create("URL", "bigFont.fnt");
        urlLabel->setScale(0.35f);
        urlLabel->setPosition({centerX, top - 50.f});
        m_mainLayer->addChild(urlLabel);

        m_urlInput = TextInput::create(220.f, "https://example.com", "chatFont.fnt");
        m_urlInput->setPosition({centerX, top - 70.f});
        m_mainLayer->addChild(m_urlInput);

        auto addBtn = CCMenuItemExt::createSpriteExtra(
            ButtonSprite::create("Add", "goldFont.fnt", "GJ_button_01.png", 0.6f),
            [this](auto) {
                auto name = m_nameInput->getString();
                auto url = m_urlInput->getString();
                if (name.empty() || url.empty()) {
                    FLAlertLayer::create("Error", "Both a name and a URL are required.", "OK")->show();
                    return;
                }
                addCustomIndex(name, url);
                if (m_onAdded) m_onAdded();
                this->onClose(nullptr);
            }
        );
        auto menu = CCMenu::create();
        menu->addChild(addBtn);
        menu->setPosition({centerX, 20.f});
        m_mainLayer->addChild(menu);

        return true;
    }

public:
    static AddIndexPopup* create(std::function<void()> onAdded) {
        auto ret = new AddIndexPopup();
        if (ret && ret->init(onAdded)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

// ---------------------------------------------------------------------------
// Modify / Info popup -- editable for custom indexes (name, URL, Save),
// read-only for the built-in default (just shows its name/URL). Both modes
// fetch and display that index's live /v1/stats.
// ---------------------------------------------------------------------------

class ModifyIndexPopup : public Popup {
protected:
    TextInput* m_nameInput = nullptr;
    TextInput* m_urlInput = nullptr;
    StatsFetcher m_stats;
    std::string m_id;
    bool m_readOnly = false;
    std::function<void()> m_onSaved;

    bool init(IndexEntry entry, std::function<void()> onSaved) {
        m_id = entry.id;
        m_readOnly = entry.id.empty();
        m_onSaved = onSaved;

        if (!Popup::init(300.f, m_readOnly ? 160.f : 220.f)) return false;
        applyPopupTheme(this);
        this->setTitle(m_readOnly ? "Index Info" : "Modify Index");

        float centerX = m_mainLayer->getContentWidth() / 2;
        float top = m_mainLayer->getContentHeight() - 40.f;

        if (m_readOnly) {
            auto nameLabel = CCLabelBMFont::create(entry.name.c_str(), "bigFont.fnt");
            nameLabel->setScale(0.5f);
            nameLabel->setPosition({centerX, top});
            m_mainLayer->addChild(nameLabel);

            auto urlLabel = CCLabelBMFont::create(entry.url.c_str(), "chatFont.fnt");
            urlLabel->setScale(0.4f);
            urlLabel->setPosition({centerX, top - 25.f});
            m_mainLayer->addChild(urlLabel);

            m_stats.label = CCLabelBMFont::create("Fetching index stats...", "bigFont.fnt");
            m_stats.label->setScale(0.35f);
            m_stats.label->setPosition({centerX, top - 60.f});
            m_mainLayer->addChild(m_stats.label);
        } else {
            auto nameLbl = CCLabelBMFont::create("Name", "bigFont.fnt");
            nameLbl->setScale(0.35f);
            nameLbl->setPosition({centerX, top});
            m_mainLayer->addChild(nameLbl);

            m_nameInput = TextInput::create(220.f, "Name", "chatFont.fnt");
            m_nameInput->setString(entry.name);
            m_nameInput->setPosition({centerX, top - 20.f});
            m_mainLayer->addChild(m_nameInput);

            auto urlLbl = CCLabelBMFont::create("URL", "bigFont.fnt");
            urlLbl->setScale(0.35f);
            urlLbl->setPosition({centerX, top - 50.f});
            m_mainLayer->addChild(urlLbl);

            m_urlInput = TextInput::create(220.f, "https://example.com", "chatFont.fnt");
            m_urlInput->setString(entry.url);
            m_urlInput->setPosition({centerX, top - 70.f});
            m_mainLayer->addChild(m_urlInput);

            m_stats.label = CCLabelBMFont::create("Fetching index stats...", "bigFont.fnt");
            m_stats.label->setScale(0.35f);
            m_stats.label->setPosition({centerX, top - 100.f});
            m_mainLayer->addChild(m_stats.label);

            auto saveBtn = CCMenuItemExt::createSpriteExtra(
                ButtonSprite::create("Save", "goldFont.fnt", "GJ_button_02.png", 0.6f),
                [this](auto) {
                    auto name = m_nameInput->getString();
                    auto url = m_urlInput->getString();
                    if (name.empty() || url.empty()) {
                        FLAlertLayer::create("Error", "Both a name and a URL are required.", "OK")->show();
                        return;
                    }
                    updateCustomIndex(m_id, name, url);
                    if (m_onSaved) m_onSaved();
                    this->onClose(nullptr);
                }
            );
            auto menu = CCMenu::create();
            menu->addChild(saveBtn);
            menu->setPosition({centerX, 20.f});
            m_mainLayer->addChild(menu);
        }

        m_stats.fetch(entry.url);
        return true;
    }

public:
    static ModifyIndexPopup* create(IndexEntry entry, std::function<void()> onSaved) {
        auto ret = new ModifyIndexPopup();
        if (ret && ret->init(entry, onSaved)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

// ---------------------------------------------------------------------------
// Index Selector popup -- a scrollable list of saved indexes. Every entry
// gets Use / Modify / Delete; deleting the built-in default goes through
// two separate confirmations warning it's not recommended, since it's
// Geode's own official index.
// ---------------------------------------------------------------------------

class IndexListPopup : public Popup {
protected:
    ScrollLayer* m_scrollLayer = nullptr;

    bool init() {
        if (!Popup::init(340.f, 280.f)) return false;
        applyPopupTheme(this);
        this->setTitle("Index Selector");

        float centerX = m_mainLayer->getContentWidth() / 2;

        m_scrollLayer = ScrollLayer::create({300.f, 190.f});
        m_scrollLayer->setPosition({centerX - 150.f, 60.f});
        m_mainLayer->addChild(m_scrollLayer);

        rebuildList();

        auto addBtn = CCMenuItemExt::createSpriteExtra(
            ButtonSprite::create("+ Add Index", "goldFont.fnt", "GJ_button_01.png", 0.7f),
            [this](auto) {
                AddIndexPopup::create([this] { this->rebuildList(); })->show();
            }
        );
        auto bottomMenu = CCMenu::create();
        bottomMenu->addChild(addBtn);
        bottomMenu->setPosition({centerX, 25.f});
        m_mainLayer->addChild(bottomMenu);

        return true;
    }

    void confirmDeleteDefault() {
        createQuickPopup(
            "Delete Default Index",
            "This is <cr>Geode's own official index</c>. Deleting it is "
            "<cr>not recommended</c> -- most mods and updates are only "
            "guaranteed to be listed there. Continue anyway?",
            "Cancel", "Continue",
            [this](auto, bool confirmed) {
                if (!confirmed) return;
                createQuickPopup(
                    "Are you REALLY sure?",
                    "This will hide the official Geode Index API from your "
                    "list. This is your <cr>last chance</c> to back out -- "
                    "delete it anyway?",
                    "Cancel", "Delete",
                    [this](auto, bool reallyConfirmed) {
                        if (reallyConfirmed) {
                            setDefaultIndexHidden(true);
                            this->rebuildList();
                        }
                    }
                );
            }
        );
    }

    void rebuildList() {
        m_scrollLayer->m_contentLayer->removeAllChildren();

        auto entries = getAllIndexes();
        float contentWidth = m_scrollLayer->getContentSize().width;
        float rowHeight = 34.f;
        float totalHeight = std::max(entries.size() * rowHeight, m_scrollLayer->getContentSize().height);

        float y = totalHeight;
        for (auto const& entry : entries) {
            y -= rowHeight;
            bool isDefault = entry.id.empty();

            auto row = CCMenu::create();
            row->setContentSize({contentWidth, rowHeight});
            row->setAnchorPoint({0.f, 0.f});
            row->setPosition({0.f, y});

            bool isActive = (entry.url == getIndexUrl());
            auto label = CCLabelBMFont::create(
                (isActive ? ("> " + entry.name) : entry.name).c_str(), "bigFont.fnt"
            );
            label->setScale(0.35f);
            label->setAnchorPoint({0.f, 0.5f});
            label->setPosition({4.f, rowHeight / 2});
            row->addChild(label);

            auto useBtn = CCMenuItemExt::createSpriteExtra(
                ButtonSprite::create("Use", "goldFont.fnt", "GJ_button_01.png", 0.5f),
                [this, url = entry.url](auto) {
                    setIndexUrl(url);
                    this->rebuildList();
                }
            );
            useBtn->setPosition({contentWidth - 78.f, rowHeight / 2});
            row->addChild(useBtn);

            auto modifyBtn = CCMenuItemExt::createSpriteExtra(
                CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png"),
                [entry](auto) {
                    ModifyIndexPopup::create(entry, [] {})->show();
                }
            );
            modifyBtn->setScale(0.5f);
            modifyBtn->setPosition({contentWidth - 40.f, rowHeight / 2});
            row->addChild(modifyBtn);

            auto deleteBtn = CCMenuItemExt::createSpriteExtra(
                CCSprite::createWithSpriteFrameName("GJ_deleteIcon_001.png"),
                [this, id = entry.id, isDefault](auto) {
                    if (isDefault) {
                        this->confirmDeleteDefault();
                    } else {
                        auto name = readSetting("custom-index-name-" + id, "this index");
                        createQuickPopup(
                            "Delete Index",
                            fmt::format("Are you sure you want to delete <cy>{}</c>?", name),
                            "Cancel", "Delete",
                            [this, id](auto, bool confirmed) {
                                if (confirmed) {
                                    deleteCustomIndex(id);
                                    this->rebuildList();
                                }
                            }
                        );
                    }
                }
            );
            deleteBtn->setScale(0.5f);
            deleteBtn->setPosition({contentWidth - 15.f, rowHeight / 2});
            row->addChild(deleteBtn);

            m_scrollLayer->m_contentLayer->addChild(row);
        }

        m_scrollLayer->m_contentLayer->setContentSize({contentWidth, totalHeight});
        m_scrollLayer->scrollToTop();
    }

public:
    static IndexListPopup* create() {
        auto ret = new IndexListPopup();
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

// ---------------------------------------------------------------------------
// Filter popup -- the platform/geode/gd browse overrides.
// ---------------------------------------------------------------------------

class FilterPopup : public Popup {
protected:
    TextInput* m_platformInput = nullptr;
    TextInput* m_geodeInput = nullptr;
    TextInput* m_gdInput = nullptr;

    bool init() {
        if (!Popup::init(300.f, 210.f)) return false;
        applyPopupTheme(this);
        this->setTitle("Browse Filters");

        float centerX = m_mainLayer->getContentWidth() / 2;
        float top = m_mainLayer->getContentHeight() - 45.f;

        auto info = CCLabelBMFont::create("Overrides what this device reports\nwhile browsing (blank = auto)", "chatFont.fnt");
        info->setScale(0.4f);
        info->setPosition({centerX, top});
        m_mainLayer->addChild(info);

        m_platformInput = TextInput::create(240.f, "platform, e.g. android64", "chatFont.fnt");
        m_platformInput->setString(getOverridePlatform());
        m_platformInput->setPosition({centerX, top - 35.f});
        m_mainLayer->addChild(m_platformInput);

        m_geodeInput = TextInput::create(240.f, "geode version, e.g. 5.10.1", "chatFont.fnt");
        m_geodeInput->setString(getOverrideGeodeVersion());
        m_geodeInput->setPosition({centerX, top - 65.f});
        m_mainLayer->addChild(m_geodeInput);

        m_gdInput = TextInput::create(240.f, "gd version, e.g. 2.2081", "chatFont.fnt");
        m_gdInput->setString(getOverrideGDVersion());
        m_gdInput->setPosition({centerX, top - 95.f});
        m_mainLayer->addChild(m_gdInput);

        auto applyBtn = CCMenuItemExt::createSpriteExtra(
            ButtonSprite::create("Apply", "goldFont.fnt", "GJ_button_02.png", 0.6f),
            [this](auto) {
                auto platform = m_platformInput->getString();
                auto geodeVer = m_geodeInput->getString();
                auto gdVer = m_gdInput->getString();
                log::debug(
                    "[OpenGeode] FilterPopup Apply -> platform='{}' geode='{}' gd='{}'",
                    platform, geodeVer, gdVer
                );
                setOverridePlatform(platform);
                setOverrideGeodeVersion(geodeVer);
                setOverrideGDVersion(gdVer);
                FLAlertLayer::create("Success", "Filter overrides updated!", "OK")->show();
                this->onClose(nullptr);
            }
        );
        auto menu = CCMenu::create();
        menu->addChild(applyBtn);
        menu->setPosition({centerX, 20.f});
        m_mainLayer->addChild(menu);

        return true;
    }

public:
    static FilterPopup* create() {
        auto ret = new FilterPopup();
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

// ---------------------------------------------------------------------------
// Filter button appearance. GE_button_05.png (default) and GE_button_02.png
// (active) are now bundled as this mod's own resources -- referenced via
// the `_spr` literal, which namespaces them under this mod's own id at
// compile time, the same way any of our own sprites would be. The
// Geometry-Dash-theme case uses GJ_button_01.png plain, with no prefix at
// all, since that's a standard public GD texture already resident in the
// game's own spritesheet, not something any mod needs to bundle.
// ---------------------------------------------------------------------------

CCNode* buildFilterButtonSprite() {
    std::string spriteName;
    if (isFilterActive()) {
        spriteName = "GE_button_02.png"_spr;
    } else if (useDarkTheme()) {
        spriteName = "GE_button_05.png"_spr;
    } else {
        spriteName = "GJ_button_01.png";
    }

    log::debug("[OpenGeode] filter button sprite frame: '{}'", spriteName);

    auto bgSprite = CCSprite::createWithSpriteFrameName(spriteName.c_str());
    if (!bgSprite) {
        log::debug("[OpenGeode] filter button sprite frame '{}' returned null, using blank fallback", spriteName);
        return CCSprite::create();
    }

    if (auto icon = CCSprite::createWithSpriteFrameName("geode.loader/geode-logo-outline-gold.png")) {
        icon->setPosition({bgSprite->getContentSize().width / 2, bgSprite->getContentSize().height / 2});
        icon->setScale(0.8f);
        bgSprite->addChild(icon);
    } else {
        log::debug("[OpenGeode] icon frame 'geode.loader/geode-logo-outline-gold.png' returned null");
    }
    return bgSprite;
}

// ---------------------------------------------------------------------------
// Hook ModsLayer. Both buttons are (re-)inserted on a short recurring timer
// rather than once in modify() -- each tab (Download/Featured/Recent/
// Installed) builds its own list frame lazily, so a one-shot injection in
// modify() only ever reaches whichever tab happened to exist at that
// moment. The timer is cheap (just ID lookups + an early-out) and every
// insertion is guarded by a "does this button already exist here" check,
// so it's safe to call repeatedly.
// ---------------------------------------------------------------------------

class $nodeModify(IndexSwitcherModsLayer, ModsLayer) {
    void ensureIndexSwitcherButton() {
        auto actionsMenu = typeinfo_cast<CCMenu*>(getChildByID("actions-menu"));
        if (!actionsMenu) {
            log::debug("[OpenGeode] ensureIndexSwitcherButton: 'actions-menu' not found on ModsLayer");
            return;
        }
        if (actionsMenu->getChildByID("index-switcher-button"_spr)) return; // already present, nothing to do

        log::debug("[OpenGeode] ensureIndexSwitcherButton: adding button to actions-menu");
        auto indexBtn = CCMenuItemExt::createSpriteExtra(
            CircleButtonSprite::createWithSpriteFrameName(
                "geode.loader/geode-logo.png",
                0.85f,
                CircleBaseColor::Blue
            ),
            [](auto) {
                IndexListPopup::create()->show();
            }
        );
        indexBtn->setScale(0.8f);
        indexBtn->setID("index-switcher-button"_spr);

        actionsMenu->addChild(indexBtn);
        actionsMenu->updateLayout();
    }

    void ensureFilterButton() {
        auto listFrame = getChildByID("mod-list-frame");
        if (!listFrame) {
            log::debug("[OpenGeode] ensureFilterButton: 'mod-list-frame' not found");
            return;
        }
        auto modList = listFrame->getChildByID("ModList");
        if (!modList) {
            log::debug("[OpenGeode] ensureFilterButton: 'ModList' not found under mod-list-frame");
            return;
        }
        auto topContainer = modList->getChildByID("top-container");
        if (!topContainer) {
            log::debug("[OpenGeode] ensureFilterButton: 'top-container' not found under ModList");
            return;
        }
        auto searchMenu = topContainer->getChildByID("search-menu");
        if (!searchMenu) {
            log::debug("[OpenGeode] ensureFilterButton: 'search-menu' not found under top-container");
            return;
        }
        auto filtersMenu = typeinfo_cast<CCMenu*>(searchMenu->getChildByID("search-filters-menu"));
        if (!filtersMenu) {
            log::debug("[OpenGeode] ensureFilterButton: 'search-filters-menu' not found (or not a CCMenu) under search-menu");
            return;
        }
        if (filtersMenu->getChildByID("index-filter-button"_spr)) return; // already present, nothing to do

        log::debug("[OpenGeode] ensureFilterButton: adding button to search-filters-menu");
        auto filterBtn = CCMenuItemExt::createSpriteExtra(
            buildFilterButtonSprite(),
            [](auto) {
                FilterPopup::create()->show();
            }
        );
        filterBtn->setID("index-filter-button"_spr);

        // A very low z-order sorts this to the front of the menu's layout
        // instead of appending it at the end.
        filtersMenu->addChild(filterBtn, -100);
        filtersMenu->updateLayout();
    }

    void recheckButtons(float) {
        ensureIndexSwitcherButton();
        ensureFilterButton();
    }

    void modify() {
        ensureIndexSwitcherButton();
        ensureFilterButton();
        this->schedule(schedule_selector(IndexSwitcherModsLayer::recheckButtons), 0.25f);
    }
};

$on_mod(Loaded) {
    web::WebRequestInterceptEvent().listen(
        [](std::string_view id, web::WebRequest& req) {
            std::string givenUrl = req.getUrl().data();
            log::debug("[OpenGeode] intercepted request, base url = '{}'", givenUrl);

            {
                std::string dump;
                for (auto const& [k, v] : req.getUrlParams()) dump += k + "=" + v + "; ";
                log::debug("[OpenGeode] params at interception time: {}", dump.empty() ? "(none)" : dump);
            }

            std::string targetIndex = getIndexUrl();
            if (string::contains(givenUrl, "api.geode-sdk.org")) {
                givenUrl = string::replace(givenUrl, "https://api.geode-sdk.org", targetIndex);
                req.url(givenUrl);
                log::debug("[OpenGeode] rewrote host -> '{}'", givenUrl);
            }

            auto platform = getOverridePlatform();
            auto geodeVer = getOverrideGeodeVersion();
            auto gdVer = getOverrideGDVersion();
            log::debug(
                "[OpenGeode] saved overrides -> platform='{}' geode='{}' gd='{}'",
                platform, geodeVer, gdVer
            );

            // Apply browse overrides directly on the request's own param
            // store. `platforms` is what the mods-list endpoints (downloads/
            // featured/recent tabs, mod updates) use; `platform` (singular)
            // is what the loader-versions endpoint uses -- both get
            // overridden when present. Only replace a param that's already
            // there, so endpoints that don't use a given key never get one
            // invented for them.
            if (!platform.empty()) {
                bool hasPlatforms = req.getUrlParams().count("platforms") > 0;
                bool hasPlatform = req.getUrlParams().count("platform") > 0;
                log::debug("[OpenGeode] platform override: hasPlatforms={} hasPlatform={}", hasPlatforms, hasPlatform);
                if (hasPlatforms) req.param("platforms", platform);
                if (hasPlatform) req.param("platform", platform);
            }
            if (!geodeVer.empty()) {
                bool hasGeode = req.getUrlParams().count("geode") > 0;
                log::debug("[OpenGeode] geode override: hasGeode={}", hasGeode);
                if (hasGeode) req.param("geode", geodeVer);
            }
            if (!gdVer.empty()) {
                bool hasGd = req.getUrlParams().count("gd") > 0;
                log::debug("[OpenGeode] gd override: hasGd={}", hasGd);
                if (hasGd) req.param("gd", gdVer);
            }

            {
                std::string dump;
                for (auto const& [k, v] : req.getUrlParams()) dump += k + "=" + v + "; ";
                log::debug("[OpenGeode] params after overrides: {}", dump.empty() ? "(none)" : dump);
            }

            return ListenerResult::Propagate;
        }, Priority::Stub
    ).leak();
}