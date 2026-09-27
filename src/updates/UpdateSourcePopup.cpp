#include "UpdateSourcePopup.hpp"

#include "../InstalledMods.hpp"
#include "../Settings.hpp"

#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/GeodeUI.hpp>

using namespace geode::prelude;

namespace opengeode {
namespace {

std::vector<std::string> getSourceCandidates(std::string const& modID) {
    if (auto source = getInstalledModSource(modID)) {
        if (!source->indexId.empty()) return {source->indexId};
    }
    return splitCSV(readSetting("mod-source-candidates-" + modID, ""));
}

std::string getSourceNames(std::string const& modID) {
    auto candidates = getSourceCandidates(modID);
    if (candidates.empty()) return "Unknown";

    auto indexes = getAllIndexes();
    std::string result;
    for (auto const& id : candidates) {
        auto it = std::find_if(indexes.begin(), indexes.end(), [&](auto const& entry) {
            return entry.id == id;
        });
        if (!result.empty()) result += " / ";
        result += it != indexes.end() ? it->name : id;
    }
    return result;
}

void addCheckmark(CCNode* parent, CCPoint position) {
    auto check = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
    if (!check) return;
    check->setScale(.42f);
    check->setPosition(position);
    parent->addChild(check, 5);
}

} // namespace

bool UpdateSourcePopup::init() {
    if (!Popup::init(350.f, 285.f, getPopupBackground()))
        return false;

    setTitle("Choose Update Source");
    if (auto close = createGeodeCloseButton())
        setCloseButtonSpr(close, .875f);

    auto size = m_mainLayer->getScaledContentSize();
    auto modID = m_options.empty() ? "" : m_options.front().modID;
    auto installed = getInstalledModSource(modID);

    auto sourceLabel = CCLabelBMFont::create(
        fmt::format(
            "Originally installed from: {}{}",
            getSourceNames(modID),
            installed && !installed->version.empty()
                ? fmt::format(" (v{})", installed->version)
                : ""
        ).c_str(),
        "bigFont.fnt"
    );
    sourceLabel->setScale(.27f);
    sourceLabel->setAnchorPoint({.5f, .5f});
    sourceLabel->setPosition({size.width / 2.f, size.height - 37.f});
    sourceLabel->limitLabelWidth(size.width - 35.f, .27f, .18f);
    m_mainLayer->addChild(sourceLabel);

    auto listBG = NineSlice::create("square02b_001.png");
    listBG->setContentSize({size.width - 25.f, size.height - 85.f});
    listBG->setOpacity(70);
    listBG->setPosition({size.width / 2.f, (size.height - 65.f) / 2.f - 5.f});
    m_mainLayer->addChild(listBG);

    auto scroll = ScrollLayer::create({size.width - 45.f, size.height - 105.f});
    scroll->setPosition({22.5f, 25.f});
    scroll->m_contentLayer->setContentWidth(scroll->getContentWidth());

    auto menu = CCMenu::create();
    menu->setContentSize({scroll->getContentWidth() - 8.f, 30.f * m_options.size() + 8.f});
    menu->setPosition({4.f, 4.f});
    menu->setLayout(ColumnLayout::create()->setGap(5.f));

    auto originalIDs = getSourceCandidates(modID);
    auto originalVersion = installed ? installed->version : "";

    for (auto const& option : m_options) {
        auto text = fmt::format(
            "{}   v{} -> v{}",
            option.indexName,
            option.currentVersion.starts_with("v") ? option.currentVersion.substr(1) : option.currentVersion,
            option.newVersion.starts_with("v") ? option.newVersion.substr(1) : option.newVersion
        );
        auto buttonSprite = ButtonSprite::create(
            text.c_str(), "goldFont.fnt", getButtonTexture("GE_button_01.png"), .46f
        );
        buttonSprite->setBaseScale(.46f);
        auto button = CCMenuItemExt::createSpriteExtra(
            buttonSprite,
            [this, option](CCMenuItemSpriteExtra*) {
                auto callback = std::move(m_callback);
                this->onClose(nullptr);
                if (callback) callback(option);
            }
        );
        button->setContentSize({scroll->getContentWidth() - 10.f, 28.f});

        if (std::find(originalIDs.begin(), originalIDs.end(), option.indexID) != originalIDs.end()) {
            addCheckmark(button, {button->getContentWidth() - 13.f, 14.f});
            if (!originalVersion.empty() &&
                parseUpdateVersion(originalVersion) == parseUpdateVersion(option.currentVersion)) {
                auto installedLabel = CCLabelBMFont::create("Installed", "bigFont.fnt");
                installedLabel->setScale(.18f);
                installedLabel->setAnchorPoint({1.f, .5f});
                installedLabel->setPosition({button->getContentWidth() - 20.f, 7.f});
                button->addChild(installedLabel, 6);
            }
        }

        menu->addChild(button);
    }

    menu->updateLayout();
    menu->setPositionY(std::max(4.f, scroll->getContentHeight() - menu->getContentHeight() - 4.f));
    scroll->m_contentLayer->setContentSize({scroll->getContentWidth(), std::max(scroll->getContentHeight(), menu->getContentHeight() + 8.f)});
    scroll->m_contentLayer->addChild(menu);
    m_mainLayer->addChild(scroll);
    return true;
}

UpdateSourcePopup* UpdateSourcePopup::create(
    std::vector<IndexUpdateInfo> options,
    std::function<void(IndexUpdateInfo)> callback
) {
    auto ret = new UpdateSourcePopup();
    ret->m_options = std::move(options);
    ret->m_callback = std::move(callback);
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

} // namespace opengeode
