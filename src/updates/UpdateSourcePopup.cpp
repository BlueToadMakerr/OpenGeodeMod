#include "UpdateSourcePopup.hpp"

#include "../InstalledMods.hpp"
#include "../Settings.hpp"
#include "../PopupSectionUtils.hpp"

#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/GeodeUI.hpp>

using namespace geode::prelude;

namespace opengeode {
namespace {

std::string cleanVersion(std::string value) {
    if (!value.empty() && value.front() == 'v') value.erase(value.begin());
    return value;
}

bool isOriginalSource(std::string const& modID, std::string const& indexID) {
    auto source = getInstalledModSource(modID);
    return source && source->indexId == indexID;
}

CCNode* makeStatusTag(std::string text, bool updated) {
    auto label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    label->setScale(.19f);
    label->setColor(updated ? ccColor3B{120, 190, 255} : ccColor3B{120, 255, 150});
    auto tag = NineSlice::create("square02_001.png");
    tag->setContentSize({label->getScaledContentWidth() + 9.f, 13.f});
    tag->setColor(updated ? ccColor3B{65, 100, 145} : ccColor3B{70, 125, 80});
    tag->setOpacity(190);
    tag->addChildAtPosition(label, Anchor::Center);
    return tag;
}

} // namespace

bool UpdateSourcePopup::init() {
    if (!Popup::init(350.f, 250.f, getPopupBackground())) return false;

    setTitle("Choose Update Source");
    if (auto close = createGeodeCloseButton()) setCloseButtonSpr(close, .875f);

    auto size = m_mainLayer->getScaledContentSize();
    auto modID = m_options.empty() ? "" : m_options.front().modID;

    auto sourceLabel = CCLabelBMFont::create("Choose an index to update from", "bigFont.fnt");
    sourceLabel->setScale(.25f);
    sourceLabel->setColor(ccGRAY);
    sourceLabel->setPosition({size.width / 2.f, size.height - 33.f});
    m_mainLayer->addChild(sourceLabel);

    auto listBG = NineSlice::create(getSectionBackground());
    listBG->setContentSize({size.width - 24.f, size.height - 68.f});
    listBG->setOpacity(70);
    listBG->setColor({0, 0, 0});
    listBG->setPosition({size.width / 2.f, size.height / 2.f - 7.f});
    m_mainLayer->addChild(listBG);

    auto scroll = ScrollLayer::create({size.width - 38.f, size.height - 82.f});
    scroll->setPosition({19.f, 20.f});
    scroll->m_contentLayer->setContentWidth(scroll->getContentWidth());
        constexpr float rowHeight = 43.f;
    constexpr float gap = 1.f;
    auto contentHeight = std::max(scroll->getContentHeight(), static_cast<float>(m_options.size()) * (rowHeight + gap) + gap);
    scroll->m_contentLayer->setContentSize({scroll->getContentWidth(), contentHeight});

    float y = contentHeight - gap - rowHeight / 2.f;
    for (auto const& option : m_options) {
        auto row = CCNode::create();
        row->setContentSize({scroll->getContentWidth() - 4.f, rowHeight});
        row->ignoreAnchorPointForPosition(false);
        row->setAnchorPoint({.5f, .5f});

        auto rowBG = NineSlice::create("square02b_001.png");
        rowBG->setContentSize(row->getContentSize());
        rowBG->setOpacity(85);
        rowBG->setColor({0, 0, 0});
        row->addChildAtPosition(rowBG, Anchor::Center);

        auto title = CCLabelBMFont::create(option.indexName.c_str(), "bigFont.fnt");
        title->setAnchorPoint({0.f, .5f});
        title->setScale(.31f);
        title->limitLabelWidth(175.f, .31f, .16f);
        title->setPosition({8.f, 31.f});
        row->addChild(title);

        auto versions = CCLabelBMFont::create(fmt::format("v{} -> v{}", cleanVersion(option.currentVersion), cleanVersion(option.newVersion)).c_str(), "bigFont.fnt");
        versions->setAnchorPoint({0.f, .5f});
        versions->setScale(.23f);
        versions->setColor({102, 190, 255});
        versions->setPosition({8.f, 19.f});
        row->addChild(versions);

        if (isOriginalSource(modID, option.indexID)) {
            auto updated = wasModUpdatedFromIndex(modID);
            auto installed = makeStatusTag(updated ? "Updated From" : "Installed From", updated);
            installed->setAnchorPoint({0.f, .5f});
            installed->setPosition({8.f, 7.f});
            row->addChild(installed, 2);
        }

        auto updateSprite = ButtonSprite::create("Update", "bigFont.fnt", getButtonTexture("GJ_button_01.png"), .30f);
        updateSprite->setScale(.90f);
        auto button = CCMenuItemExt::createSpriteExtra(updateSprite, [this, option](CCMenuItemSpriteExtra*) {
            auto callback = std::move(m_callback);
            this->onClose(nullptr);
            if (callback) callback(option);
        });
        auto menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        menu->setContentSize(row->getContentSize());
        button->setPosition({row->getContentWidth() - 34.f, rowHeight / 2.f});
        menu->addChild(button);
        row->addChild(menu, 5);

        row->setPosition({scroll->getContentWidth() / 2.f, y});
        scroll->m_contentLayer->addChild(row);
        y -= rowHeight + gap;
    }

    m_mainLayer->addChild(scroll);

    return true;
}

UpdateSourcePopup* UpdateSourcePopup::create(std::vector<IndexUpdateInfo> options, std::function<void(IndexUpdateInfo)> callback) {
    auto ret = new UpdateSourcePopup();
    ret->m_options = std::move(options);
    ret->m_callback = std::move(callback);
    if (ret && ret->init()) { ret->autorelease(); return ret; }
    delete ret;
    return nullptr;
}

} // namespace opengeode
