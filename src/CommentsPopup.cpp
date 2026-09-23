#include "CommentsPopup.hpp"
#include "Settings.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/MDTextArea.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include <Geode/ui/LazySprite.hpp>
#include <Geode/utils/ColorProvider.hpp>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

using namespace geode::prelude;

class OpenGeodeTabSprite : public CCNode {
    NineSlice* m_deselectedBG = nullptr;
    NineSlice* m_selectedBG = nullptr;
    CCSprite* m_icon = nullptr;
    CCLabelBMFont* m_label = nullptr;

    bool init(char const* iconFrame, char const* text, float width) {
        if (!CCNode::init())
            return false;

        CCSize itemSize { width, 35.f };
        CCSize iconSize { 18.f, 18.f };
        setContentSize(itemSize);
        setAnchorPoint({ .5f, .5f });

        m_deselectedBG = NineSlice::createWithSpriteFrameName("tab-bg.png"_spr);
        if (!m_deselectedBG) return false;
        m_deselectedBG->setScale(.8f);
        m_deselectedBG->setContentSize(itemSize / .8f);
        m_deselectedBG->setColor("mod-list-tab-deselected-bg"_cc3b);
        addChildAtPosition(m_deselectedBG, Anchor::Center);

        m_selectedBG = NineSlice::createWithSpriteFrameName("tab-bg.png"_spr);
        if (!m_selectedBG) return false;
        m_selectedBG->setScale(.8f);
        m_selectedBG->setContentSize(itemSize / .8f);
        m_selectedBG->setColor(to3B(ColorProvider::get()->color("mod-list-tab-selected-bg"_spr)));
        addChildAtPosition(m_selectedBG, Anchor::Center);

        m_icon = CCSprite::createWithSpriteFrameName(iconFrame);
        if (!m_icon) return false;
        limitNodeSize(m_icon, iconSize, 3.f, .1f);
        addChildAtPosition(m_icon, Anchor::Left, ccp(16, 0), false);

        m_label = CCLabelBMFont::create(text, "bigFont.fnt");
        m_label->limitLabelWidth(getContentWidth() - 45.f, std::clamp(width * .0045f, .35f, .55f), .1f);
        m_label->setAnchorPoint({ .5f, .5f });
        addChildAtPosition(m_label, Anchor::Left, ccp((itemSize.width - iconSize.width) / 2 + iconSize.width, 0), false);
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

namespace opengeode {
namespace {

struct CommentAttachment {
    int id = 0;
    std::string url;
};

struct CommentData {
    int id = 0;
    std::string body;
    int authorID = 0;
    std::string username;
    std::string pfp;
    bool canEdit = false;
    bool canDelete = false;
    std::vector<CommentAttachment> attachments;
};

struct CommentState {
    std::vector<CommentData> comments;
    std::vector<std::string> versions;
    std::vector<int> modDeveloperIDs;
    std::string selectedVersion;
    std::string lock = "none";
    int lockedBy = 0;
    std::string lockedByName;
    int currentDeveloperID = 0;
    bool currentDeveloperAdmin = false;
    bool currentDeveloperVerified = false;
    bool currentDeveloperModDeveloper = false;
    bool loggedIn = false;
};

std::string trimSlash(std::string url) {
    while (!url.empty() && url.back() == '/') url.pop_back();
    return url;
}

std::string errorText(web::WebResponse const& response) {
    if (auto json = response.json()) {
        for (auto const* key : {"error", "detail", "message"}) {
            auto value = (*json)[key].asString().unwrapOr("");
            if (!value.empty()) return value;
        }
    }
    if (response.code() > 0) return fmt::format("HTTP {}", response.code());
    if (!response.errorMessage().empty()) return std::string(response.errorMessage());
    return "Request failed.";
}

std::string stringValue(matjson::Value const& value, char const* key, std::string fallback = "") {
    return value[key].asString().unwrapOr(fallback);
}

int intValue(matjson::Value const& value, char const* key, int fallback = 0) {
    return value[key].asInt().unwrapOr(fallback);
}

std::string getModID(CCNode* popup) {
    auto label = typeinfo_cast<CCLabelBMFont*>(popup->getChildByIDRecursive("mod-id-label"));
    if (!label) return "";

    auto value = std::string(label->getString());
    constexpr char const* prefix = "(ID: ";
    if (!value.starts_with(prefix)) return "";

    value.erase(0, 5);
    if (!value.empty() && value.back() == ')') value.pop_back();
    return value;
}


class VersionSelectPopup : public Popup {
    std::vector<std::string> m_versions;
    std::function<void(std::string)> m_onSelect;

    bool init(
        std::vector<std::string> versions,
        std::function<void(std::string)> cb
    ) {
        if (!Popup::init(250.f, 240.f)) return false;

        m_versions = std::move(versions);
        m_onSelect = std::move(cb);
        setTitle("Select Version");

        auto root = CCNode::create();
        root->setContentSize({224.f, 194.f});
        root->setAnchorPoint({.5f, .5f});
        root->setLayout(
            ColumnLayout::create()
                ->setAxisAlignment(AxisAlignment::Center)
                ->setCrossAxisAlignment(AxisAlignment::Center)
                ->setGap(4.f)
        );

        auto scroll = ScrollLayer::create({224.f, 184.f});
        auto content = scroll->m_contentLayer;
        content->setLayout(
            ColumnLayout::create()
                ->setAxisAlignment(AxisAlignment::Start)
                ->setCrossAxisAlignment(AxisAlignment::Center)
                ->setGap(4.f)
                ->setPadding(Padding::uniform(4.f))
        );

        for (auto const& version : m_versions) {
            auto button = ButtonSprite::create(
                version.c_str(),
                "bigFont.fnt",
                "GJ_button_01.png",
                .40f
            );
            button->setScale(.40f);

            auto item = CCMenuItemExt::createSpriteExtra(
                button,
                [this, version](auto) {
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

            auto menu = CCMenu::create();
            menu->setContentSize({210.f, 28.f});
            menu->setLayout(
                RowLayout::create()
                    ->setAxisAlignment(AxisAlignment::Center)
                    ->setCrossAxisAlignment(AxisAlignment::Center)
            );
            menu->addChild(item);
            menu->updateLayout();
            content->addChild(menu);
        }

        content->setContentSize({
            224.f,
            std::max(
                184.f,
                8.f + 32.f * static_cast<float>(m_versions.size())
            )
        });
        content->updateLayout();

        root->addChild(scroll);
        root->updateLayout();
        m_mainLayer->addChildAtPosition(root, Anchor::Center);
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
    std::string m_url;

    bool init(std::string url) {
        if (!Popup::init(350.f, 300.f)) return false;
        m_url = std::move(url);
        setTitle("Attachment");

        auto holder = CCNode::create();
        holder->setContentSize({320.f, 250.f});
        holder->setAnchorPoint({.5f, .5f});
        holder->setLayout(RowLayout::create()
            ->setAxisAlignment(AxisAlignment::Center)
            ->setCrossAxisAlignment(AxisAlignment::Center));
        m_mainLayer->addChildAtPosition(holder, Anchor::Center);

        auto image = LazySprite::create({320.f, 250.f}, false);
        if (image) {
            image->loadFromUrl(m_url);
            image->setLoadCallback([image](Result<> result) {
                if (result) limitNodeSize(image, {320.f, 250.f}, 1.f, .1f);
                else image->setVisible(false);
            });
            holder->addChild(image);
            holder->updateLayout();
        }
        m_noElasticity = true;
        return true;
    }

public:
    static AttachmentImagePopup* create(std::string url) {
        auto ret = new AttachmentImagePopup();
        if (ret && ret->init(std::move(url))) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

class AttachmentPopup : public Popup {
    std::function<void(int)> m_onDelete;
    std::function<void()> m_onAdd;

    static void addPreview(CCNode* row, std::string const& url, std::filesystem::path const* localPath) {
        CCNode* previewNode = nullptr;

        std::error_code ec;
        if (localPath && std::filesystem::exists(*localPath, ec) && !ec) {
            auto pathString = pathToString(*localPath);
            auto sprite = CCSprite::create(pathString.c_str());
            if (sprite) {
                limitNodeSize(sprite, {36.f, 36.f}, 1.f, .1f);
                previewNode = sprite;
            }
        }
        else if (!url.empty()) {
            auto preview = LazySprite::create({36.f, 36.f}, false);
            if (preview) {
                preview->loadFromUrl(url);
                preview->setLoadCallback([preview](Result<> result) {
                    if (!result) {
                        preview->setVisible(false);
                        return;
                    }
                    limitNodeSize(preview, {36.f, 36.f}, 1.f, .1f);
                });
                previewNode = preview;
            }
        }

        if (previewNode)
            row->addChildAtPosition(previewNode, Anchor::Center, ccp(0.f, 0.f), false);
    }

    bool init(
        std::vector<CommentAttachment> attachments,
        std::vector<std::filesystem::path> pending,
        std::function<void(int)> cb,
        std::function<void()> onAdd
    ) {
        if (!Popup::init(300.f, 220.f)) return false;

        m_onDelete = std::move(cb);
        m_onAdd = std::move(onAdd);
        setTitle("Attachments");

        auto root = CCNode::create();
        root->setContentSize({278.f, 168.f});
        root->setAnchorPoint({.5f, .5f});
        root->setLayout(ColumnLayout::create()
            ->setAxisAlignment(AxisAlignment::Between)
            ->setCrossAxisAlignment(AxisAlignment::Center)
            ->setPadding(Padding::symmetric(0.f, 5.f))
            ->setGap(5.f));
        m_mainLayer->addChildAtPosition(root, Anchor::Center);

        auto scroll = ScrollLayer::create({270.f, 122.f});
        scroll->setAnchorPoint({.5f, .5f});
        auto content = scroll->m_contentLayer;
        content->setLayout(ColumnLayout::create()
            ->setAxisAlignment(AxisAlignment::Start)
            ->setCrossAxisAlignment(AxisAlignment::Center)
            ->setGap(6.f)
            ->setPadding(Padding::uniform(5.f)));

        auto addRow = [this, content](
            std::string name, std::string status, std::string url,
            std::filesystem::path const* localPath, int attachmentID
        ) {
            auto row = CCNode::create();
            row->setContentSize({258.f, 46.f});
            row->setLayout(AnchorLayout::create());

            auto bg = NineSlice::create("square02b_001.png");
            bg->setColor(ccBLACK);
            bg->setOpacity(70);
            bg->setScale(.3f);
            bg->setContentSize(row->getContentSize() / bg->getScale());
            row->addChildAtPosition(bg, Anchor::Center);

            auto rowContent = CCNode::create();
            rowContent->setContentSize(row->getContentSize());
            rowContent->setLayout(RowLayout::create()
                ->setAxisAlignment(AxisAlignment::Between)
                ->setCrossAxisAlignment(AxisAlignment::Center)
                ->setPadding(Padding::horizontal(6.f)));
            row->addChild(rowContent);
            rowContent->setLayoutOptions(AnchorLayoutOptions::create()->setAnchor(Anchor::Center));

            auto previewHolder = CCNode::create();
            previewHolder->setContentSize({44.f, 42.f});
            previewHolder->setLayout(RowLayout::create()
                ->setAxisAlignment(AxisAlignment::Center)
                ->setCrossAxisAlignment(AxisAlignment::Center));
            rowContent->addChild(previewHolder);
            addPreview(previewHolder, url, localPath);

            auto info = CCNode::create();
            info->setContentSize({150.f, 42.f});
            info->setLayout(ColumnLayout::create()
                ->setAxisAlignment(AxisAlignment::Center)
                ->setCrossAxisAlignment(AxisAlignment::Start)
                ->setGap(1.f));
            auto nameLabel = CCLabelBMFont::create(name.c_str(), "bigFont.fnt");
            nameLabel->setScale(.31f);
            nameLabel->limitLabelWidth(218.f, .30f, .1f);
            info->addChild(nameLabel);
            if (!status.empty()) {
                auto statusLabel = CCLabelBMFont::create(status.c_str(), "chatFont.fnt");
                statusLabel->setScale(.24f);
                statusLabel->limitLabelWidth(218.f, .24f, .1f);
                info->addChild(statusLabel);
            }
            info->updateLayout();
            rowContent->addChild(info);

            if (attachmentID != 0) {
                auto actions = CCMenu::create();
                actions->setContentSize({48.f, 42.f});
                actions->setLayout(RowLayout::create()
                    ->setAxisAlignment(AxisAlignment::End)
                    ->setCrossAxisAlignment(AxisAlignment::Center));
                auto del = ButtonSprite::create("Delete", "goldFont.fnt", "GJ_button_06.png", .32f);
                del->setScale(.32f);
                actions->addChild(CCMenuItemExt::createSpriteExtra(
                    del, [this, attachmentID](auto) {
                        if (m_onDelete) m_onDelete(attachmentID);
                    }
                ));
                actions->updateLayout();
                rowContent->addChild(actions);
            }

            content->addChild(row);
        };

        for (auto const& attachment : attachments)
            addRow(fmt::format("Attachment {}", attachment.id), "", attachment.url, nullptr, attachment.id);
        for (auto const& path : pending)
            addRow(pathToString(path.filename()), "Pending upload", "", &path, 0);

        if (attachments.empty() && pending.empty()) {
            auto empty = CCLabelBMFont::create("No attachments.", "chatFont.fnt");
            empty->setScale(.34f);
            content->addChild(empty);
        }

        content->updateLayout();
        content->setContentSize({
            270.f,
            std::max(
                122.f,
                10.f + 52.f * static_cast<float>(attachments.size() + pending.size())
            )
        });
        root->addChild(scroll);

        auto addMenu = CCMenu::create();
        addMenu->setContentSize({270.f, 40.f});
        addMenu->setLayout(RowLayout::create()
            ->setAxisAlignment(AxisAlignment::Center)
            ->setCrossAxisAlignment(AxisAlignment::Center));
        auto add = ButtonSprite::create("Add image", "goldFont.fnt", "GJ_button_01.png", .48f);
        add->setScale(.48f);
        addMenu->addChild(CCMenuItemExt::createSpriteExtra(add, [this](auto) {
            if (m_onAdd) {
                m_onAdd();
                removeFromParent();
            }
        }));
        addMenu->updateLayout();
        root->addChild(addMenu);
        root->updateLayout();
        return true;
    }

public:
    static AttachmentPopup* create(
        std::vector<CommentAttachment> attachments,
        std::vector<std::filesystem::path> pending,
        std::function<void(int)> cb,
        std::function<void()> onAdd
    ) {
        auto ret = new AttachmentPopup();
        if (ret && ret->init(
            std::move(attachments),
            std::move(pending),
            std::move(cb),
            std::move(onAdd)
        )) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

class CommentsLayer : public CCLayer {
    std::string m_modID;
    CCNode* m_textArea = nullptr;
    CommentState m_state;

    async::TaskHolder<web::WebResponse> m_requestTask;
    std::vector<std::filesystem::path> m_pendingFiles;
    std::vector<int> m_removedAttachments;

    int m_editingCommentID = 0;

    CCLabelBMFont* m_versionLabel = nullptr;
    CCLabelBMFont* m_lockLabel = nullptr;
    CCMenuItemSpriteExtra* m_lockButton = nullptr;
    TextInput* m_input = nullptr;
    CCMenuItemSpriteExtra* m_sendButton = nullptr;
    CCLabelBMFont* m_attachmentLabel = nullptr;
    CCLabelBMFont* m_statusLabel = nullptr;
    CCMenuItemSpriteExtra* m_exitEditButton = nullptr;
    CCNode* m_commentsContainer = nullptr;
    CCNode* m_bottom = nullptr;
    CCMenu* m_exitEditMenu = nullptr;
    std::unordered_map<int, int> m_attachmentOffsets;

    bool init(std::string modID, CCNode* textArea) {
        if (!CCLayer::init()) return false;

        m_modID = std::move(modID);
        m_textArea = textArea;
        setContentSize(textArea->getContentSize());
        setAnchorPoint({0.f, 0.f});
        setKeyboardEnabled(true);

        auto width = getContentWidth();
        auto height = getContentHeight();

        auto root = CCNode::create();
        root->setContentSize(getContentSize());
        root->setLayout(AnchorLayout::create());
        addChild(root);

        auto bg = NineSlice::create("square02b_001.png");
        bg->setColor(ccBLACK);
        bg->setOpacity(105);
        bg->setScale(.3f);
        bg->setContentSize(getContentSize() / bg->getScale());
        root->addChildAtPosition(bg, Anchor::Center);

        // TOP: status on the left, lock controls on the right.
        auto top = CCNode::create();
        top->setContentSize({width - 8.f, 44.f});
        top->setAnchorPoint({.5f, .5f});
        top->setLayout(RowLayout::create()
            ->setAxisAlignment(AxisAlignment::Between)
            ->setCrossAxisAlignment(AxisAlignment::Center)
            ->setPadding(Padding::horizontal(8.f))
            ->setGap(5.f));
        root->addChild(top);
        top->setLayoutOptions(AnchorLayoutOptions::create()
            ->setAnchor(Anchor::Top)
            ->setOffset(ccp(0.f, -3.f)));

        m_lockLabel = CCLabelBMFont::create("Unlocked", "goldFont.fnt");
        m_lockLabel->setScale(.38f);
        top->addChild(m_lockLabel);

        auto lockControls = CCMenu::create();
        lockControls->setContentSize({205.f, 40.f});
        lockControls->setAnchorPoint({.5f, .5f});
        lockControls->setLayout(RowLayout::create()
            ->setAxisAlignment(AxisAlignment::End)
            ->setCrossAxisAlignment(AxisAlignment::Center)
            ->setGap(4.f));

        auto makeLockButton = [this, lockControls](
            char const* text, char const* value, char const* texture
        ) {
            auto sprite = ButtonSprite::create(
                text, "goldFont.fnt", texture, .40f
            );
            sprite->setScale(.40f);
            lockControls->addChild(CCMenuItemExt::createSpriteExtra(
                sprite, [this, value](auto) { setLock(value); }
            ));
        };

        // Use the existing button textures for the lock states instead of recoloring sprites.
        makeLockButton("Lock", "locked", "GJ_button_06.png");
        makeLockButton("Internal", "internal", "GJ_button_02.png");
        makeLockButton("Unlock", "none", "GJ_button_01.png");
        lockControls->updateLayout();
        top->addChild(lockControls);
        top->updateLayout();

        // MIDDLE: comments only.
        auto commentsArea = CCNode::create();
        commentsArea->setContentSize({
            width - 8.f,
            std::max(1.f, height - 116.f)
        });
        commentsArea->setAnchorPoint({.5f, .5f});
        commentsArea->setLayout(AnchorLayout::create());
        root->addChild(commentsArea);
        commentsArea->setLayoutOptions(
            AnchorLayoutOptions::create()->setAnchor(Anchor::Center)
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

        // BOTTOM: v1.0.0 [Versions] [Exit Edit] [+] [input] [Send]
        auto bottom = CCNode::create();
        bottom->setContentSize({width - 8.f, 62.f});
        bottom->setAnchorPoint({.5f, .5f});
        bottom->setLayout(RowLayout::create()
            ->setAxisAlignment(AxisAlignment::Center)
            ->setCrossAxisAlignment(AxisAlignment::Center)
            ->setPadding(Padding::horizontal(8.f))
            ->setGap(5.f));
        root->addChild(bottom);
        m_bottom = bottom;
        bottom->setLayoutOptions(
            AnchorLayoutOptions::create()
                ->setAnchor(Anchor::Bottom)
                ->setOffset(ccp(0.f, 3.f))
        );

        auto bottomBg = NineSlice::create("square02b_001.png");
        bottomBg->setColor(ccBLACK);
        bottomBg->setOpacity(115);
        bottomBg->setScale(.3f);
        bottomBg->setContentSize(bottom->getContentSize() / bottomBg->getScale());
        root->addChildAtPosition(
            bottomBg, Anchor::Bottom, ccp(0.f, 3.f), false
        );

        auto versionGroup = CCNode::create();
        versionGroup->setContentSize({132.f, 42.f});
        versionGroup->setAnchorPoint({.5f, .5f});
        versionGroup->setLayout(RowLayout::create()
            ->setAxisAlignment(AxisAlignment::Center)
            ->setCrossAxisAlignment(AxisAlignment::Center)
            ->setGap(5.f));

        m_versionLabel = CCLabelBMFont::create("v-", "chatFont.fnt");
        m_versionLabel->setScale(.30f);
        versionGroup->addChild(m_versionLabel);

        auto versionItem = CCMenuItemExt::createSpriteExtra(
            OpenGeodeTabSprite::create("version.png"_spr, "Versions", 104.f),
            [this](auto) { showVersionPicker(); }
        );
        auto versionMenu = CCMenu::create();
        versionMenu->setContentSize({108.f, 42.f});
        versionMenu->setAnchorPoint({.5f, .5f});
        versionMenu->setLayout(RowLayout::create()
            ->setAxisAlignment(AxisAlignment::Center)
            ->setCrossAxisAlignment(AxisAlignment::Center));
        versionMenu->addChild(versionItem);
        versionMenu->updateLayout();
        versionGroup->addChild(versionMenu);
        versionGroup->updateLayout();
        bottom->addChild(versionGroup);

        auto exitSprite = ButtonSprite::create(
            "Exit Edit", "goldFont.fnt", "GJ_button_06.png", .28f
        );
        exitSprite->setScale(.28f);
        m_exitEditButton = CCMenuItemExt::createSpriteExtra(
            exitSprite, [this](auto) { exitEditMode(); }
        );

        m_exitEditMenu = CCMenu::create();
        m_exitEditMenu->setContentSize({82.f, 42.f});
        m_exitEditMenu->setAnchorPoint({.5f, .5f});
        m_exitEditMenu->setLayout(RowLayout::create()
            ->setAxisAlignment(AxisAlignment::Center)
            ->setCrossAxisAlignment(AxisAlignment::Center));
        m_exitEditMenu->addChild(m_exitEditButton);
        m_exitEditMenu->updateLayout();
        bottom->addChild(m_exitEditMenu);

        auto attachSprite = CCSprite::createWithSpriteFrameName("GJ_plusBtn_001.png");
        if (attachSprite)
            limitNodeSize(attachSprite, {26.f, 26.f}, 1.f, .1f);

        auto attachItem = CCMenuItemExt::createSpriteExtra(
            attachSprite ? static_cast<CCNode*>(attachSprite)
                         : static_cast<CCNode*>(CCLabelBMFont::create("+", "bigFont.fnt")),
            [this](auto) { showAttachmentsPopup(); }
        );
        auto attachMenu = CCMenu::create();
        attachMenu->setContentSize({42.f, 42.f});
        attachMenu->setAnchorPoint({.5f, .5f});
        attachMenu->setLayout(RowLayout::create()
            ->setAxisAlignment(AxisAlignment::Center)
            ->setCrossAxisAlignment(AxisAlignment::Center));
        attachMenu->addChild(attachItem);
        attachMenu->updateLayout();
        bottom->addChild(attachMenu);

        m_input = TextInput::create(100.f, "Write a comment...", "chatFont.fnt");
        m_input->setID("opengeode-comment-input"_spr);
        m_input->setCommonFilter(CommonFilter::Any);
        m_input->setMaxCharCount(2000);
        bottom->addChild(m_input);

        auto send = ButtonSprite::create(
            "Send", "goldFont.fnt", "GJ_button_01.png", .48f
        );
        send->setScale(.48f);
        m_sendButton = CCMenuItemExt::createSpriteExtra(
            send, [this](auto) { submitComment(); }
        );
        auto sendMenu = CCMenu::create();
        sendMenu->setContentSize({68.f, 42.f});
        sendMenu->setAnchorPoint({.5f, .5f});
        sendMenu->setLayout(RowLayout::create()
            ->setAxisAlignment(AxisAlignment::Center)
            ->setCrossAxisAlignment(AxisAlignment::Center));
        sendMenu->addChild(m_sendButton);
        sendMenu->updateLayout();
        bottom->addChild(sendMenu);

        m_statusLabel = CCLabelBMFont::create("", "chatFont.fnt");
        m_statusLabel->setScale(.20f);
        m_statusLabel->setAnchorPoint({1.f, .5f});
        root->addChild(m_statusLabel);
        m_statusLabel->setLayoutOptions(
            AnchorLayoutOptions::create()
                ->setAnchor(Anchor::TopRight)
                ->setOffset(ccp(-8.f, -48.f))
        );

        m_exitEditButton->setVisible(false);
        root->updateLayout();
        top->updateLayout();
        commentsArea->updateLayout();
        bottom->updateLayout();
        load();
        return true;
    }

    void updateBottomLayout() {
        if (!m_bottom || !m_input || !m_exitEditMenu) return;

        constexpr float versionWidth = 132.f;
        constexpr float exitWidth = 82.f;
        constexpr float attachWidth = 42.f;
        constexpr float sendWidth = 68.f;
        constexpr float gaps = 5.f * 5.f;
        constexpr float horizontalPadding = 16.f;

        auto available = m_bottom->getContentWidth()
            - versionWidth - attachWidth - sendWidth
            - horizontalPadding - gaps;

        if (m_exitEditButton && m_exitEditButton->isVisible())
            available -= exitWidth;

        m_input->setContentSize({
            std::max(80.f, available),
            34.f
        });

        m_exitEditMenu->setContentSize({
            m_exitEditButton && m_exitEditButton->isVisible() ? exitWidth : 0.f,
            42.f
        });
        m_exitEditMenu->updateLayout();
        m_bottom->updateLayout();
    }

    void request(
        std::string method,
        std::string path,
        std::function<void(web::WebResponse)> callback
    ) {
        auto request = web::WebRequest();
        auto token = getAuthAccessToken();
        if (!token.empty())
            request.header("Authorization", "Bearer " + token);

        m_requestTask.spawn(
            request.send(method, trimSlash(getIndexUrl()) + path),
            [callback = std::move(callback)](web::WebResponse response) mutable {
                callback(std::move(response));
            }
        );
    }

    void load() {
        m_state.loggedIn = hasAuthTokens();
        m_statusLabel->setString("Loading comments...");

        if (!m_state.loggedIn) {
            loadMod();
            return;
        }

        request("GET", "/v1/me", [this](web::WebResponse response) {
            if (response.ok()) {
                auto json = response.json().unwrapOr(matjson::Value());
                auto payload = json["payload"].isObject() ? json["payload"] : json;
                m_state.currentDeveloperID = intValue(payload, "id");
                m_state.currentDeveloperAdmin = payload["admin"].asBool().unwrapOr(false);
                m_state.currentDeveloperVerified = payload["verified"].asBool().unwrapOr(false);
            }
            loadMod();
        });
    }

    void loadMod() {
        request("GET", fmt::format("/v1/mods/{}", m_modID), [this](web::WebResponse response) {
            if (!response.ok()) {
                m_statusLabel->setString(errorText(response).c_str());
                return;
            }

            auto json = response.json().unwrapOr(matjson::Value());
            auto payload = json["payload"].isObject() ? json["payload"] : json;

            m_state.versions.clear();
            m_state.modDeveloperIDs.clear();

            auto developers = payload["developers"];
            if (developers.isArray()) {
                for (auto const& developer : developers) {
                    auto id = intValue(developer, "id");
                    if (id) m_state.modDeveloperIDs.push_back(id);
                }
            }

            m_state.currentDeveloperModDeveloper =
                std::find(
                    m_state.modDeveloperIDs.begin(),
                    m_state.modDeveloperIDs.end(),
                    m_state.currentDeveloperID
                ) != m_state.modDeveloperIDs.end();

            auto versions = payload["versions"];
            if (versions.isArray()) {
                for (auto const& version : versions) {
                    auto value = stringValue(version, "version");
                    if (!value.empty())
                        m_state.versions.push_back(value);
                }
            }

            std::reverse(m_state.versions.begin(), m_state.versions.end());
            if (m_state.selectedVersion.empty() && !m_state.versions.empty())
                m_state.selectedVersion = m_state.versions.front();

            if (m_state.selectedVersion.empty()) {
                rebuild();
                return;
            }

            loadSelectedVersion();
        });
    }

    void loadSelectedVersion() {
        if (m_state.selectedVersion.empty()) return;

        m_statusLabel->setString("Loading submission...");

        request(
            "GET",
            fmt::format(
                "/v1/mods/{}/versions/{}/submission",
                m_modID,
                m_state.selectedVersion
            ),
            [this](web::WebResponse response) {
                if (!response.ok()) {
                    m_state.comments.clear();
                    m_state.lock = "none";
                    m_state.lockedBy = 0;
                    rebuild();
                    m_statusLabel->setString(
                        "This version does not have a submission."
                    );
                    return;
                }

                auto json = response.json().unwrapOr(matjson::Value());
                auto payload = json["payload"].isObject() ? json["payload"] : json;

                m_state.lock = stringValue(payload, "lock", "none");
                auto lockedBy = payload["locked_by"];
                m_state.lockedBy = lockedBy.isObject() ? intValue(lockedBy, "id") : 0;
                m_state.lockedByName = lockedBy.isObject() ? stringValue(lockedBy, "username", "Unknown") : "";

                request(
                    "GET",
                    fmt::format(
                        "/v1/mods/{}/versions/{}/submission/comments",
                        m_modID,
                        m_state.selectedVersion
                    ),
                    [this](web::WebResponse commentsResponse) {
                        if (!commentsResponse.ok()) {
                            m_state.comments.clear();
                            rebuild();
                            m_statusLabel->setString(
                                errorText(commentsResponse).c_str()
                            );
                            return;
                        }

                        parseComments(commentsResponse.json().unwrapOr(matjson::Value()));
                        rebuild();
                    }
                );
            }
        );
    }

    void parseComments(matjson::Value const& json) {
        auto payload = json["payload"].isObject() ? json["payload"] : json;
        auto data = payload["data"];

        m_state.comments.clear();

        if (!data.isArray()) return;

        for (auto const& raw : data) {
            CommentData comment;
            comment.id = intValue(raw, "id");
            comment.body = stringValue(raw, "comment");

            auto author = raw["author"];
            if (author.isObject()) {
                comment.authorID = intValue(author, "id");
                comment.username = stringValue(author, "username", "Unknown");
                auto githubID = intValue(author, "github_id");
                if (githubID > 0)
                    comment.pfp = fmt::format(
                        "https://avatars.githubusercontent.com/u/{}?v=4",
                        githubID
                    );
            }

            comment.canEdit =
                m_state.loggedIn &&
                (comment.authorID == m_state.currentDeveloperID ||
                 m_state.currentDeveloperAdmin);
            comment.canDelete = comment.canEdit;

            auto attachments = raw["attachments"];
            if (attachments.isArray()) {
                for (auto const& attachment : attachments) {
                    CommentAttachment item;
                    item.id = intValue(attachment, "id");
                    item.url = stringValue(attachment, "url");
                    if (item.id && !item.url.empty())
                        comment.attachments.push_back(std::move(item));
                }
            }

            m_state.comments.push_back(std::move(comment));
        }
    }

    bool canComment() const {
        return m_state.loggedIn &&
            (m_state.lock == "none" || m_state.currentDeveloperAdmin);
    }

    bool canUploadAttachments() const {
        return m_state.loggedIn &&
            (m_state.currentDeveloperVerified ||
             m_state.currentDeveloperAdmin ||
             m_state.currentDeveloperModDeveloper);
    }

    void rebuild() {
        if (!m_commentsContainer) return;

        m_commentsContainer->removeAllChildren();
        auto scroll = typeinfo_cast<ScrollLayer*>(
            getChildByIDRecursive("opengeode-comments-scroll"_spr)
        );
        if (!scroll) return;

        auto width = scroll->getContentWidth() - 12.f;
        float totalHeight = 8.f;
        bool any = false;

        m_commentsContainer->setLayout(
            ColumnLayout::create()
                ->setAxisAlignment(AxisAlignment::Start)
                ->setCrossAxisAlignment(AxisAlignment::Center)
                ->setGap(7.f)
                ->setPadding(Padding::uniform(4.f))
        );

        for (auto const& comment : m_state.comments) {
            any = true;

            auto header = CCNode::create();
            header->setContentSize({width - 4.f, 36.f});
            header->setLayout(RowLayout::create()
                ->setAxisAlignment(AxisAlignment::Between)
                ->setCrossAxisAlignment(AxisAlignment::Center)
                ->setPadding(Padding::horizontal(3.f)));

            auto identity = CCNode::create();
            identity->setContentSize({width - 105.f, 36.f});
            identity->setLayout(RowLayout::create()
                ->setAxisAlignment(AxisAlignment::Start)
                ->setCrossAxisAlignment(AxisAlignment::Center)
                ->setGap(7.f));

            auto avatar = CCNode::create();
            avatar->setContentSize({34.f, 34.f});
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
            actions->setContentSize({98.f, 32.f});
            actions->setLayout(RowLayout::create()
                ->setAxisAlignment(AxisAlignment::End)
                ->setCrossAxisAlignment(AxisAlignment::Center)
                ->setGap(4.f));

            if (comment.canEdit) {
                auto button = ButtonSprite::create(
                    "Edit", "goldFont.fnt", "GJ_button_01.png", .27f
                );
                button->setScale(.27f);
                actions->addChild(CCMenuItemExt::createSpriteExtra(
                    button, [this, comment](auto) { beginEdit(comment); }
                ));
            }
            if (comment.canDelete) {
                auto button = ButtonSprite::create(
                    "Delete", "goldFont.fnt", "GJ_button_06.png", .27f
                );
                button->setScale(.27f);
                actions->addChild(CCMenuItemExt::createSpriteExtra(
                    button, [this, comment](auto) { deleteComment(comment.id); }
                ));
            }
            actions->updateLayout();
            header->addChild(actions);
            header->updateLayout();

            auto body = MDTextArea::create(
                comment.body.empty() ? "..." : comment.body,
                {width - 18.f, 66.f},
                true
            );
            body->setScale(1.05f);
            body->getScrollLayer()->m_cutContent = false;
            body->getScrollLayer()->m_disableMovement = true;
            body->getScrollLayer()->setMouseEnabled(false);
            if (auto bodyBG = body->getChildByType<CCScale9Sprite>(0))
                bodyBG->setVisible(false);

            auto attachmentArea = CCNode::create();
            attachmentArea->setContentSize({
                width - 18.f,
                comment.attachments.empty() ? 0.f : 48.f
            });
            attachmentArea->setLayout(RowLayout::create()
                ->setAxisAlignment(AxisAlignment::Center)
                ->setCrossAxisAlignment(AxisAlignment::Center)
                ->setGap(4.f));

            if (!comment.attachments.empty()) {
                auto& offset = m_attachmentOffsets[comment.id];
                auto count = static_cast<int>(comment.attachments.size());
                constexpr int pageSize = 4;
                auto maxOffset = std::max(0, count - pageSize);
                offset = std::clamp(offset, 0, maxOffset);

                auto makeArrow = [this](char const* label, int delta, int commentID) {
                    auto sprite = ButtonSprite::create(
                        label, "bigFont.fnt", "GJ_button_01.png", .24f
                    );
                    sprite->setScale(.24f);
                    return CCMenuItemExt::createSpriteExtra(
                        sprite, [this, delta, commentID](auto) {
                            auto& value = m_attachmentOffsets[commentID];
                            value = std::max(0, value + delta);
                            rebuild();
                        }
                    );
                };

                if (count > pageSize)
                    attachmentArea->addChild(makeArrow("<", -1, comment.id));

                auto images = CCNode::create();
                images->setContentSize({
                    std::min(190.f, width - 70.f), 46.f
                });
                images->setLayout(RowLayout::create()
                    ->setAxisAlignment(AxisAlignment::Start)
                    ->setCrossAxisAlignment(AxisAlignment::Center)
                    ->setGap(5.f));

                auto visible = std::min(pageSize, count - offset);
                for (int i = 0; i < visible; ++i) {
                    auto const& attachment = comment.attachments[offset + i];
                    auto image = LazySprite::create({42.f, 42.f}, false);
                    if (image) {
                        image->loadFromUrl(attachment.url);
                        image->setLoadCallback([image](Result<> result) {
                            if (result)
                                limitNodeSize(image, {42.f, 42.f}, 1.f, .1f);
                            else
                                image->setVisible(false);
                        });
                    }

                    auto item = CCMenuItemExt::createSpriteExtra(
                        image ? static_cast<CCNode*>(image)
                              : static_cast<CCNode*>(CCLabelBMFont::create("?", "chatFont.fnt")),
                        [this, attachment](auto) {
                            showAttachmentImage(attachment.url);
                        }
                    );
                    auto itemMenu = CCMenu::create();
                    itemMenu->setContentSize({44.f, 44.f});
                    itemMenu->setLayout(RowLayout::create()
                        ->setAxisAlignment(AxisAlignment::Center)
                        ->setCrossAxisAlignment(AxisAlignment::Center));
                    itemMenu->addChild(item);
                    itemMenu->updateLayout();
                    images->addChild(itemMenu);
                }
                images->updateLayout();
                attachmentArea->addChild(images);

                if (count > pageSize)
                    attachmentArea->addChild(makeArrow(">", 1, comment.id));

                attachmentArea->updateLayout();
            }

            auto cardHeight = 36.f + 4.f + 66.f +
                (comment.attachments.empty() ? 0.f : 52.f);

            auto card = CCNode::create();
            card->setContentSize({width, cardHeight});
            card->setAnchorPoint({0.f, 0.f});
            card->setLayout(AnchorLayout::create());

            auto cardBG = NineSlice::create("square02b_001.png");
            cardBG->setColor(ccBLACK);
            cardBG->setOpacity(75);
            cardBG->setScale(.3f);
            cardBG->setContentSize(card->getContentSize() / cardBG->getScale());
            card->addChildAtPosition(cardBG, Anchor::Center);

            auto stack = CCNode::create();
            stack->setContentSize({width - 8.f, cardHeight - 8.f});
            stack->setLayout(ColumnLayout::create()
                ->setAxisAlignment(AxisAlignment::Start)
                ->setCrossAxisAlignment(AxisAlignment::Center)
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

        if (!any) {
            auto empty = CCLabelBMFont::create(
                "No comments yet.", "chatFont.fnt"
            );
            empty->setScale(.35f);
            m_commentsContainer->addChild(empty);
            totalHeight += 35.f;
        }

        m_commentsContainer->setAnchorPoint({0.f, 0.f});
        m_commentsContainer->setContentSize({
            scroll->getContentWidth(),
            std::max(scroll->getContentHeight(), totalHeight + 8.f)
        });
        m_commentsContainer->updateLayout();
        scroll->scrollToTop();

        m_versionLabel->setString(
            m_state.selectedVersion.empty()
                ? "v-"
                : fmt::format("v{}", m_state.selectedVersion).c_str()
        );

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
        auto lockColor = ccGREEN;
        if (m_state.lock == "internal")
            lockColor = cc3bFromHexString("00D9FF").unwrapOr(ccGREEN);
        else if (m_state.lock == "locked")
            lockColor = ccRED;
        m_lockLabel->setColor(lockColor);

        if (!m_state.loggedIn)
            m_statusLabel->setString("Log in to comment.");
        else if (m_state.lock == "locked" && !m_state.currentDeveloperAdmin)
            m_statusLabel->setString("This submission is locked.");
        else if (m_state.lock == "internal" && !m_state.currentDeveloperAdmin)
            m_statusLabel->setString("This submission is locked to the index team.");
        else if (!any)
            m_statusLabel->setString("No comments yet.");
        else
            m_statusLabel->setString("");
    }

    void addAvatar(CCNode* avatar, CommentData const& comment) {
        if (comment.pfp.empty()) return;

        auto key = std::hash<std::string>{}(comment.pfp);
        auto path = Mod::get()->getSaveDir() /
            fmt::format("pfp-{:x}.png", key);

        std::error_code ec;
        if (std::filesystem::exists(path, ec) && !ec) {
            auto pathString = pathToString(path);
            auto sprite = CCSprite::create(pathString.c_str());
            if (sprite) {
                limitNodeSize(sprite, {28.f, 28.f}, 1.f, .1f);
                avatar->addChildAtPosition(
                    sprite, Anchor::Center, ccp(0.f, 0.f), false
                );
            }
            return;
        }

        auto sprite = LazySprite::create({28.f, 28.f}, false);
        if (!sprite) return;

        avatar->addChildAtPosition(
            sprite, Anchor::Center, ccp(0.f, 0.f), false
        );
        sprite->setLoadCallback([sprite](Result<> result) {
            if (!result) {
                sprite->setVisible(false);
                return;
            }
            limitNodeSize(sprite, {28.f, 28.f}, 1.f, .1f);
        });
        sprite->loadFromUrl(comment.pfp);
    }

    void showAttachmentImage(std::string const& url) {
        if (url.empty()) return;
        if (auto popup = AttachmentImagePopup::create(url))
            popup->show();
    }

    void showVersionPicker() {
        if(m_state.versions.empty())return;
        auto popup = VersionSelectPopup::create(
            m_state.versions,
            [this](std::string version) {
                if (version == m_state.selectedVersion)
                    return;

                m_state.selectedVersion = std::move(version);
                m_editingCommentID = 0;
                m_pendingFiles.clear();
                m_removedAttachments.clear();
                m_input->setString("");
                m_attachmentLabel->setString("");
                loadSelectedVersion();
            }
        );
        if(popup){popup->m_noElasticity=true;popup->show();}
    }

    void showAttachmentsPopup() {
        std::vector<CommentAttachment> attachments;
        for(auto const& c:m_state.comments)if(c.id==m_editingCommentID){attachments=c.attachments;break;}
        auto popup=AttachmentPopup::create(
            std::move(attachments), m_pendingFiles,
            [this](int id) { removeAttachment(id); },
            [this]() { pickAttachments(); }
        );
        if(popup){popup->m_noElasticity=true;popup->show();}
    }

    void showLockPicker() {
        if (!m_state.currentDeveloperAdmin) return;

        auto popup = createQuickPopup(
            "Submission Lock",
            "Choose who can comment on this submission.",
            "Cancel",
            nullptr,
            nullptr
        );
        if (!popup) return;

        auto menu = CCMenu::create();
        menu->setContentSize({190.f, 150.f});
        menu->setLayout(ColumnLayout::create()->setGap(5.f));

        auto addChoice = [this, popup, menu](
            char const* label, char const* value
        ) {
            auto button = ButtonSprite::create(
                label, "bigFont.fnt",
                "GJ_button_01.png", .36f
            );
            menu->addChild(
                CCMenuItemExt::createSpriteExtra(
                    button,
                    [this, popup, value](auto) {
                        setLock(value);
                        popup->removeFromParent();
                    }
                )
            );
        };

        addChoice("Unlocked", "none");
        addChoice("Internal", "internal");
        addChoice("Locked", "locked");

        popup->m_mainLayer->addChildAtPosition(menu, Anchor::Center);
        popup->show();
    }

    void setLock(std::string value) {
        auto json = matjson::makeObject({
            {"lock", value}
        });

        auto request = web::WebRequest();
        request.header(
            "Authorization",
            "Bearer " + getAuthAccessToken()
        );
        request.bodyJSON(json);

        m_statusLabel->setString("Updating lock...");

        m_requestTask.spawn(
            request.put(
                trimSlash(getIndexUrl()) +
                fmt::format(
                    "/v1/mods/{}/versions/{}/submission",
                    m_modID,
                    m_state.selectedVersion
                )
            ),
            [this](web::WebResponse response) {
                if (!response.ok()) {
                    m_statusLabel->setString(errorText(response).c_str());
                    return;
                }
                loadSelectedVersion();
            }
        );
    }

    void pickAttachments() {
        if (!canUploadAttachments()) {
            m_statusLabel->setString(
                "Attachments require a verified developer, mod developer, or admin."
            );
            return;
        }

        file::FilePickOptions options;
        options.filters.push_back({
            "Images", {"png", "jpg", "jpeg", "gif", "webp"}
        });
        options.filters.push_back({"All Files", {}});

        async::spawn(
            file::pickMany(options),
            [this](file::PickManyResult result) {
                if (!result) return;

                auto files = std::move(result).unwrap();
                if (files.empty())
                    return;

                m_pendingFiles.insert(
                    m_pendingFiles.end(),
                    std::make_move_iterator(files.begin()),
                    std::make_move_iterator(files.end())
                );
                m_attachmentLabel->setString(
                    fmt::format(
                        "{} file{}",
                        m_pendingFiles.size(),
                        m_pendingFiles.size() == 1 ? "" : "s"
                    ).c_str()
                );
            }
        );
    }

    void submitComment() {
        if (m_state.selectedVersion.empty()) {
            m_statusLabel->setString("Select a version first.");
            return;
        }

        if (!canComment()) {
            m_statusLabel->setString("You cannot comment on this submission.");
            return;
        }

        auto text = std::string(m_input->getString().c_str());
        if (text.empty() && m_pendingFiles.empty() && m_editingCommentID == 0) {
            m_statusLabel->setString(
                "Write something or attach an image."
            );
            return;
        }

        if (!hasAuthTokens()) {
            m_statusLabel->setString("Log in to comment.");
            return;
        }

        if (m_editingCommentID != 0) {
            updateExistingComment(text);
            return;
        }

        auto json = matjson::makeObject({
            {"comment", text}
        });

        auto request = web::WebRequest();
        request.header(
            "Authorization",
            "Bearer " + getAuthAccessToken()
        );
        request.bodyJSON(json);

        m_sendButton->setEnabled(false);
        m_statusLabel->setString("Posting...");

        m_requestTask.spawn(
            request.post(
                trimSlash(getIndexUrl()) +
                fmt::format(
                    "/v1/mods/{}/versions/{}/submission/comments",
                    m_modID,
                    m_state.selectedVersion
                )
            ),
            [this](web::WebResponse response) {
                if (!response.ok()) {
                    m_sendButton->setEnabled(true);
                    m_statusLabel->setString(errorText(response).c_str());
                    return;
                }

                auto json = response.json().unwrapOr(matjson::Value());
                auto payload = json["payload"].isObject()
                    ? json["payload"]
                    : json;
                auto commentID = intValue(payload, "id");

                if (commentID == 0) {
                    m_sendButton->setEnabled(true);
                    m_statusLabel->setString(
                        "Comment was created but no ID was returned."
                    );
                    loadSelectedVersion();
                    return;
                }

                finishCommentAttachments(commentID, false);
            }
        );
    }

    void updateExistingComment(std::string const& text) {
        auto json = matjson::makeObject({
            {"comment", text}
        });

        auto request = web::WebRequest();
        request.header(
            "Authorization",
            "Bearer " + getAuthAccessToken()
        );
        request.bodyJSON(json);

        m_sendButton->setEnabled(false);
        m_statusLabel->setString("Saving...");

        m_requestTask.spawn(
            request.put(
                trimSlash(getIndexUrl()) +
                fmt::format(
                    "/v1/mods/{}/versions/{}/submission/comments/{}",
                    m_modID,
                    m_state.selectedVersion,
                    m_editingCommentID
                )
            ),
            [this](web::WebResponse response) {
                if (!response.ok()) {
                    m_sendButton->setEnabled(true);
                    m_statusLabel->setString(errorText(response).c_str());
                    return;
                }

                finishCommentAttachments(m_editingCommentID, true);
            }
        );
    }

    void finishCommentAttachments(int commentID, bool editing) {
        auto finish = [this, editing]() {
            m_pendingFiles.clear();
            m_removedAttachments.clear();
            m_editingCommentID = 0;
            if (m_exitEditButton)
                m_exitEditButton->setVisible(false);
            m_input->setString("");
            m_attachmentLabel->setString("");
            m_sendButton->setEnabled(true);
            m_statusLabel->setString(editing ? "Comment updated." : "Comment posted.");
            loadSelectedVersion();
        };

        if (!m_removedAttachments.empty()) {
            removeNextAttachment(commentID, std::move(finish));
            return;
        }

        if (!m_pendingFiles.empty()) {
            uploadAttachments(commentID, std::move(finish));
            return;
        }

        finish();
    }

    void uploadAttachments(int commentID, std::function<void()> finish) {
        if (!canUploadAttachments()) {
            m_pendingFiles.clear();
            m_statusLabel->setString(
                "Comment saved, but you cannot upload attachments."
            );
            finish();
            return;
        }

        web::MultipartForm form;
        for (auto const& path : m_pendingFiles) {
            auto result = form.file("image", path);
            if (!result) {
                m_sendButton->setEnabled(true);
                m_statusLabel->setString(
                    "Could not read an attachment."
                );
                return;
            }
        }

        auto request = web::WebRequest();
        request.header(
            "Authorization",
            "Bearer " + getAuthAccessToken()
        );
        request.bodyMultipart(form);

        m_statusLabel->setString("Uploading attachments...");

        m_requestTask.spawn(
            request.post(
                trimSlash(getIndexUrl()) +
                fmt::format(
                    "/v1/mods/{}/versions/{}/submission/comments/{}/attachments",
                    m_modID,
                    m_state.selectedVersion,
                    commentID
                )
            ),
            [this, finish = std::move(finish)](web::WebResponse response) mutable {
                if (!response.ok()) {
                    m_sendButton->setEnabled(true);
                    m_statusLabel->setString(
                        fmt::format(
                            "Comment saved, attachment upload failed: {}",
                            errorText(response)
                        ).c_str()
                    );
                    return;
                }
                finish();
            }
        );
    }

    void removeNextAttachment(int commentID, std::function<void()> finish) {
        if (m_removedAttachments.empty()) {
            if (!m_pendingFiles.empty())
                uploadAttachments(commentID, std::move(finish));
            else
                finish();
            return;
        }

        auto attachmentID = m_removedAttachments.back();
        m_removedAttachments.pop_back();

        auto request = web::WebRequest();
        request.header(
            "Authorization",
            "Bearer " + getAuthAccessToken()
        );

        m_requestTask.spawn(
            request.send(
                "DELETE",
                trimSlash(getIndexUrl()) +
                fmt::format(
                    "/v1/mods/{}/versions/{}/submission/comments/{}/attachments/{}",
                    m_modID,
                    m_state.selectedVersion,
                    commentID,
                    attachmentID
                )
            ),
            [this, commentID, finish = std::move(finish)](
                web::WebResponse response
            ) mutable {
                if (!response.ok()) {
                    m_sendButton->setEnabled(true);
                    m_statusLabel->setString(
                        errorText(response).c_str()
                    );
                    return;
                }
                removeNextAttachment(commentID, std::move(finish));
            }
        );
    }

    void exitEditMode() {
        m_editingCommentID = 0;
        m_removedAttachments.clear();
        m_pendingFiles.clear();
        m_input->setString("");
        m_attachmentLabel->setString("");
        m_statusLabel->setString("");
        m_sendButton->setEnabled(true);
        if (m_exitEditButton)
            m_exitEditButton->setVisible(false);
        updateBottomLayout();
        if (m_exitEditMenu) {
            m_exitEditMenu->setContentSize({0.f, 38.f});
            m_exitEditMenu->updateLayout();
        }
        if (m_bottom) m_bottom->updateLayout();
        rebuild();
    }

    void beginEdit(CommentData const& comment) {
        if (!comment.canEdit) return;

        m_editingCommentID = comment.id;
        m_removedAttachments.clear();
        m_pendingFiles.clear();
        m_input->setString(comment.body.c_str());
        m_attachmentLabel->setString("");
        m_statusLabel->setString("Editing comment. Press Send to save.");
        if (m_exitEditButton)
            m_exitEditButton->setVisible(true);
        updateBottomLayout();
        rebuild();
        m_input->focus();
    }

    void removeAttachment(int attachmentID) {
        if (m_editingCommentID == 0) return;

        if (std::find(
                m_removedAttachments.begin(),
                m_removedAttachments.end(),
                attachmentID
            ) == m_removedAttachments.end()) {
            m_removedAttachments.push_back(attachmentID);
        }

        m_statusLabel->setString("Attachment marked for removal.");
    }

    void deleteComment(int id) {
        createQuickPopup(
            "Delete Comment",
            "Delete this comment?",
            "Cancel",
            "Delete",
            [this, id](auto, bool confirmed) {
                if (!confirmed) return;

                auto request = web::WebRequest();
                request.header(
                    "Authorization",
                    "Bearer " + getAuthAccessToken()
                );

                m_requestTask.spawn(
                    request.send(
                        "DELETE",
                        trimSlash(getIndexUrl()) +
                        fmt::format(
                            "/v1/mods/{}/versions/{}/submission/comments/{}",
                            m_modID,
                            m_state.selectedVersion,
                            id
                        )
                    ),
                    [this](web::WebResponse response) {
                        if (!response.ok()) {
                            m_statusLabel->setString(
                                errorText(response).c_str()
                            );
                            return;
                        }
                        loadSelectedVersion();
                    }
                );
            }
        );
    }

public:
    static CommentsLayer* create(
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
};

} // namespace

void ensureCommentsTab(CCNode* popup) {
    if (!popup) return;

    auto modID = getModID(popup);
    if (modID.empty()) return;

    auto tabs = popup->getChildByIDRecursive("tabs-menu");
    auto textarea = popup->getChildByIDRecursive("textarea");
    if (!tabs || !textarea || tabs->getChildByID("opengeode-comments-tab"))
        return;

    auto description = typeinfo_cast<CCMenuItemSpriteExtra*>(
        tabs->getChildByID("description")
    );
    auto changelog = typeinfo_cast<CCMenuItemSpriteExtra*>(
        tabs->getChildByID("changelog")
    );
    if (!description || !changelog) return;

    auto descriptionListener = description->m_pListener;
    auto descriptionSelector = description->m_pfnSelector;
    auto changelogListener = changelog->m_pListener;
    auto changelogSelector = changelog->m_pfnSelector;

    auto commentsSprite = OpenGeodeTabSprite::create(
        "GJ_chatIcon_001.png", "Comments", 140.f
    );
    if (!commentsSprite) return;

    auto callback = [
        modID,
        textarea,
        descriptionListener,
        descriptionSelector,
        changelogListener,
        changelogSelector,
        commentsSprite
    ](CCMenuItemSpriteExtra* sender) {
        auto parent = textarea->getParent();
        if (!parent) return;

        while (auto old = parent->getChildByType<CommentsLayer>(0))
            old->removeFromParent();

        auto tag = sender->getTag();
        if (tag == 0) {
            (descriptionListener->*descriptionSelector)(sender);
            commentsSprite->select(0);
            textarea->setVisible(true);
        }
        else if (tag == 1) {
            (changelogListener->*changelogSelector)(sender);
            commentsSprite->select(0);
            textarea->setVisible(true);
        }
        else {
            commentsSprite->select(1);
            textarea->setVisible(false);

            auto layer = CommentsLayer::create(modID, parent);
            if (layer) {
                parent->addChild(layer);
                layer->setPosition({0.f, 0.f});
            }
        }
    };

    CCMenuItemExt::assignCallback<CCMenuItemSpriteExtra>(
        description, callback
    );
    CCMenuItemExt::assignCallback<CCMenuItemSpriteExtra>(
        changelog, callback
    );

    auto item = CCMenuItemExt::createSpriteExtra(
        commentsSprite, callback
    );
    item->setTag(2);
    item->setID("opengeode-comments-tab");
    item->m_pListener = description->m_pListener;
    tabs->addChild(item);
    tabs->updateLayout();
}

} // namespace opengeode
