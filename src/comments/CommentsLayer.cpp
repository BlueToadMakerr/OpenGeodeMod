#include "CommentsLayer.hpp"
#include "Settings.hpp"
#include "CommentsUtils.hpp"
#include "CommentsPopups.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/MDTextArea.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/utils/web.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace geode::prelude;

namespace opengeode {

std::string m_modID;
    CCNode* m_textArea = nullptr;
    CommentState m_state;

    async::TaskHolder<web::WebResponse> m_requestTask;
    std::vector<std::filesystem::path> m_pendingFiles;
    std::vector<int> m_removedAttachments;

    int m_editingCommentID = 0;

    CCMenuItemSpriteExtra* m_versionButton = nullptr;
    CCLabelBMFont* m_attachmentCountLabel = nullptr;
    CCLabelBMFont* m_lockLabel = nullptr;
    float m_lockLabelMaxWidth = 200.f;
    CCMenu* m_lockControls = nullptr;
    CCMenuItemSpriteExtra* m_exitButton = nullptr;
    CCMenuItemSpriteExtra* m_attachButton = nullptr;
    TextInput* m_input = nullptr;
    CCMenuItemSpriteExtra* m_sendButton = nullptr;
    CCNode* m_commentsContainer = nullptr;
    CCNode* m_bottom = nullptr;
    std::unordered_map<int, int> m_attachmentOffsets;

