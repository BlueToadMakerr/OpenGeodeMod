#include "CommentsPopups.hpp"
#include "CommentsUtils.hpp"
#include "PopupSectionUtils.hpp"

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
        if (!Popup::init(300.f, 292.f, getPopupBackground())) return false;

        m_versions = std::move(versions);
        m_onSelect = std::move(cb);
        setTitle("Select Version");

        if (auto close = createGeodeCloseButton())
            setCloseButtonSpr(close, .8f);

        const float contentWidth = 270.f;
        const float contentHeight = 218.f;

        auto scroll = ScrollLayer::create({contentWidth, contentHeight});
        auto content = scroll->m_contentLayer;
        content->setLayout(
            ColumnLayout::create()
                ->setAxisAlignment(AxisAlignment::Start)
                ->setCrossAxisAlignment(AxisAlignment::Center)
                ->setGap(4.f)
                ->setPadding(Padding::uniform(4.f))
        );

        for (auto const& version : m_versions) {
            auto row = CCNode::create();
            row->setContentSize({270.f, 40.f});
            row->setAnchorPoint({.5f, .5f});

            auto bg = NineSlice::create(getSectionBackground());
            bg->setColor({0, 0, 0});
            bg->setOpacity(65);
            bg->setScale(.3f);
            bg->setContentSize(row->getContentSize() / bg->getScale());
            row->addChildAtPosition(bg, Anchor::Center);

            auto label = CCLabelBMFont::create(
                (version.starts_with("v") ? version : "v" + version).c_str(),
                "bigFont.fnt"
            );
            label->setScale(.38f);
            label->setAnchorPoint({0.f, .5f});
            label->limitLabelWidth(190.f, .38f, .1f);
            label->setPosition({8.f, 20.f});
            row->addChild(label);

            auto viewSprite = ButtonSprite::create(
                "View",
                "bigFont.fnt",
                getButtonTexture("GJ_button_01.png"),
                .8f
            );
            viewSprite->setScale(.5f);

            auto item = CCMenuItemExt::createSpriteExtra(
                viewSprite,
                [this, version](CCObject*) {
                    auto callback = m_onSelect;
                    removeFromParent();

                    if (callback) {
                        geode::queueInMainThread(
                            [callback = std::move(callback), version] {
                                callback(version);
                            }
                        );
                    }
                }
            );
            item->setContentSize({45.f, 18.f});

            auto menu = CCMenu::create();
            menu->setPosition({229.f, 20.f});
            menu->addChild(item);
            row->addChild(menu);

            content->addChild(row);
        }

        content->setContentSize({
            contentWidth,
            std::max(
                contentHeight,
                8.f + 44.f * static_cast<float>(m_versions.size())
            )
        });
        content->updateLayout();

        m_mainLayer->addChildAtPosition(scroll, Anchor::Center);
        return true;
    }

public:
    static VersionSelectPopup* create(
        std::vector<std::string> versions,
        std::function<void(std::string)> cb
    ) {
        auto ret = new VersionSelectPopup();
        if (ret && ret->init(std::move(versions), std::move(cb))) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
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
