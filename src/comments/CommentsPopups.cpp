#include "CommentsPopups.hpp"
#include "CommentsUtils.hpp"
#include "../PopupSectionUtils.hpp"

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
        if (!Popup::init(360.f, 280.f, getPopupBackground())) return false;
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
    std::string m_selectedVersion;

    bool init(std::vector<std::string> versions, std::string selectedVersion, std::function<void(std::string)> cb) {
        if (!Popup::init(300.f, 292.f, getPopupBackground())) return false;

        m_versions = std::move(versions);
        m_selectedVersion = std::move(selectedVersion);
        m_onSelect = std::move(cb);
        setTitle("Select Version");

        if (auto close = createGeodeCloseButton())
            setCloseButtonSpr(close, .8f);

        const float width = 300.f;
        const float contentWidth = 270.f;
        const float contentHeight = 218.f;
        const size_t pageSize = 5;
        const auto pageCount = std::max<size_t>(
            1,
            (m_versions.size() + pageSize - 1) / pageSize
        );

        auto page = std::make_shared<size_t>(0);

        auto rows = std::make_shared<std::vector<CCNode*>>();
        for (size_t i = 0; i < pageSize; ++i) {
            auto row = CCNode::create();
            row->setContentSize({270.f, 40.f});
            row->setAnchorPoint({.5f, .5f});

            auto bg = NineSlice::create(getSectionBackground());
            bg->setColor({0, 0, 0});
            bg->setOpacity(65);
            bg->setScale(.3f);
            bg->setContentSize(row->getContentSize() / bg->getScale());
            row->addChildAtPosition(bg, Anchor::Center);

            auto label = CCLabelBMFont::create("", "bigFont.fnt");
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
                [this, rows](CCObject* sender) {
                    auto item = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
                    if (!item) return;
                    auto index = static_cast<size_t>(item->getTag());
                    if (index >= m_versions.size()) return;

                    auto callback = m_onSelect;
                    auto version = m_versions[index];
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
            item->setContentSize({55.f, 18.f});

            auto menu = CCMenu::create();
            menu->setPosition({235.f, 20.f});
            menu->addChild(item);
            row->addChild(menu);

            rows->push_back(row);
            m_mainLayer->addChild(row);
        }

        auto pageLabel = CCLabelBMFont::create("1/1", "bigFont.fnt");
        pageLabel->setScale(.5f);
        pageLabel->setAnchorPoint({.5f, .5f});
        pageLabel->setPosition({width / 2.f, 22.f});
        m_mainLayer->addChild(pageLabel);

        auto updatePage = [this, rows, page, pageSize, pageCount, pageLabel, contentWidth, contentHeight, width]() {
            auto start = *page * pageSize;
            for (size_t i = 0; i < rows->size(); ++i) {
                auto row = rows->at(i);
                auto index = start + i;
                row->setVisible(index < m_versions.size());
                if (index >= m_versions.size())
                    continue;

                auto version = m_versions[index];
                auto label = typeinfo_cast<CCLabelBMFont*>(row->getChildByType<CCLabelBMFont>(0));
                if (label)
                    label->setString(
                        (version.starts_with("v") ? version : "v" + version).c_str()
                    );

                auto menu = typeinfo_cast<CCMenu*>(row->getChildByType<CCMenu>(0));
                if (menu) {
                    auto item = typeinfo_cast<CCMenuItemSpriteExtra*>(menu->getChildByType<CCMenuItemSpriteExtra>(0));
                    if (item) {
                        item->setTag(static_cast<int>(index));
                        auto button = typeinfo_cast<ButtonSprite*>(item->getNormalImage());
                        if (button) {
                            auto current = version.starts_with("v") ? version : "v" + version;
                            auto selected = m_selectedVersion.starts_with("v")
                                ? m_selectedVersion
                                : "v" + m_selectedVersion;
                            button->setString(current == selected ? "Viewing" : "View");
                                }
                    }
                }

                row->setPosition({
                    width / 2.f,
                    234.f - static_cast<float>(i) * 44.f
                });
            }
            pageLabel->setString(
                fmt::format("{}/{}", *page + 1, pageCount).c_str()
            );
        };

        for (size_t i = 0; i < pageSize; ++i) {
            rows->at(i)->setPosition({
                width / 2.f,
                234.f - static_cast<float>(i) * 44.f
            });
        }

        auto prevMenu = CCMenu::create();
        prevMenu->setPosition({15.f, 146.f});
        auto prevSprite = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
        if (prevSprite) {
            prevSprite->setScale(.8f);
            auto prevItem = CCMenuItemExt::createSpriteExtra(
                prevSprite,
                [page, pageCount, updatePage](auto) {
                    if (*page > 0) {
                        --(*page);
                        updatePage();
                    }
                }
            );
            prevMenu->addChild(prevItem);
        }
        m_mainLayer->addChild(prevMenu);

        auto nextMenu = CCMenu::create();
        nextMenu->setPosition({width - 15.f, 146.f});
        auto nextSprite = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
        if (nextSprite) {
            nextSprite->setFlipX(true);
            nextSprite->setScale(.8f);
            auto nextItem = CCMenuItemExt::createSpriteExtra(
                nextSprite,
                [page, pageCount, updatePage](auto) {
                    if (*page + 1 < pageCount) {
                        ++(*page);
                        updatePage();
                    }
                }
            );
            nextMenu->addChild(nextItem);
        }
        m_mainLayer->addChild(nextMenu);

        updatePage();
        return true;
    }

public:
    static VersionSelectPopup* create(
        std::vector<std::string> versions,
        std::string selectedVersion,
        std::function<void(std::string)> cb
    ) {
        auto ret = new VersionSelectPopup();
        if (ret && ret->init(std::move(versions), std::move(selectedVersion), std::move(cb))) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

class AttachmentImagePopup : public Popup {
    bool init(std::string url) {
        if (!Popup::init(350.f, 260.f, getPopupBackground())) return false;
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
Popup* createVersionSelectPopup(std::vector<std::string> versions, std::function<void(std::string)> callback) { return VersionSelectPopup::create(std::move(versions), "", std::move(callback)); }
Popup* createAttachmentImagePopup(std::string url) { return AttachmentImagePopup::create(std::move(url)); }

} // namespace opengeode
