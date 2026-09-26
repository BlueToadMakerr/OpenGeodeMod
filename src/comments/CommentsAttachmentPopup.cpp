#include "CommentsPopups.hpp"
#include "CommentsUtils.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/string.hpp>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

using namespace geode::prelude;

namespace opengeode {

class AttachmentPopup : public Popup {
    std::vector<CommentAttachment> m_attachments;
    std::vector<std::filesystem::path> m_pending;
    std::vector<int> m_removed;
    std::function<void(int)> m_onToggleDelete;
    std::function<void(std::filesystem::path const&)> m_onRemovePending;
    std::function<void()> m_onAdd;
    ScrollLayer* m_scroll = nullptr;
    CCNode* m_content = nullptr;

    bool isRemoved(int id) const {
        return std::find(m_removed.begin(), m_removed.end(), id) != m_removed.end();
    }

    void rebuild() {
        if (!m_content) return;
        m_content->removeAllChildren();

        constexpr float rowWidth = 258.f;
        constexpr float rowHeight = 40.f;
        constexpr float pad = 6.f;
        constexpr float imageSize = 32.f;
        constexpr float actionWidth = 72.f;
        constexpr float infoWidth = rowWidth - pad * 4.f - imageSize - actionWidth;

        auto addRow = [this](
            std::string name,
            std::string status,
            std::string url,
            std::filesystem::path const* localPath,
            int attachmentID,
            bool removed,
            bool pendingUpload
        ) {
            auto row = CCNode::create();
            row->setContentSize({rowWidth, rowHeight});
            row->setAnchorPoint({.5f, .5f});
            row->setLayout(AnchorLayout::create());

            auto bg = NineSlice::create("square02b_001.png");
            bg->setColor(ccBLACK);
            bg->setOpacity(70);
            bg->setScale(.3f);
            bg->setContentSize(row->getContentSize() / bg->getScale());
            row->addChildAtPosition(bg, Anchor::Center);

            auto box = createAttachmentBox(imageSize, url, localPath);
            row->addChildAtPosition(box, Anchor::Left, ccp(pad + imageSize / 2.f, 0.f));

            auto info = CCNode::create();
            info->setContentSize({infoWidth, imageSize});
            info->setAnchorPoint({0.f, .5f});
            info->setLayout(AnchorLayout::create());
            row->addChildAtPosition(info, Anchor::Left, ccp(pad * 2.f + imageSize, 0.f));

            auto nameLabel = CCLabelBMFont::create(name.c_str(), "chatFont.fnt");
            nameLabel->setAnchorPoint({0.f, .5f});
            nameLabel->limitLabelWidth(infoWidth, .27f, .1f);
            info->addChildAtPosition(nameLabel, Anchor::Left, ccp(0.f, 7.f));

            auto statusLabel = CCLabelBMFont::create(status.c_str(), "chatFont.fnt");
            statusLabel->setAnchorPoint({0.f, .5f});
            statusLabel->limitLabelWidth(infoWidth, .22f, .1f);
            info->addChildAtPosition(statusLabel, Anchor::Left, ccp(0.f, -7.f));

            auto actions = CCMenu::create();
            actions->setContentSize({actionWidth, imageSize});
            actions->setAnchorPoint({.5f, .5f});
            actions->ignoreAnchorPointForPosition(false);
            row->addChildAtPosition(actions, Anchor::Right, ccp(-(pad + actionWidth / 2.f), 0.f));

            auto action = ButtonSprite::create(
                removed ? "Restore" : "Remove",
                "goldFont.fnt",
                removed ? "GE_button_05.png"_spr : "GJ_button_06.png",
                1.f
            );

            CCMenuItemSpriteExtra* actionItem = nullptr;
            if (pendingUpload) {
                actionItem = CCMenuItemExt::createSpriteExtra(
                    action,
                    [this, path = localPath ? *localPath : std::filesystem::path()](auto) {
                        if (path.empty()) return;
                        if (m_onRemovePending) m_onRemovePending(path);
                        m_pending.erase(std::remove(m_pending.begin(), m_pending.end(), path), m_pending.end());
                        rebuild();
                    }
                );
            } else {
                actionItem = CCMenuItemExt::createSpriteExtra(
                    action,
                    [this, attachmentID](auto) {
                        if (m_onToggleDelete) m_onToggleDelete(attachmentID);
                        if (isRemoved(attachmentID)) {
                            m_removed.erase(std::remove(m_removed.begin(), m_removed.end(), attachmentID), m_removed.end());
                        } else {
                            m_removed.push_back(attachmentID);
                        }
                        rebuild();
                    }
                );
            }

            limitNodeSize(actionItem, {actionWidth, imageSize - 6.f}, .7f, .1f);
            actionItem->m_baseScale = actionItem->getScale();
            actionItem->setPosition(actions->getContentSize() / 2.f);
            actions->addChild(actionItem);
            m_content->addChild(row);
        };

        for (auto const& attachment : m_attachments) {
            auto removed = isRemoved(attachment.id);
            addRow(
                attachment.filename.empty() ? fmt::format("Image {}", attachment.id) : attachment.filename,
                removed ? "Pending to remove" : "Uploaded",
                attachment.url,
                nullptr,
                attachment.id,
                removed,
                false
            );
        }

        for (auto const& path : m_pending) {
            addRow(
                geode::utils::string::pathToString(path.filename()),
                "Pending to upload",
                "",
                &path,
                0,
                false,
                true
            );
        }

        if (m_attachments.empty() && m_pending.empty()) {
            auto empty = CCLabelBMFont::create("No attachments.", "chatFont.fnt");
            empty->setScale(.34f);
            empty->setAnchorPoint({.5f, .5f});
            m_content->addChild(empty);
        }

        auto count = m_attachments.size() + m_pending.size();
        m_content->setContentSize({
            m_scroll->getContentWidth(),
            std::max(m_scroll->getContentHeight(), 10.f + (rowHeight + 4.f) * static_cast<float>(count))
        });
        m_content->updateLayout();
    }

