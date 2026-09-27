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
    label->setScale(.22f);
    label->setColor(labelColor);
    auto tag = NineSlice::create("square02_001.png");
    tag->setContentSize({label->getScaledContentWidth() + 12.f, 16.f});
    tag->setOpacity(175);
    tag->setColor(bgColor);
    tag->addChildAtPosition(label, Anchor::Center);
    return tag;
}

CCSprite* createActionButtonSprite(char const* text) {
    return ButtonSprite::create(text, "bigFont.fnt", getButtonTexture("GJ_button_01.png"), .50f);
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
    if (!installed) warning = "\n\n<cr>The original index this mod was installed from is unknown.</c>";
    else if (installed->indexId != update.indexID) {
        warning = fmt::format("\n\n<cr>Originally installed from <cy>{}</c>.</c>\nYou are updating it from <cy>{}</c> instead.", installed->indexName.empty() ? installed->indexId : installed->indexName, update.indexName);
    }
    createQuickPopup("Update Mod", fmt::format("Update <cy>{}</c> from <cg>{}</c>?\nVersion: <cy>v{}</c> -> <cg>v{}</c>{}", update.modName, update.indexName, cleanVersion(update.currentVersion), cleanVersion(update.newVersion), warning), "Cancel", "Update", [callback = std::move(callback)](auto, bool confirmed) mutable { if (callback) callback(confirmed); }, true);
}

} // namespace

bool UpdateModItem::init(IndexUpdateInfo update, std::vector<IndexUpdateInfo> sources) {
    if (!CCNode::init()) return false;
    m_update = std::move(update);
    m_sources = sortedSources(std::move(sources));
    m_mod = Loader::get()->getInstalledMod(m_update.modID);

    setContentSize({350.f, 88.f});
    ignoreAnchorPointForPosition(false);
    setAnchorPoint({.5f, .5f});
    setID("update-mod-item");

    auto bg = NineSlice::create("square02b_001.png");
    bg->setContentSize(getContentSize());
    bg->setOpacity(95);
    addChildAtPosition(bg, Anchor::Center);

    CCNode* logo = nullptr;
    if (m_mod) logo = geode::createModLogo(m_mod);
    if (!logo) logo = createServerModLogo(m_update.modID);
    if (!logo) logo = CCSprite::createWithSpriteFrameName("GJ_folderIcon_001.png");
    logo->setID("mod-logo");
    logo->setScale(.58f);
    addChildAtPosition(logo, Anchor::Left, {28.f, 0.f});

    auto info = CCNode::create();
    info->setID("info-container");
    info->setContentSize({190.f, 74.f});
    info->setAnchorPoint({0.f, .5f});
    addChildAtPosition(info, Anchor::Left, {52.f, 0.f});

    auto title = CCLabelBMFont::create(fmt::format("{}  v{} -> v{}", m_update.modName, cleanVersion(m_update.currentVersion), cleanVersion(m_update.newVersion)).c_str(), "bigFont.fnt");
    title->setID("mod-name");
    title->setAnchorPoint({0.f, .5f});
    title->setScale(.42f);
    title->setColor(ccWHITE);
    title->limitLabelWidth(188.f, .42f, .20f);
    title->setPosition({0.f, 63.f});
    info->addChild(title);

    auto developers = CCLabelBMFont::create("", "goldFont.fnt");
    developers->setAnchorPoint({0.f, .5f});
    developers->setScale(.24f);
    developers->setColor(ccWHITE);
    if (m_mod) developers->setString(ModMetadata::formatDeveloperDisplayString(m_mod->getMetadata().getDevelopers()).c_str());
    else developers->setString("Unknown developer");
    developers->limitLabelWidth(188.f, .24f, .15f);
    developers->setPosition({0.f, 39.f});
    info->addChild(developers);

    bool outdated = m_update.outdated;
    bool restartRequired = isCompleted(m_update);
    auto gameVersion = m_mod ? m_mod->getMetadata().getGameVersion() : std::nullopt;

    if (outdated || restartRequired) {
        m_tags = CCNode::create();
        m_tags->setID("status-tags");
        m_tags->setContentSize({188.f, 18.f});
        m_tags->setLayout(SimpleRowLayout::create()->setMainAxisAlignment(MainAxisAlignment::Start)->setGap(4.f));
        if (outdated) {
            auto text = gameVersion ? fmt::format("Outdated (GD {})", *gameVersion) : "Outdated";
            m_tags->addChild(makeTag(text, {245, 153, 245}, {156, 123, 163}));
        }
        if (restartRequired) m_tags->addChild(makeTag("Restart Required", {153, 245, 245}, {123, 156, 163}));
        m_tags->updateLayout();
        m_tags->setAnchorPoint({0.f, .5f});
        m_tags->setPosition({0.f, 19.f});
        info->addChild(m_tags);
    } else {
        auto descriptionBG = NineSlice::create("square02b_001.png");
        descriptionBG->setID("description-bg");
        descriptionBG->setContentSize({188.f, 17.f});
        descriptionBG->setOpacity(75);
        descriptionBG->setAnchorPoint({0.f, .5f});
        descriptionBG->setPosition({0.f, 19.f});
        info->addChild(descriptionBG);
        auto descriptionText = m_mod ? m_mod->getMetadata().getDescription() : std::nullopt;
        auto description = CCLabelBMFont::create(descriptionText ? descriptionText->c_str() : "[No Description Provided]", "chatFont.fnt");
        description->setColor(descriptionText ? ccWHITE : ccGRAY);
        description->setAnchorPoint({0.f, .5f});
        limitNodeWidth(description, 178.f, 1.f, .085f);
        description->setPosition({6.f, 8.5f});
        descriptionBG->addChild(description);
        m_description = descriptionBG;
    }

    auto controls = CCMenu::create();
    controls->setID("controls");
    controls->setContentSize({112.f, 42.f});
    controls->setLayout(SimpleRowLayout::create()->setMainAxisAlignment(MainAxisAlignment::End)->setGap(5.f));
    addChildAtPosition(controls, Anchor::Right, {-7.f, 0.f});
    auto updateSprite = CircleButtonSprite::create(CCSprite::createWithSpriteFrameName("update.png"_spr), CircleBaseColor::Green, CircleBaseSize::Small);
    m_updateButton = CCMenuItemSpriteExtra::create(updateSprite, this, menu_selector(UpdateModItem::onUpdate));
    m_updateButton->setID("update-button");
    controls->addChild(m_updateButton);

    if (m_mod && !m_mod->isInternal()) {
        m_enableToggle = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(UpdateModItem::onEnable), .8f);
        m_enableToggle->setID("enable-toggler");
        m_enableToggle->setScale(.75f);
        m_enableToggle->toggle(!m_mod->isOrWillBeEnabled());
        if (outdated) m_enableToggle->setOpacity(100);
        controls->addChild(m_enableToggle);
    }

    auto viewSprite = createActionButtonSprite("View");
    auto view = CCMenuItemSpriteExtra::create(viewSprite, this, menu_selector(UpdateModItem::onView));
    view->setID("view-button");
    controls->addChild(view);
    controls->updateLayout();
    return true;
}