    bool CommentsLayer::init(std::string modID, CCNode* textArea) {

        if (!CCLayer::init()) return false;

        m_modID = std::move(modID);
        m_textArea = textArea;
        setContentSize(textArea->getContentSize());
        setAnchorPoint({.5f, .5f});
        setKeyboardEnabled(true);

        auto width = getContentWidth();
        auto height = getContentHeight();

        auto root = CCNode::create();
        root->setContentSize(getContentSize());
        root->setAnchorPoint({.5f, .5f});
        root->setLayout(AnchorLayout::create());
        addChildAtPosition(root, Anchor::Center);

        auto bg = NineSlice::create("square02b_001.png");
        bg->setColor(ccBLACK);
        bg->setOpacity(105);
        bg->setScale(.3f);
        bg->setContentSize(getContentSize() / bg->getScale());
        root->addChildAtPosition(bg, Anchor::Center);

        auto top = CCNode::create();
        top->setContentSize({width - 8.f, kTopHeight});
        top->setAnchorPoint({.5f, 1.f});
        top->setLayout(AnchorLayout::create());
        root->addChild(top);
        top->setLayoutOptions(AnchorLayoutOptions::create()
            ->setAnchor(Anchor::Top)
            ->setOffset(ccp(0.f, -kTopInset)));

        m_lockLabel = CCLabelBMFont::create("Unlocked", "chatFont.fnt");
        m_lockLabel->setScale(.42f);
        m_lockLabel->setAnchorPoint({0.f, .5f});

        auto lockControls = CCMenu::create();
        m_lockControls = lockControls;
        lockControls->setAnchorPoint({1.f, .5f});
        lockControls->ignoreAnchorPointForPosition(false);
        lockControls->setLayout(RowLayout::create()
            ->setAxisAlignment(AxisAlignment::End)
            ->setCrossAxisAlignment(AxisAlignment::Center)
            ->setAutoScale(false)
            ->setGap(4.f));

        auto makeLockButton = [this, lockControls](
            char const* text, char const* value, char const* texture
        ) {
            auto sprite = ButtonSprite::create(
                text, "goldFont.fnt", texture, 1.f
            );
            auto item = CCMenuItemExt::createSpriteExtra(
                sprite, [this, value](auto) { setLock(value); }
            );
            item->setScale(kLockButtonScale);
            item->m_baseScale = kLockButtonScale;
            lockControls->addChild(item);
        };

        makeLockButton("Lock", "locked", "GJ_button_06.png");
        makeLockButton("Internal", "internal", "GJ_button_02.png");
        makeLockButton("Unlock", "none", "GJ_button_01.png");

        float lockWidth = 4.f * 2.f;
        for (auto child : lockControls->getChildrenExt())
            lockWidth += child->getScaledContentSize().width;
        lockControls->setContentSize({lockWidth, kTopHeight});
        lockControls->updateLayout();

        top->addChildAtPosition(lockControls, Anchor::Right, ccp(-8.f, 0.f));
        top->addChildAtPosition(m_lockLabel, Anchor::Left, ccp(8.f, 0.f));

        m_lockLabelMaxWidth = std::max(60.f, width - 8.f - 16.f - lockWidth - 5.f);
        m_lockLabel->limitLabelWidth(m_lockLabelMaxWidth, .42f, .1f);

        auto middleHeight = std::max(
            1.f,
            height
                - (kTopHeight + kTopInset)
                - (kBottomHeight + kBottomInset)
                - kSectionGap * 2.f
        );
        auto middleOffsetY =
            ((kBottomHeight + kBottomInset) - (kTopHeight + kTopInset)) / 2.f;

        auto commentsArea = CCNode::create();
        commentsArea->setContentSize({width - 8.f, middleHeight});
        commentsArea->setAnchorPoint({.5f, .5f});
        commentsArea->setLayout(AnchorLayout::create());
        root->addChild(commentsArea);
        commentsArea->setLayoutOptions(
            AnchorLayoutOptions::create()
                ->setAnchor(Anchor::Center)
                ->setOffset(ccp(0.f, middleOffsetY))
        );

        auto scroll = ScrollLayer::create({
            commentsArea->getContentWidth(),
            commentsArea->getContentHeight()
        });
        scroll->setID("opengeode-comments-scroll"_spr);
        scroll->setAnchorPoint({0.f, 0.f});
        commentsArea->addChild(scroll);
        scroll->setLayoutOptions(
            AnchorLayoutOptions::create()->setAnchor(Anchor::BottomLeft)
        );
        m_commentsContainer = scroll->m_contentLayer;
        if (m_commentsContainer)
            m_commentsContainer->setAnchorPoint({0.f, 0.f});

        auto bottom = CCNode::create();
        bottom->setContentSize({width - 8.f, kBottomHeight});
        bottom->setAnchorPoint({.5f, 0.f});
        root->addChild(bottom);
        m_bottom = bottom;
        bottom->setLayoutOptions(
            AnchorLayoutOptions::create()
                ->setAnchor(Anchor::Bottom)
                ->setOffset(ccp(0.f, kBottomInset))
        );

        auto bottomBg = NineSlice::create("square02b_001.png");
        bottomBg->setColor(ccBLACK);
        bottomBg->setOpacity(115);
        bottomBg->setScale(.3f);
        bottomBg->setContentSize(bottom->getContentSize() / bottomBg->getScale());
        bottomBg->setPosition(bottom->getContentSize() / 2.f);
        bottom->addChild(bottomBg);

        auto bottomMenu = CCMenu::create();
        bottomMenu->setContentSize(bottom->getContentSize());
        bottomMenu->setAnchorPoint({0.f, 0.f});
        bottomMenu->setPosition({0.f, 0.f});
        bottom->addChild(bottomMenu);

        auto versionSprite = ButtonSprite::create(
            "v-", "bigFont.fnt", "GJ_button_01.png", 1.f
        );
        m_versionButton = CCMenuItemExt::createSpriteExtra(
            versionSprite, [this](auto) { showVersionPicker(); }
        );
        m_versionButton->setScale(kBarButtonScale);
        m_versionButton->m_baseScale = kBarButtonScale;
        bottomMenu->addChild(m_versionButton);

        auto exitSprite = ButtonSprite::create(
            "Exit Edit", "goldFont.fnt", "GJ_button_06.png", 1.f
        );
        m_exitButton = CCMenuItemExt::createSpriteExtra(
            exitSprite, [this](auto) { exitEdit(); }
        );
        m_exitButton->setScale(kBarButtonScale);
        m_exitButton->m_baseScale = kBarButtonScale;
        m_exitButton->setVisible(false);
        bottomMenu->addChild(m_exitButton);

        auto attachSprite = CCSprite::createWithSpriteFrameName("GJ_plusBtn_001.png");
        if (attachSprite)
            limitNodeSize(attachSprite, {13.f, 13.f}, 1.f, .1f);

        m_attachButton = CCMenuItemExt::createSpriteExtra(
            attachSprite ? static_cast<CCNode*>(attachSprite)
                         : static_cast<CCNode*>(CCLabelBMFont::create("+", "bigFont.fnt")),
            [this](auto) { showAttachmentsPopup(); }
        );
        bottomMenu->addChild(m_attachButton);

        m_attachmentCountLabel = CCLabelBMFont::create("", "chatFont.fnt");
        m_attachmentCountLabel->setScale(.38f);
        m_attachmentCountLabel->setAnchorPoint({.5f, .5f});
        m_attachmentCountLabel->setVisible(false);
        bottom->addChild(m_attachmentCountLabel);

        m_input = TextInput::create(100.f, "Add a comment...", "chatFont.fnt");
        m_input->setID("opengeode-comment-input"_spr);
        m_input->setCommonFilter(CommonFilter::Any);
        m_input->setMaxCharCount(2000);
        m_input->setAnchorPoint({.5f, .5f});
        m_input->setContentSize({100.f, 6.5f});
        bottom->addChild(m_input);

        auto send = ButtonSprite::create(
            "Send", "goldFont.fnt", "GJ_button_01.png", 1.f
        );
        m_sendButton = CCMenuItemExt::createSpriteExtra(
            send, [this](auto) { submitComment(); }
        );
        m_sendButton->setScale(kBarButtonScale);
        m_sendButton->m_baseScale = kBarButtonScale;
        bottomMenu->addChild(m_sendButton);

        root->updateLayout();
        updateBottomLayout();
        load();
        return true;
    
}
void CommentsLayer::updateBottomLayout() {

        if (!m_bottom || !m_input) return;

        constexpr float pad = 8.f;
        constexpr float gap = 5.f;
        auto rowWidth = m_bottom->getContentWidth();
        auto centerY = m_bottom->getContentHeight() / 2.f;

        std::vector<CCNode*> order {
            m_versionButton, m_exitButton, m_attachButton,
            m_attachmentCountLabel, m_input, m_sendButton
        };

        float fixedWidth = 0.f;
        int shown = 0;
        for (auto node : order) {
            if (!node || !node->isVisible()) continue;
            ++shown;
            if (node != m_input)
                fixedWidth += node->getScaledContentSize().width;
        }

        auto inputWidth = std::max(
            1.f,
            rowWidth - pad * 2.f - fixedWidth
                - gap * static_cast<float>(std::max(0, shown - 1))
        );
        m_input->setContentSize({
            inputWidth,
            6.5f
        });

        float x = pad;
        for (auto node : order) {
            if (!node || !node->isVisible()) continue;
            auto w = node == m_input
                ? inputWidth
                : node->getScaledContentSize().width;
            node->setPosition({x + w / 2.f, centerY});
            x += w + gap;
        }
    
}
void CommentsLayer::rebuild() {

        if (!m_commentsContainer) return;

        m_commentsContainer->removeAllChildren();
        auto scroll = typeinfo_cast<ScrollLayer*>(
            getChildByIDRecursive("opengeode-comments-scroll"_spr)
        );
        if (!scroll) return;

        auto width = scroll->getContentWidth() - 18.f;
        float totalHeight = 8.f;
        bool any = false;

        m_commentsContainer->setLayout(
            ColumnLayout::create()
                ->setAxisReverse(true)
                ->setAxisAlignment(AxisAlignment::Start)
                ->setCrossAxisAlignment(AxisAlignment::Center)
                ->setAutoScale(false)
                ->setGap(7.f)
                ->setPadding(Padding::uniform(4.f))
        );

        for (auto const& comment : m_state.comments) {
            any = true;

            auto header = CCNode::create();
            header->setContentSize({width - 4.f, 36.f});
            header->setAnchorPoint({.5f, .5f});
            header->setLayout(RowLayout::create()
                ->setAxisAlignment(AxisAlignment::Between)
                ->setCrossAxisAlignment(AxisAlignment::Center)
                ->setPadding(Padding::horizontal(3.f)));

            auto identity = CCNode::create();
            identity->setContentSize({width - 105.f, 36.f});
            identity->setAnchorPoint({.5f, .5f});
            identity->setLayout(RowLayout::create()
                ->setAxisAlignment(AxisAlignment::Start)
                ->setCrossAxisAlignment(AxisAlignment::Center)
                ->setGap(7.f));

            auto avatar = CCNode::create();
            avatar->setContentSize({34.f, 34.f});
            avatar->setAnchorPoint({.5f, .5f});
            avatar->setLayout(AnchorLayout::create());

            auto avatarBG = CCScale9Sprite::create("square02_small.png");
            avatarBG->setColor(ccBLACK);
            avatarBG->setOpacity(100);
            avatarBG->setContentSize({30.f, 30.f});
            avatar->addChildAtPosition(avatarBG, Anchor::Center);

            addAvatar(avatar, comment);
            identity->addChild(avatar);

            auto name = CCLabelBMFont::create(
                comment.username.c_str(), "goldFont.fnt"
            );
            name->setScale(.32f);
            name->limitLabelWidth(width - 150.f, .32f, .1f);
            identity->addChild(name);
            identity->updateLayout();
            header->addChild(identity);

            auto actions = CCMenu::create();
            actions->setContentSize({150.f, 42.f});
            actions->setAnchorPoint({.5f, .5f});
            actions->setLayout(RowLayout::create()
                ->setAxisAlignment(AxisAlignment::End)
                ->setCrossAxisAlignment(AxisAlignment::Center)
                ->setGap(4.f));

            auto viewButton = ButtonSprite::create(
                "View", "goldFont.fnt", "GJ_button_01.png", 1.f
            );
            viewButton->setScale(.34f);
            auto viewItem = CCMenuItemExt::createSpriteExtra(
                viewButton, [this, comment](auto) {
                    showComment(comment.body);
                }
            );
            viewItem->setAnchorPoint({.5f, .5f});
            actions->addChild(viewItem);

            if (comment.canEdit) {
                auto button = ButtonSprite::create(
                    "Edit", "goldFont.fnt", "GJ_button_01.png", 1.f
                );
                button->setScale(.34f);
                auto editItem = CCMenuItemExt::createSpriteExtra(
                    button, [this, comment](auto) { beginEdit(comment); }
                );
                editItem->setAnchorPoint({.5f, .5f});
                actions->addChild(editItem);
            }
            if (comment.canDelete) {
                auto button = ButtonSprite::create(
                    "Delete", "goldFont.fnt", "GJ_button_06.png", 1.f
                );
                button->setScale(.34f);
                auto deleteItem = CCMenuItemExt::createSpriteExtra(
                    button, [this, comment](auto) { deleteComment(comment.id); }
                );
                deleteItem->setAnchorPoint({.5f, .5f});
                actions->addChild(deleteItem);
            }
            actions->updateLayout();
            header->addChild(actions);
            header->updateLayout();

            // Estimated line count height calculation and non-scrollable body
            auto const& text = comment.body.empty()
                ? std::string("...")
                : comment.body;

            constexpr float lineHeight = 12.f;
            constexpr float charsPerLine = 55.f;
            constexpr float minHeight = 24.f;
            constexpr float maxHeight = 80.f;

            float estimatedLines = 0.f;

            for (auto const& line : utils::string::split(text, "\n")) {
                estimatedLines += std::max(
                    1.f,
                    std::ceil(static_cast<float>(line.size()) / charsPerLine)
                );
            }

            auto bodyHeight = std::clamp(
                estimatedLines * lineHeight,
                minHeight,
                maxHeight
            );

            auto body = MDTextArea::create(
                text,
                {width - 18.f, bodyHeight},
                true
            );

            body->setContentSize({
                width - 18.f,
                bodyHeight
            });

            body->setAnchorPoint({.5f, .5f});
            body->setScale(1.05f);
            body->getScrollLayer()->m_cutContent = false;
            body->getScrollLayer()->m_disableMovement = true;
            body->getScrollLayer()->setMouseEnabled(false);
            if (auto bodyBG = body->getChildByType<CCScale9Sprite>(0))
                bodyBG->setVisible(false);

            auto attachmentArea = CCMenu::create();
            attachmentArea->setAnchorPoint({.5f, .5f});
            attachmentArea->ignoreAnchorPointForPosition(false);
            attachmentArea->setContentSize({
                width - 18.f,
                comment.attachments.empty() ? 0.f : kAttachmentAreaHeight
            });

            if (!comment.attachments.empty()) {
                auto areaWidth = attachmentArea->getContentWidth();
                auto areaHeight = attachmentArea->getContentHeight();
                auto total = static_cast<int>(comment.attachments.size());

                auto populate = std::make_shared<std::function<void()>>();
                *populate = [
                    this,
                    commentID = comment.id,
                    attachments = comment.attachments,
                    area = attachmentArea,
                    areaWidth,
                    areaHeight
                ]() {
                    while (area->getChildByTag(kAttachmentImageTag))
                        area->removeChildByTag(kAttachmentImageTag);

                    auto& offset = m_attachmentOffsets[commentID];
                    auto count = static_cast<int>(attachments.size());
                    offset = std::clamp(
                        offset, 0, std::max(0, count - kAttachmentPageSize)
                    );
                    auto visible = std::min(kAttachmentPageSize, count - offset);

                    float gap = 4.f;
                    float span = static_cast<float>(visible) * kThumbSize
                        + static_cast<float>(std::max(0, visible - 1)) * gap;
                    float x = (areaWidth - span) / 2.f;

                    for (int i = 0; i < visible; ++i) {
                        auto const& attachment = attachments[offset + i];
                        auto box = createAttachmentBox(kThumbSize, attachment.url);
                        auto item = CCMenuItemExt::createSpriteExtra(
                            box,
                            [this, url = attachment.url](auto) {
                                showAttachmentImage(url);
                            }
                        );
                        item->setTag(kAttachmentImageTag);
                        item->setPosition({x + kThumbSize / 2.f, areaHeight / 2.f});
                        area->addChild(item);
                        x += kThumbSize + gap;
                    }
                };

                if (total > kAttachmentPageSize) {
                    auto makeArrow = [
                        this, populate, commentID = comment.id, areaWidth, areaHeight
                    ](bool right) {
                        auto arrow = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
                        CCNode* image = arrow
                            ? static_cast<CCNode*>(arrow)
                            : static_cast<CCNode*>(
                                CCLabelBMFont::create(right ? ">" : "<", "bigFont.fnt")
                            );
                        if (arrow) arrow->setFlipX(right);

                        auto item = CCMenuItemExt::createSpriteExtra(
                            image,
                            [this, populate, commentID, right](auto) {
                                m_attachmentOffsets[commentID] += right ? 1 : -1;
                                (*populate)();
                            }
                        );
                        item->setScale(.6f);
                        item->m_baseScale = .6f;
                        auto half = item->getScaledContentSize().width / 2.f;
                        item->setPosition({
                            right ? areaWidth - half : half,
                            areaHeight / 2.f
                        });
                        return item;
                    };
                    attachmentArea->addChild(makeArrow(false));
                    attachmentArea->addChild(makeArrow(true));
                }

                (*populate)();
            }

            auto attachmentHeight = comment.attachments.empty()
                ? 0.f
                : kAttachmentAreaHeight + 4.f;

            auto cardHeight =
                36.f +
                4.f +
                bodyHeight +
                attachmentHeight;

            auto card = CCNode::create();
            card->setContentSize({width, cardHeight});
            card->setAnchorPoint({.5f, .5f});
            card->setLayout(AnchorLayout::create());

            auto cardBG = NineSlice::create("square02b_001.png");
            cardBG->setColor(ccBLACK);
            cardBG->setOpacity(75);
            cardBG->setScale(.3f);
            cardBG->setContentSize(card->getContentSize() / cardBG->getScale());
            card->addChildAtPosition(cardBG, Anchor::Center);

            auto stack = CCNode::create();
            stack->setContentSize({width - 8.f, cardHeight - 8.f});
            stack->setAnchorPoint({.5f, .5f});
            stack->setLayout(ColumnLayout::create()
                ->setAxisAlignment(AxisAlignment::Start)
                ->setCrossAxisAlignment(AxisAlignment::Center)
                ->setAxisReverse(true)
                ->setAutoScale(false)
                ->setGap(4.f)
                ->setPadding(Padding::uniform(2.f)));
            card->addChild(stack);
            stack->setLayoutOptions(
                AnchorLayoutOptions::create()->setAnchor(Anchor::Center)
            );

            stack->addChild(header);
            stack->addChild(body);
            if (!comment.attachments.empty())
                stack->addChild(attachmentArea);

            stack->updateLayout();
            m_commentsContainer->addChild(card);
            totalHeight += cardHeight + 7.f;
        }

        m_commentsContainer->setAnchorPoint({0.f, 0.f});
        m_commentsContainer->setContentSize({
            scroll->getContentWidth(),
            std::max(scroll->getContentHeight(), totalHeight + 8.f)
        });
        m_commentsContainer->updateLayout();

        // Safely trigger scroll->scrollToTop() on the main thread after layout updates
        geode::queueInMainThread([scroll] {
            if (scroll) {
                scroll->scrollToTop();
            }
        });

        if (m_versionButton) {
            auto sprite = typeinfo_cast<ButtonSprite*>(m_versionButton->getNormalImage());
            if (sprite) {
                sprite->setString(
                    m_state.selectedVersion.empty()
                        ? "v-"
                        : fmt::format("v{}", m_state.selectedVersion).c_str()
                );
                m_versionButton->updateSprite();
            }
        }

        if (m_attachmentCountLabel) {
            size_t attachmentCount = m_pendingFiles.size();
            if (m_editingCommentID != 0) {
                for (auto const& c : m_state.comments) {
                    if (c.id != m_editingCommentID) continue;
                    attachmentCount += c.attachments.size();
                    for (auto id : m_removedAttachments) {
                        if (std::any_of(
                                c.attachments.begin(), c.attachments.end(),
                                [id](auto const& a) { return a.id == id; }
                            ) && attachmentCount > 0)
                            --attachmentCount;
                    }
                    break;
                }
            }
            m_attachmentCountLabel->setString(
                attachmentCount > 0
                    ? std::to_string(attachmentCount).c_str()
                    : ""
            );
            m_attachmentCountLabel->setVisible(attachmentCount > 0);
        }

        if (m_exitButton)
            m_exitButton->setVisible(m_editingCommentID != 0);

        auto allowed = canComment();
        m_input->setVisible(allowed);
        m_sendButton->setVisible(allowed);
        updateBottomLayout();

        auto lockText = m_state.lock == "none"
            ? "Unlocked"
            : (m_state.lock == "internal" ? "Internal" : "Locked");
        auto lockDisplayText = m_state.lock == "none"
            ? std::string("Unlocked")
            : fmt::format("{} by {}", lockText, m_state.lockedByName.empty() ? "User" : m_state.lockedByName);
        m_lockLabel->setString(lockDisplayText.c_str());
        m_lockLabel->limitLabelWidth(m_lockLabelMaxWidth, .42f, .1f);
        auto lockColor = ccGREEN;
        if (m_state.lock == "internal")
            lockColor = cc3bFromHexString("00D9FF").unwrapOr(ccGREEN);
        else if (m_state.lock == "locked")
            lockColor = ccRED;
        m_lockLabel->setColor(lockColor);
        if (m_lockControls)
            m_lockControls->setVisible(m_state.currentDeveloperAdmin);
    
}
void CommentsLayer::addAvatar(CCNode* avatar, CommentData const& comment) {

        if (comment.pfp.empty()) return;

        auto key = std::hash<std::string>{}(comment.pfp);
        auto path = Mod::get()->getSaveDir() /
            fmt::format("pfp-{:x}.png", key);

        if (createContainedImage(
                avatar, {28.f, 28.f}, comment.pfp, &path
            ))
            return;

        createContainedImage(avatar, {28.f, 28.f}, comment.pfp);
    
}

CommentsLayer* CommentsLayer::create(
    std::string modID,
    CCNode* textArea
) {
    auto ret = new CommentsLayer();
    if (ret && ret->init(std::move(modID), textArea)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

} // namespace opengeode
