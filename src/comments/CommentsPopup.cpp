#include "CommentsLayer.hpp"
#include "CommentsUtils.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/utils/ColorProvider.hpp>

#include <algorithm>
#include <string>

using namespace geode::prelude;

namespace opengeode {
namespace {

class OpenGeodeTabSprite : public CCNode {
    CCScale9Sprite* m_deselectedBG = nullptr;
    CCScale9Sprite* m_selectedBG = nullptr;
    CCSprite* m_icon = nullptr;
    CCLabelBMFont* m_label = nullptr;

    bool init(char const* iconFrame, char const* text, float width) {
        if (!CCNode::init()) return false;

        const CCSize itemSize{width, 35.f};
        const CCSize iconSize{18.f, 18.f};
        setContentSize(itemSize);
        setAnchorPoint({.5f, .5f});

        m_deselectedBG = CCScale9Sprite::createWithSpriteFrameName("geode.loader/tab-bg.png");
        if (!m_deselectedBG) return false;
        m_deselectedBG->setScale(.8f);
        m_deselectedBG->setContentSize(itemSize / .8f);
        m_deselectedBG->setColor(ColorProvider::get()->color3b("mod-list-tab-deselected-bg"));
        addChildAtPosition(m_deselectedBG, Anchor::Center);

        m_selectedBG = CCScale9Sprite::createWithSpriteFrameName("geode.loader/tab-bg.png");
        if (!m_selectedBG) return false;
        m_selectedBG->setScale(.8f);
        m_selectedBG->setContentSize(itemSize / .8f);
        m_selectedBG->setColor(to3B(ColorProvider::get()->color("mod-list-tab-selected-bg")));
        addChildAtPosition(m_selectedBG, Anchor::Center);

        m_icon = CCSprite::createWithSpriteFrameName(iconFrame);
        if (!m_icon) return false;
        limitNodeSize(m_icon, iconSize, 3.f, .1f);
        addChildAtPosition(m_icon, Anchor::Left, ccp(16, 0), false);

        m_label = CCLabelBMFont::create(text, "bigFont.fnt");
        m_label->limitLabelWidth(getContentWidth() - 45.f, std::clamp(width * .0045f, .35f, .55f), .1f);
        m_label->setAnchorPoint({.5f, .5f});
        addChildAtPosition(
            m_label,
            Anchor::Left,
            ccp((itemSize.width - iconSize.width) / 2.f + iconSize.width, 0),
            false
        );

        select(false);
        return true;
    }

public:
    static OpenGeodeTabSprite* create(char const* iconFrame, char const* text, float width) {
        auto ret = new OpenGeodeTabSprite();
        if (ret && ret->init(iconFrame, text, width)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    void select(bool selected) {
        if (m_deselectedBG) m_deselectedBG->setVisible(!selected);
        if (m_selectedBG) m_selectedBG->setVisible(selected);
    }
};

}

void selectExistingTab(CCMenuItemSpriteExtra* item, bool selected) {
    if (!item) return;
    auto image = item->getNormalImage();
    if (!image) return;
    auto deselected = typeinfo_cast<NineSlice*>(image->getChildByType<NineSlice>(0));
    auto selectedBG = typeinfo_cast<NineSlice*>(image->getChildByType<NineSlice>(1));
    if (deselected) deselected->setVisible(!selected);
    if (selectedBG) selectedBG->setVisible(selected);
}

void clearCommentsTab(CCNode* popup) {
    if (!popup) return;

    for (auto const* containerID : {"description-container", "changelog-container"}) {
        auto container = popup->getChildByIDRecursive(containerID);
        if (!container) continue;

        if (auto layer = container->getChildByIDRecursive("opengeode-comments-layer"))
            layer->removeFromParent();

        if (auto textarea = container->getChildByIDRecursive("textarea"))
            textarea->setVisible(true);
    }

    auto tabs = popup->getChildByIDRecursive("tabs-menu");
    if (!tabs) return;

    auto comments = typeinfo_cast<CCMenuItemSpriteExtra*>(
        tabs->getChildByID("opengeode-comments-tab")
    );
    if (!comments) return;

    auto sprite = typeinfo_cast<OpenGeodeTabSprite*>(comments->getNormalImage());
    if (sprite)
        sprite->select(false);
}

void ensureCommentsTab(CCNode* popup) {
    if (!popup) return;

    auto modID = getModID(popup);
    if (modID.empty()) return;

    auto tabs = popup->getChildByIDRecursive("tabs-menu");
    auto textarea = popup->getChildByIDRecursive("textarea");
    if (!tabs || !textarea || tabs->getChildByID("opengeode-comments-tab"))
        return;

    auto description = typeinfo_cast<CCMenuItemSpriteExtra*>(tabs->getChildByID("description"));
    auto changelog = typeinfo_cast<CCMenuItemSpriteExtra*>(tabs->getChildByID("changelog"));
    if (!description || !changelog) return;

    auto commentsSprite = OpenGeodeTabSprite::create("chat", "Comments", 140.f);
    if (!commentsSprite) return;

    auto item = CCMenuItemExt::createSpriteExtra(
        commentsSprite,
        [popup, modID, description, changelog, commentsSprite](CCMenuItemSpriteExtra*) {
            auto descriptionContainer = popup->getChildByIDRecursive("description-container");
            auto changelogContainer = popup->getChildByIDRecursive("changelog-container");

            CCNode* activeContainer = descriptionContainer
                ? descriptionContainer
                : changelogContainer;
            if (!activeContainer) return;

            auto activeTextarea = activeContainer->getChildByIDRecursive("textarea");
            if (!activeTextarea) return;

            while (auto old = activeContainer->getChildByType<CommentsLayer>(0))
                old->removeFromParent();

            selectExistingTab(description, false);
            selectExistingTab(changelog, false);
            commentsSprite->select(true);
            activeTextarea->setVisible(false);

            auto layer = CommentsLayer::create(modID, activeTextarea);
            if (layer) {
                layer->setID("opengeode-comments-layer");
                layer->setAnchorPoint({.5f, .5f});
                layer->setScale(activeTextarea->getScale());
                layer->setRotation(activeTextarea->getRotation());
                activeContainer->addChildAtPosition(layer, Anchor::Center);
            }
        }
    );

    item->setTag(2);
    item->setID("opengeode-comments-tab");
    tabs->addChild(item);
    tabs->updateLayout();
}
} // namespace opengeode
