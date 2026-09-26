#include "CommentsPopups.hpp"
#include "CommentsUtils.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/MDTextArea.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/LazySprite.hpp>
#include <Geode/utils/string.hpp>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace geode::prelude;

namespace opengeode {

class CommentViewPopup : public Popup {
    bool init(std::string text) {
        if (!Popup::init(360.f, 280.f, "GE_square01.png"_spr)) return false;
        setTitle("Comment");
        auto area = MDTextArea::create(text.empty() ? "..." : text, {330.f, 220.f}, true);
        area->getScrollLayer()->m_cutContent = false;
        area->getScrollLayer()->m_disableMovement = false;
        area->getScrollLayer()->setMouseEnabled(true);
        if (auto bg = area->getChildByType<CCScale9Sprite>(0)) bg->setVisible(false);
        m_mainLayer->addChildAtPosition(area, Anchor::Center);
        m_noElasticity = true;
        return true;
    }
public:
    static CommentViewPopup* create(std::string text) {
        auto ret = new CommentViewPopup();
        if (ret && ret->init(std::move(text))) { ret->autorelease(); return ret; }
        delete ret;
        return nullptr;
    }
};

class VersionSelectPopup : public Popup {
    std::vector<std::string> m_versions;
    std::function<void(std::string)> m_onSelect;
    bool init(std::vector<std::string> versions, std::function<void(std::string)> cb) {
        if (!Popup::init(270.f, 240.f, "GE_square01.png"_spr)) return false;
        m_versions = std::move(versions);
        m_onSelect = std::move(cb);
        setTitle("Select Version");
        auto root = CCNode::create();
        root->setContentSize({244.f, 194.f});
        root->setAnchorPoint({.5f, .5f});
        root->setLayout(ColumnLayout::create()->setAxisAlignment(AxisAlignment::Center)->setCrossAxisAlignment(AxisAlignment::Center)->setGap(4.f));
        auto scroll = ScrollLayer::create({244.f, 184.f});
        auto content = scroll->m_contentLayer;
        content->setLayout(ColumnLayout::create()->setAxisAlignment(AxisAlignment::Start)->setCrossAxisAlignment(AxisAlignment::Center)->setGap(4.f)->setPadding(Padding::uniform(4.f)));
        for (auto const& version : m_versions) {
            auto row = CCNode::create();
            row->setContentSize({230.f, 32.f});
            row->setAnchorPoint({.5f, .5f});
            row->setLayout(RowLayout::create()
                ->setAxisAlignment(AxisAlignment::Between)
                ->setCrossAxisAlignment(AxisAlignment::Center)
                ->setPadding(Padding::horizontal(6.f)));

            auto label = CCLabelBMFont::create(version.c_str(), "bigFont.fnt");
            label->setScale(.34f);
            label->limitLabelWidth(130.f, .34f, .1f);
            row->addChild(label);

            auto view = ButtonSprite::create(
                "View", "bigFont.fnt", "GE_button_05.png"_spr, .8f
            );
            auto item = CCMenuItemExt::createSpriteExtra(view, [this, version](auto) {
                auto callback = m_onSelect;
                removeFromParent();
                if (callback)
                    geode::queueInMainThread(
                        [callback = std::move(callback), version] {
                            callback(version);
                        }
                    );
            });
            item->setScale(.38f);
            item->m_baseScale = .38f;
            row->addChild(item);
            row->updateLayout();
            content->addChild(row);
        }
        content->setContentSize({244.f, std::max(184.f, 8.f + 36.f * static_cast<float>(m_versions.size()))});
        content->updateLayout(); root->addChild(scroll); root->updateLayout();
        m_mainLayer->addChildAtPosition(root, Anchor::Center);
        return true;
    }
public:
    static VersionSelectPopup* create(std::vector<std::string> versions, std::function<void(std::string)> cb) {
        auto ret = new VersionSelectPopup();
        if (ret && ret->init(std::move(versions), std::move(cb))) { ret->autorelease(); return ret; }
        delete ret; return nullptr;
    }
};

class AttachmentImagePopup : public Popup {
    bool init(std::string url) {
        if (!Popup::init(350.f, 260.f, "GE_square01.png"_spr)) return false;
        setTitle("Attachment");
        auto holder = CCNode::create();
        holder->setContentSize({320.f, 210.f});
        holder->setAnchorPoint({.5f, .5f});
        holder->setLayout(AnchorLayout::create());
        m_mainLayer->addChildAtPosition(holder, Anchor::Center);
        createContainedImage(holder, {320.f, 210.f}, url);
        m_noElasticity = true;
        return true;
    }
public:
    static AttachmentImagePopup* create(std::string url) {
        auto ret = new AttachmentImagePopup();
        if (ret && ret->init(std::move(url))) { ret->autorelease(); return ret; }
        delete ret; return nullptr;
    }
};

// AttachmentPopup implementation remains unchanged from the split refactor.
// The factory is kept here so other comments-layer translation units do not
// need access to the private popup class.

Popup* createCommentViewPopup(std::string text) { return CommentViewPopup::create(std::move(text)); }
Popup* createVersionSelectPopup(std::vector<std::string> versions, std::function<void(std::string)> callback) { return VersionSelectPopup::create(std::move(versions), std::move(callback)); }
Popup* createAttachmentImagePopup(std::string url) { return AttachmentImagePopup::create(std::move(url)); }

} // namespace opengeode
