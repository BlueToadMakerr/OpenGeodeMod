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
        std::ifstream file(settingPath(key), std::ios::binary);
        if (!file.is_open()) return fallback;
        std::ostringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }

    void writeSetting(std::string const& key, std::string const& value) {
        std::ofstream file(settingPath(key), std::ios::binary | std::ios::trunc);
        file << value;
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

// Replaces the value of `key` in a URL's query string, but only if that key
// is already present. We deliberately never *add* a param that wasn't
// there -- e.g. an endpoint that doesn't take `geode` shouldn't suddenly
// get one just because an override is set.
std::string overrideQueryParam(std::string url, std::string const& key, std::string const& value) {
    std::string needle = key + "=";
    size_t start = std::string::npos;

    if (auto pos = url.find("?" + needle); pos != std::string::npos) {
        start = pos + 1;
    } else if (auto pos = url.find("&" + needle); pos != std::string::npos) {
        start = pos + 1;
    }
    if (start == std::string::npos) return url;

    size_t valueStart = start + needle.size();
    size_t valueEnd = url.find('&', valueStart);
    if (valueEnd == std::string::npos) valueEnd = url.size();

    return url.substr(0, valueStart) + value + url.substr(valueEnd);
}

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
                setOverridePlatform(m_platformInput->getString());
                setOverrideGeodeVersion(m_geodeInput->getString());
                setOverrideGDVersion(m_gdInput->getString());
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
// Filter button appearance: GE_button_05 by default (Geode theme),
// GE_button_01 when the loader's theme is Geometry Dash, and GE_button_02
// whenever an override is currently active. GE_* textures are loose files
// under geode.loader's own resource folder rather than a spritesheet, so
// they need the "geode.loader/" namespace prefix -- same convention as the
// icon below -- to resolve via the sprite frame cache at all.
// ---------------------------------------------------------------------------

CCNode* buildFilterButtonSprite() {
    std::string bg = isFilterActive()
        ? "geode.loader/GE_button_02.png"
        : (useDarkTheme() ? "geode.loader/GE_button_05.png" : "geode.loader/GE_button_01.png");

    auto bgSprite = CCSprite::createWithSpriteFrameName(bg.c_str());
    if (!bgSprite) return CCSprite::create(); // fall back to an empty node rather than crash if the frame's missing

    if (auto icon = CCSprite::createWithSpriteFrameName("geode.loader/geode-logo-outline-gold.png")) {
        icon->setPosition({bgSprite->getContentSize().width / 2, bgSprite->getContentSize().height / 2});
        icon->setScale(0.8f);
        bgSprite->addChild(icon);
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
        if (!actionsMenu || actionsMenu->getChildByID("index-switcher-button"_spr)) return;

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
        if (!listFrame) return;
        auto modList = listFrame->getChildByID("ModList");
        if (!modList) return;
        auto topContainer = modList->getChildByID("top-container");
        if (!topContainer) return;
        auto searchMenu = topContainer->getChildByID("search-menu");
        if (!searchMenu) return;
        auto filtersMenu = typeinfo_cast<CCMenu*>(searchMenu->getChildByID("search-filters-menu"));
        if (!filtersMenu || filtersMenu->getChildByID("index-filter-button"_spr)) return;

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
            std::string targetIndex = getIndexUrl();

            if (string::contains(givenUrl, "api.geode-sdk.org")) {
                givenUrl = string::replace(givenUrl, "https://api.geode-sdk.org", targetIndex);
            }

            // Apply browse overrides. `platforms` is what the mods-list
            // endpoints (downloads/featured/recent tabs, mod updates) use;
            // `platform` (singular) is what the loader-versions endpoint
            // uses -- both get rewritten when present. Only params Geode
            // already put in the URL get touched, so unrelated endpoints
            // are untouched.
            if (auto platform = getOverridePlatform(); !platform.empty()) {
                givenUrl = overrideQueryParam(givenUrl, "platforms", platform);
                givenUrl = overrideQueryParam(givenUrl, "platform", platform);
            }
            if (auto geodeVer = getOverrideGeodeVersion(); !geodeVer.empty()) {
                givenUrl = overrideQueryParam(givenUrl, "geode", geodeVer);
            }
            if (auto gdVer = getOverrideGDVersion(); !gdVer.empty()) {
                givenUrl = overrideQueryParam(givenUrl, "gd", gdVer);
            }

            req.url(givenUrl);

            return ListenerResult::Propagate;
        }, Priority::Stub
    ).leak();
}