#include "UpdateModItem.hpp"

#include "UpdateSourcePopup.hpp"
#include "../InstalledMods.hpp"
#include "../PopupSectionUtils.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/SimpleAxisLayout.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/binding/Slider.hpp>

using namespace geode::prelude;

namespace opengeode {
namespace {

std::string cleanVersion(std::string value) {
    if (!value.empty() && value.front() == 'v') value.erase(value.begin());
    return value;
}

CCNode* makeTag(std::string text, ccColor3B color) {
    auto label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    label->setScale(.22f);
    label->setColor(ccWHITE);

    auto tag = NineSlice::create("square02_001.png");
    tag->setContentSize({label->getScaledContentWidth() + 12.f, 16.f});
    tag->setOpacity(135);
    tag->setColor(color);
    tag->addChildAtPosition(label, Anchor::Center);
    return tag;
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

void showUpdateConfirmation(
    IndexUpdateInfo update,
    std::function<void(bool)> callback
) {
    auto installed = getInstalledModSource(update.modID);
    std::string warning;

    if (!installed) {
        warning = "\n\n<cr>The original index this mod was installed from is unknown.</c>";
    } else if (installed->indexId != update.indexID) {
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

    setContentSize({350.f, 112.f});
    ignoreAnchorPointForPosition(false);
    setAnchorPoint({.5f, .5f});
    setID("update-mod-item");

    auto bg = NineSlice::create("square02b_001.png");
    bg->setContentSize(getContentSize());
    bg->setOpacity(95);
    addChildAtPosition(bg, Anchor::Center);

    CCNode* logo = nullptr;
    if (m_mod) {
        logo = geode::createModLogo(m_mod);
    }
    if (!logo) {
        logo = createServerModLogo(m_update.modID);
    }
    if (!logo) {
        logo = CCSprite::createWithSpriteFrameName("GJ_folderIcon_001.png");
    }
    logo->setID("mod-logo");
    logo->setScale(.65f);
    addChildAtPosition(logo, Anchor::Left, {31.f, 0.f});

    auto info = CCNode::create();
    info->setContentSize({205.f, 100.f});
    addChildAtPosition(info, Anchor::Left, {60.f, 0.f});

    auto title = CCLabelBMFont::create(m_update.modName.c_str(), "bigFont.fnt");
    title->setID("mod-name");
    title->setAnchorPoint({0.f, .5f});
    title->setScale(.48f);
    title->limitLabelWidth(200.f, .48f, .30f);
    title->setPosition({0.f, 88.f});
    info->addChild(title);

    auto versions = CCLabelBMFont::create(
        fmt::format("v{} -> v{}", cleanVersion(m_update.currentVersion), cleanVersion(m_update.newVersion)).c_str(),
        "goldFont.fnt"
    );
    versions->setAnchorPoint({0.f, .5f});
    versions->setScale(.28f);
    versions->setPosition({0.f, 72.f});
    info->addChild(versions);

    auto developers = CCLabelBMFont::create("", "goldFont.fnt");
    developers->setAnchorPoint({0.f, .5f});
    developers->setScale(.27f);
    if (m_mod) {
        developers->setString(
            ModMetadata::formatDeveloperDisplayString(m_mod->getMetadata().getDevelopers()).c_str()
        );
    } else {
        developers->setString("Unknown developer");
    }
    developers->limitLabelWidth(200.f, .27f, .16f);
    developers->setPosition({0.f, 55.f});
    info->addChild(developers);

    auto gameVersion = m_mod ? m_mod->getMetadata().getGameVersion() : std::nullopt;
    bool outdated = m_update.outdated;
    bool restartRequired = isCompleted(m_update);

    if (outdated || restartRequired) {
        m_tags = CCNode::create();
        m_tags->setContentSize({200.f, 30.f});
        m_tags->setLayout(
            SimpleRowLayout::create()->setMainAxisAlignment(MainAxisAlignment::Start)->setGap(4.f)
        );

        if (outdated) {
            auto text = gameVersion
                ? fmt::format("Outdated (GD {})", *gameVersion)
                : "Outdated";
            m_tags->addChild(makeTag(text, {255, 190, 70}));
        }
        if (restartRequired) {
            m_tags->addChild(makeTag("Restart Required", {153, 245, 245}));
        }
        m_tags->updateLayout();
        m_tags->setPosition({0.f, 28.f});
        info->addChild(m_tags);
    } else {
        auto descriptionBG = NineSlice::create("square02b_001.png");
        descriptionBG->setContentSize({200.f, 31.f});
        descriptionBG->setOpacity(75);
        descriptionBG->setAnchorPoint({0.f, .5f});
        descriptionBG->setPosition({0.f, 28.f});
        info->addChild(descriptionBG);

        auto description = CCLabelBMFont::create(
            m_mod && m_mod->getMetadata().getDescription()
                ? m_mod->getMetadata().getDescription()->c_str()
                : "[No Description Provided]",
            "chatFont.fnt"
        );
        description->setColor(
            m_mod && m_mod->getMetadata().getDescription() ? ccWHITE : ccGRAY
        );
        description->setAnchorPoint({0.f, .5f});
        description->limitLabelWidth(185.f, .23f, .11f);
        description->setPosition({8.f, 15.5f});
        descriptionBG->addChild(description);
        m_description = descriptionBG;
    }

    auto controls = CCMenu::create();
    controls->setContentSize({72.f, 90.f});
    controls->setLayout(
        ColumnLayout::create()->setGap(5.f)->setAxisAlignment(AxisAlignment::Center)
    );
    addChildAtPosition(controls, Anchor::Right, {-42.f, 0.f});

    auto viewSprite = createGeodeButton("View", 50, false, true);
    auto view = CCMenuItemSpriteExtra::create(viewSprite, this, menu_selector(UpdateModItem::onView));
    view->setID("view-button");
    controls->addChild(view);

    auto updateSprite = createGeodeButton("Update", 50, false, true);
    m_updateButton = CCMenuItemSpriteExtra::create(updateSprite, this, menu_selector(UpdateModItem::onUpdate));
    m_updateButton->setID("update-button");
    controls->addChild(m_updateButton);

    controls->updateLayout();
    return true;
}

void UpdateModItem::onView(CCObject*) {
    if (m_mod) {
        openInfoPopup(m_mod);
    }
}

void UpdateModItem::onUpdate(CCObject*) {
    if (m_sources.size() <= 1) {
        if (!m_sources.empty()) {
            showUpdateConfirmation(m_sources.front(), [this, update = m_sources.front()](bool confirmed) {
                if (confirmed) startUpdate(update);
            });
        }
        return;
    }

    showSourcePicker();
}

void UpdateModItem::showSourcePicker() {
    UpdateSourcePopup::create(
        m_sources,
        [this](IndexUpdateInfo selected) {
            showUpdateConfirmation(selected, [this, selected](bool confirmed) {
                if (confirmed) startUpdate(selected);
            });
        }
    )->show();
}

void UpdateModItem::startUpdate(IndexUpdateInfo update) {
    if (!m_updateButton) return;

    m_updateButton->setVisible(false);
    m_updateButton->setEnabled(false);

    m_progress = Slider::create(nullptr, nullptr);
    m_progress->setID("update-progress");
    m_progress->m_touchLogic->m_thumb->setVisible(false);
    m_progress->setScale(1.45f);
    m_progress->setValue(0.f);
    m_progress->setContentSize({50.f, 10.f});

    if (auto parent = m_updateButton->getParent()) {
        parent->addChild(m_progress);
        m_progress->setPosition(m_updateButton->getPosition());
    }

    downloadIndexUpdate(
        update,
        [this, update](bool success) {
            finishUpdate(update, success);
        },
        [this](float progress) {
            setProgress(progress);
        }
    );
}

void UpdateModItem::setProgress(float progress) {
    if (m_progress)
        m_progress->setValue(std::clamp(progress, 0.f, 1.f));
}

void UpdateModItem::finishUpdate(IndexUpdateInfo const& update, bool success) {
    if (m_progress) {
        m_progress->removeFromParentAndCleanup(true);
        m_progress = nullptr;
    }

    if (!success) {
        m_updateButton->setVisible(true);
        m_updateButton->setEnabled(true);
        return;
    }

    m_updated = true;
    if (m_updateButton)
        m_updateButton->setVisible(false);

    if (m_tags)
        m_tags->removeFromParentAndCleanup(true);

    auto info = getChildByType<CCNode>(0);
    if (info) {
        auto restart = makeTag("Restart Required", {153, 245, 245});
        restart->setPosition({60.f, 28.f});
        addChild(restart, 8);
    }

    auto check = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
    if (check) {
        check->setScale(.42f);
        check->setPosition({getContentWidth() - 42.f, 28.f});
        addChild(check, 8);
    }
}

UpdateModItem* UpdateModItem::create(IndexUpdateInfo update, std::vector<IndexUpdateInfo> sources) {
    auto ret = new UpdateModItem();
    if (ret && ret->init(std::move(update), std::move(sources))) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

} // namespace opengeode