    bool init(
        std::vector<CommentAttachment> attachments,
        std::vector<std::filesystem::path> pending,
        std::vector<int> removed,
        std::function<void(int)> onToggleDelete,
        std::function<void(std::filesystem::path const&)> onRemovePending,
        std::function<void()> onAdd
    ) {
        if (!Popup::init(300.f, 235.f, "GE_square01.png"_spr)) return false;

        m_attachments = std::move(attachments);
        m_pending = std::move(pending);
        m_removed = std::move(removed);
        m_onToggleDelete = std::move(onToggleDelete);
        m_onRemovePending = std::move(onRemovePending);
        m_onAdd = std::move(onAdd);
        setTitle("Attachments");

        auto size = m_mainLayer->getContentSize();
        constexpr float scrollWidth = 270.f;
        constexpr float scrollHeight = 153.f;
        constexpr float scrollBottom = 46.f;

        auto listBG = NineSlice::create("square02b_001.png");
        listBG->setColor(ccBLACK);
        listBG->setOpacity(90);
        listBG->setScale(.3f);
        listBG->setContentSize(CCSize{scrollWidth + 6.f, scrollHeight + 6.f} / listBG->getScale());
        listBG->setPosition({size.width / 2.f, scrollBottom + scrollHeight / 2.f});
        m_mainLayer->addChild(listBG);

        m_scroll = ScrollLayer::create({scrollWidth, scrollHeight});
        m_scroll->setPosition({(size.width - scrollWidth) / 2.f, scrollBottom});
        m_content = m_scroll->m_contentLayer;
        m_content->setAnchorPoint({0.f, 0.f});
        m_content->setLayout(
            ColumnLayout::create()
                ->setAxisReverse(true)
                ->setAxisAlignment(AxisAlignment::Start)
                ->setCrossAxisAlignment(AxisAlignment::Center)
                ->setAutoScale(false)
                ->setGap(4.f)
                ->setPadding(Padding::uniform(5.f))
        );
        m_mainLayer->addChild(m_scroll);

        auto add = ButtonSprite::create("+ Add Image", "bigFont.fnt", "GE_button_05.png"_spr, .8f);
        auto addItem = CCMenuItemExt::createSpriteExtra(add, [this](auto) {
            if (m_onAdd) {
                m_onAdd();
                removeFromParent();
            }
        });
        addItem->setScale(.85f);
        addItem->m_baseScale = .85f;
        m_buttonMenu->addChildAtPosition(addItem, Anchor::Bottom, ccp(0.f, 24.f));

        rebuild();
        m_scroll->scrollToTop();
        return true;
    }

public:
    static AttachmentPopup* create(
        std::vector<CommentAttachment> attachments,
        std::vector<std::filesystem::path> pending,
        std::vector<int> removed,
        std::function<void(int)> onToggleDelete,
        std::function<void(std::filesystem::path const&)> onRemovePending,
        std::function<void()> onAdd
    ) {
        auto ret = new AttachmentPopup();
        if (ret && ret->init(
            std::move(attachments), std::move(pending), std::move(removed),
            std::move(onToggleDelete), std::move(onRemovePending), std::move(onAdd)
        )) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

Popup* createAttachmentPopup(
    std::vector<CommentAttachment> attachments,
    std::vector<std::filesystem::path> pending,
    std::vector<int> removed,
    std::function<void(int)> onToggleDelete,
    std::function<void(std::filesystem::path const&)> onRemovePending,
    std::function<void()> onAdd
) {
    return AttachmentPopup::create(
        std::move(attachments), std::move(pending), std::move(removed),
        std::move(onToggleDelete), std::move(onRemovePending), std::move(onAdd)
    );
}

} // namespace opengeode