void UpdateModItem::onView(CCObject*) { if (m_mod) openInfoPopup(m_mod); }
void UpdateModItem::onEnable(CCObject*) {
    if (!m_mod || m_mod->isInternal()) return;
    if (m_mod->isOrWillBeEnabled()) (void)m_mod->disable();
    else (void)m_mod->enable();
    addRestartRequiredTag();
}

void UpdateModItem::addRestartRequiredTag() {
    auto info = getChildByID("info-container");
    if (!info || info->getChildByID("status-tags")) return;
    auto tags = CCNode::create();
    tags->setID("status-tags");
    tags->setContentSize({188.f, 18.f});
    tags->setLayout(SimpleRowLayout::create()->setMainAxisAlignment(MainAxisAlignment::Start)->setGap(4.f));
    tags->addChild(makeTag("Restart Required", {153, 245, 245}, {123, 156, 163}));
    tags->updateLayout();
    tags->setAnchorPoint({0.f, .5f});
    tags->setPosition({0.f, 19.f});
    if (m_description) m_description->removeFromParentAndCleanup(true);
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
    m_progress->setScale(1.2f);
    m_progress->setValue(0.f);
    m_progress->setContentSize({26.f, 10.f});
    if (auto parent = m_updateButton->getParent()) { parent->addChild(m_progress, 5); m_progress->setPosition(m_updateButton->getPosition()); }
    downloadIndexUpdate(update, [this, update](bool success) { finishUpdate(update, success); }, [this](float progress) { setProgress(progress); });
}
void UpdateModItem::setProgress(float progress) { if (m_progress) m_progress->setValue(std::clamp(progress, 0.f, 1.f)); }
void UpdateModItem::finishUpdate(IndexUpdateInfo const& update, bool success) {
    if (m_progress) { m_progress->removeFromParentAndCleanup(true); m_progress = nullptr; }
    if (!success) { if (m_updateButton) { m_updateButton->setVisible(true); m_updateButton->setEnabled(true); } return; }
    m_updated = true;
    if (m_updateButton) m_updateButton->setVisible(false);
    addRestartRequiredTag();
    auto check = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
    if (check) { check->setID("updated-check"); check->setScale(.38f); check->setPosition({getContentWidth() - 18.f, -20.f}); addChild(check, 8); }
}

UpdateModItem* UpdateModItem::create(IndexUpdateInfo update, std::vector<IndexUpdateInfo> sources) {
    auto ret = new UpdateModItem();
    if (ret && ret->init(std::move(update), std::move(sources))) { ret->autorelease(); return ret; }
    delete ret; return nullptr;
}

} // namespace opengeode
