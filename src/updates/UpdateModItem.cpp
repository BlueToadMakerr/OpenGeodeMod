#include "UpdateModItem.hpp"

#include "UpdateSourcePopup.hpp"
#include "../InstalledMods.hpp"
#include "../PopupSectionUtils.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/SimpleAxisLayout.hpp>
#include <Geode/binding/Slider.hpp>
#include <Geode/utils/ColorProvider.hpp>

using namespace geode::prelude;

namespace opengeode {
namespace {

std::string cleanVersion(std::string value) {
    if (!value.empty() && value.front() == 'v') value.erase(value.begin());
    return value;
}

CCNode* makeTag(std::string text, ccColor3B labelColor, ccColor3B bgColor) {
    auto label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    label->setScale(.20f);
    label->setColor(labelColor);
    auto tag = NineSlice::create("square02_001.png");
    tag->setContentSize({label->getScaledContentWidth() + 10.f, 14.f});
    tag->setOpacity(175);
    tag->setColor(bgColor);
    tag->addChildAtPosition(label, Anchor::Center);
    return tag;
}

CCSprite* createActionButtonSprite(char const* text) {
    auto sprite = ButtonSprite::create(text, "bigFont.fnt", getButtonTexture("GJ_button_01.png"), .42f);
    return sprite;
}

bool isCompleted(IndexUpdateInfo const& update) {
    return completedIndexUpdates().contains(indexUpdateKey(update));
}

std::vector<IndexUpdateInfo> sortedSources(std::vector<IndexUpdateInfo> sources) {
    std::sort(sources.begin(), sources.end(), [](auto const& a, auto const& b) {
        auto av = parseUpdateVersion(a.newVersion);
        auto bv = parseUpdateVersion(b.newVersion);
        if (av != bv) return av > bv;
        return a.indexName < b.indexName;
    });
    return sources;
}

void showUpdateConfirmation(IndexUpdateInfo update, std::function<void(bool)> callback) {
    auto installed = getInstalledModSource(update.modID);
    std::string warning;

    if (!installed) {
        warning = "\n\n<cr>The original index this mod was installed from is unknown.</c>";
    }
    else if (installed->indexId != update.indexID) {
        warning = fmt::format(
            "\n\n<cr>Originally installed from <cy>{}</c>.</c>\n"
            "You are updating it from <cy>{}</c> instead.",
            installed->indexName.empty() ? installed->indexId : installed->indexName,
            update.indexName
        );
    }

    createQuickPopup(
        "Update Mod",
        fmt::format(
            "Update <cy>{}</c> from <cg>{}</c>?\n"
            "Version: <cy>v{}</c> -> <cg>v{}</c>{}",
            update.modName,
            update.indexName,
            cleanVersion(update.currentVersion),
            cleanVersion(update.newVersion),
            warning
        ),
        "Cancel",
        "Update",
        [callback = std::move(callback)](auto, bool confirmed) mutable {
            if (callback) callback(confirmed);
        },
        true
    );
}

} // namespace

bool UpdateModItem::init(IndexUpdateInfo update, std::vector<IndexUpdateInfo> sources) {
    if (!CCNode::init()) return false;
    m_update = std::move(update);
    m_sources = sortedSources(std::move(sources));
    m_mod = Loader::get()->getInstalledMod(m_update.modID);

    setContentSize({330.f, 58.f});
    ignoreAnchorPointForPosition(false);
    setAnchorPoint({.5f, .5f});
    setID("update-mod-item");

    auto bg = NineSlice::create("square02b_001.png");
    bg->setContentSize(getContentSize());
    bg->setOpacity(95);
    bg->setColor({0, 0, 0});
    addChildAtPosition(bg, Anchor::Center);

    CCNode* logo = nullptr;
    if (m_mod) logo = geode::createModLogo(m_mod);
    if (!logo) logo = createServerModLogo(m_update.modID);
    if (!logo) logo = CCSprite::createWithSpriteFrameName("GJ_folderIcon_001.png");
    logo->setID("mod-logo");
    logo->setScale(.68f);
    addChildAtPosition(logo, Anchor::Left, {25.f, 0.f});

    auto info = CCNode::create();
    info->setID("info-container");
    info->setContentSize({168.f, 52.f});
    info->setAnchorPoint({0.f, .5f});
    addChildAtPosition(info, Anchor::Left, {50.f, 0.f});

    auto title = CCLabelBMFont::create(m_update.modName.c_str(), "bigFont.fnt");
    title->setID("mod-name");
    title->setAnchorPoint({0.f, .5f});
    title->setScale(.36f);
    title->setColor(ccWHITE);
    title->limitLabelWidth(108.f, .36f, .17f);
    title->setPosition({0.f, 44.f});
    info->addChild(title);

    auto version = CCLabelBMFont::create(fmt::format("v{} -> v{}", cleanVersion(m_update.currentVersion), cleanVersion(m_update.newVersion)).c_str(), "bigFont.fnt");
    version->setID("version-change");
    version->setAnchorPoint({0.f, .5f});
    version->setScale(.24f);
    version->setColor({102, 190, 255});
    version->setPosition({title->getScaledContentWidth() + 4.f, 44.f});
    info->addChild(version);

    auto developers = CCLabelBMFont::create("", "goldFont.fnt");
    developers->setAnchorPoint({0.f, .5f});
    developers->setScale(.30f);
    developers->setColor(ccWHITE);
    if (m_mod) developers->setString(ModMetadata::formatDeveloperDisplayString(m_mod->getMetadata().getDevelopers()).c_str());
    else developers->setString("Unknown developer");
    developers->limitLabelWidth(168.f, .30f, .18f);
    developers->setPosition({0.f, 27.f});
    info->addChild(developers);

    bool outdated = m_update.outdated;
    bool restartRequired = isCompleted(m_update);
    auto gameVersion = m_mod ? m_mod->getMetadata().getGameVersion() : std::nullopt;

    if (outdated || restartRequired || m_update.disabled) {
        m_tags = CCNode::create();
        m_tags->setID("status-tags");
        m_tags->setContentSize({164.f, 14.f});
        m_tags->setLayout(SimpleRowLayout::create()->setMainAxisAlignment(MainAxisAlignment::Start)->setGap(3.f));
        if (outdated) {
            auto text = gameVersion ? fmt::format("Outdated (GD {})", *gameVersion) : "Outdated";
            m_tags->addChild(makeTag(text, {245, 153, 245}, {156, 123, 163}));
        }
        if (restartRequired) m_tags->addChild(makeTag("Restart Required", {153, 245, 245}, {123, 156, 163}));
        if (m_update.disabled) m_tags->addChild(makeTag("Disabled", {255, 170, 170}, {165, 95, 95}));
        m_tags->updateLayout();
        m_tags->setAnchorPoint({0.f, .5f});
        m_tags->setPosition({0.f, 10.f});
        info->addChild(m_tags);
    } else {
        auto descriptionBG = NineSlice::create("square02b_001.png");
        descriptionBG->setID("description-bg");
        descriptionBG->setContentSize({164.f, 13.f});
        descriptionBG->setOpacity(75);
        descriptionBG->setColor({0, 0, 0});
        descriptionBG->setAnchorPoint({0.f, .5f});
        descriptionBG->setPosition({0.f, 12.f});
        info->addChild(descriptionBG);
        auto descriptionText = m_mod ? m_mod->getMetadata().getDescription() : std::nullopt;
        auto description = CCLabelBMFont::create(descriptionText ? descriptionText->c_str() : "[No Description Provided]", "chatFont.fnt");
        description->setColor(descriptionText ? ccWHITE : ccGRAY);
        description->setAnchorPoint({0.f, .5f});
        limitNodeWidth(description, 154.f, 1.f, .065f);
        description->setPosition({5.f, 6.f});
        descriptionBG->addChild(description);
        m_description = descriptionBG;
    }

    auto controls = CCMenu::create();
    controls->setID("controls");
    controls->setAnchorPoint({1.f, .5f});
    controls->setLayout(SimpleRowLayout::create()->setMainAxisAlignment(MainAxisAlignment::End)->setGap(2.f));

    auto updateIcon = CCSprite::createWithSpriteFrameName("geode.loader/update.png");
    auto updateSprite = CircleButtonSprite::create(
        updateIcon,
        isGeodeTheme() ? CircleBaseColor::DarkPurple : CircleBaseColor::Green,
        CircleBaseSize::Medium
    );
    updateSprite->setScale(.58f);
    m_updateButton = CCMenuItemSpriteExtra::create(updateSprite, this, menu_selector(UpdateModItem::onUpdate));
    m_updateButton->setID("update-button");
    controls->addChild(m_updateButton);

    auto viewSprite = createActionButtonSprite("View");
    auto view = CCMenuItemSpriteExtra::create(viewSprite, this, menu_selector(UpdateModItem::onView));
    view->setID("view-button");
    controls->addChild(view);

    auto controlsWidth =
        m_updateButton->getScaledContentWidth() +
        2.f +
        view->getScaledContentWidth();
    controls->setContentSize({controlsWidth, 20.f});
    controls->updateLayout();
    addChildAtPosition(controls, Anchor::Right, {-4.f, 0.f});
    return true;
}

void UpdateModItem::onView(CCObject*) { if (m_mod) openInfoPopup(m_mod); }

void UpdateModItem::refreshStatusTags(bool restartRequired) {
    auto info = getChildByID("info-container");
    if (!info) return;

    if (m_tags) {
        m_tags->removeFromParentAndCleanup(true);
        m_tags = nullptr;
    }

    bool outdated = m_update.outdated;
    bool disabled = m_update.disabled;
    if (!outdated && !disabled && !restartRequired) return;

    auto tags = CCNode::create();
    tags->setID("status-tags");
    tags->setContentSize({164.f, 14.f});
    tags->setLayout(SimpleRowLayout::create()->setMainAxisAlignment(MainAxisAlignment::Start)->setGap(3.f));

    if (outdated) {
        auto gameVersion = m_mod ? m_mod->getMetadata().getGameVersion() : std::nullopt;
        auto text = gameVersion ? fmt::format("Outdated (GD {})", *gameVersion) : "Outdated";
        tags->addChild(makeTag(text, {245, 153, 245}, {156, 123, 163}));
    }
    if (disabled) tags->addChild(makeTag("Disabled", {255, 170, 170}, {165, 95, 95}));
    if (restartRequired) tags->addChild(makeTag("Restart Required", {153, 245, 245}, {123, 156, 163}));

    tags->updateLayout();
    tags->setAnchorPoint({0.f, .5f});
    tags->setPosition({0.f, 10.f});
    if (m_description) {
        m_description->removeFromParentAndCleanup(true);
        m_description = nullptr;
    }
    info->addChild(tags);
    m_tags = tags;
}

void UpdateModItem::onUpdate(CCObject*) {
    if (m_sources.size() <= 1) {
        if (!m_sources.empty()) {
            auto update = m_sources.front();
            showUpdateConfirmation(update, [this, update](bool confirmed) { if (confirmed) startUpdate(update); });
        }
        return;
    }
    showSourcePicker();
}

void UpdateModItem::showSourcePicker() {
    UpdateSourcePopup::create(m_sources, [this](IndexUpdateInfo selected) {
        showUpdateConfirmation(selected, [this, selected](bool confirmed) { if (confirmed) startUpdate(selected); });
    })->show();
}

void UpdateModItem::startUpdate(IndexUpdateInfo update) {
    if (!m_updateButton) return;
    m_updateButton->setVisible(false);
    m_updateButton->setEnabled(false);
    m_progress = Slider::create(nullptr, nullptr);
    m_progress->setID("update-progress");
    m_progress->m_touchLogic->m_thumb->setVisible(false);
    m_progress->setScale(.75f);
    m_progress->setValue(0.f);
    m_progress->setContentSize({120.f, 6.f});
    if (m_tags) m_tags->setVisible(false);
    if (m_description) m_description->setVisible(false);
    auto info = getChildByID("info-container");
    if (info) {
        info->addChild(m_progress, 5);
        m_progress->setPosition({82.f, 10.f});
    }
    downloadIndexUpdate(update, [this, update](bool success) { finishUpdate(update, success); }, [this](float progress) { setProgress(progress); });
}

void UpdateModItem::setProgress(float progress) { if (m_progress) m_progress->setValue(std::clamp(progress, 0.f, 1.f)); }

void UpdateModItem::finishUpdate(IndexUpdateInfo const& update, bool success) {
    if (m_progress) { m_progress->removeFromParentAndCleanup(true); m_progress = nullptr; }
    if (!success) {
        if (m_tags) m_tags->setVisible(true);
        if (m_description) m_description->setVisible(true);
        if (m_updateButton) { m_updateButton->setVisible(true); m_updateButton->setEnabled(true); }
        return;
    }
    m_updated = true;
    m_update = update;
    refreshStatusTags(true);
    if (m_updateButton) { m_updateButton->setVisible(true); m_updateButton->setEnabled(true); }
}

UpdateModItem* UpdateModItem::createProgressTest() {
    IndexUpdateInfo test;
    test.modID = "devtools.progress-test";
    test.modName = "DevTools Progress Test";
    test.currentVersion = "1.0.0";
    test.newVersion = "1.0.1";

    auto ret = new UpdateModItem();
    if (ret && ret->init(test, {test})) {
        ret->m_updateButton->setVisible(false);
        ret->m_updateButton->setEnabled(false);
        if (ret->m_tags) ret->m_tags->setVisible(false);
        if (ret->m_description) ret->m_description->setVisible(false);
        ret->m_progress = Slider::create(nullptr, nullptr);
        ret->m_progress->setID("update-progress-test");
        ret->m_progress->m_touchLogic->m_thumb->setVisible(false);
        ret->m_progress->setScale(.75f);
        ret->m_progress->setValue(.5f);
        ret->m_progress->setContentSize({120.f, 6.f});
        if (auto info = ret->getChildByID("info-container")) {
            info->addChild(ret->m_progress, 5);
            ret->m_progress->setPosition({82.f, 10.f});
        }
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

UpdateModItem* UpdateModItem::create(IndexUpdateInfo update, std::vector<IndexUpdateInfo> sources) {
    auto ret = new UpdateModItem();
    if (ret && ret->init(std::move(update), std::move(sources))) { ret->autorelease(); return ret; }
    delete ret;
    return nullptr;
}

} // namespace opengeode
