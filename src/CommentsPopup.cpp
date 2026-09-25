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
#include <functional>

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

// ---------------------------------------------------------------------------
// Comments window layout constants (top / middle / bottom sections).
// ---------------------------------------------------------------------------
constexpr float kTopHeight = 29.f;
constexpr float kTopInset = 3.f;
constexpr float kBottomHeight = 38.f;
constexpr float kBottomInset = 3.f;
constexpr float kSectionGap = 3.f;

// Auto-sized buttons (text at scale 1 so the button hugs the text, then the
// whole button is scaled). Tweak these two to make the buttons bigger/smaller.
constexpr float kLockButtonScale = .55f;   // Lock / Internal / Unlock (gold Pusab)
constexpr float kBarButtonScale = .65f;    // bottom bar: version / Exit Edit / Send

// Attachment thumbnails.
constexpr float kThumbSize = 36.f;         // in comments
constexpr float kAttachmentAreaHeight = 42.f;
constexpr int kAttachmentPageSize = 4;
constexpr int kAttachmentImageTag = 7701;

struct CommentAttachment {
    int id = 0;
    std::string url;
    std::string filename;
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

LazySprite* createContainedImage(
    CCNode* holder,
    CCSize size,
    std::string const& url,
    std::filesystem::path const* localPath = nullptr
) {
    if (localPath) {
        std::error_code ec;
        if (!std::filesystem::exists(*localPath, ec) || ec)
            return nullptr;
    }
    else if (url.empty()) {
        return nullptr;
    }

    auto sprite = LazySprite::create(size, false);
    if (!sprite) return nullptr;

    holder->addChildAtPosition(sprite, Anchor::Center);
    sprite->setLoadCallback([sprite, size](Result<> result) {
        if (!result) {
            sprite->setVisible(false);
            return;
        }

        auto real = sprite->getContentSize();
        if (auto inner = sprite->getChildByType<CCSprite>(0)) {
            auto innerSize = inner->getScaledContentSize();
            if (innerSize.width > 0.f && innerSize.height > 0.f)
                real = innerSize;
        }
        if (real.width > 0.f && real.height > 0.f) {
            auto fit = std::min(
                1.f,
                std::min(size.width / real.width, size.height / real.height)
            );
            sprite->setScale(fit);
        }
        if (auto parent = sprite->getParent())
            sprite->setPosition(parent->getContentSize() / 2.f);
    });

    if (localPath) sprite->loadFromFile(*localPath);
    else sprite->loadFromUrl(url);

    return sprite;
}

CCNode* createAttachmentBox(
    float size,
    std::string const& url,
    std::filesystem::path const* localPath = nullptr
) {
    auto box = CCNode::create();
    box->setContentSize({size, size});
    box->setAnchorPoint({.5f, .5f});
    box->setLayout(AnchorLayout::create());

    auto bg = NineSlice::create("square02b_001.png");
    bg->setColor(ccBLACK);
    bg->setOpacity(120);
    bg->setScale(.3f);
    bg->setContentSize(box->getContentSize() / bg->getScale());
    box->addChildAtPosition(bg, Anchor::Center);

    createContainedImage(box, {size - 4.f, size - 4.f}, url, localPath);
    return box;
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

// ---------------------------------------------------------------------------
// Popups (Inheriting from Geode v5.0.0 geode::Popup with exact generic parameters)
// ---------------------------------------------------------------------------

class VersionSelectPopup : public geode::Popup<std::vector<std::string>, std::function<void(std::string)>> {
    std::vector<std::string> m_versions;
    std::function<void(std::string)> m_onSelect;

protected:
    bool setup(std::vector<std::string> versions, std::function<void(std::string)> cb) override {
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
                [this, version](CCMenuItemSpriteExtra*) {
                    auto callback = m_onSelect;
                    this->onClose(nullptr);
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
        if (ret && ret->initAnchored(250.f, 240.f, std::move(versions), std::move(cb))) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

class AttachmentImagePopup : public geode::Popup<std::string> {
protected:
    bool setup(std::string url) override {
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
        if (ret && ret->initAnchored(350.f, 260.f, std::move(url))) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

class AttachmentPopup : public geode::Popup<
    std::vector<CommentAttachment>,
    std::vector<std::filesystem::path>,
    std::vector<int>,
    std::function<void(int)>,
    std::function<void(std::filesystem::path const&)>,
    std::function<void()>
> {
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

        auto addRow = [this](std::string name, std::string status,
                             std::string url,
                             std::filesystem::path const* localPath,
                             int attachmentID,
                             bool removed,
                             bool pendingUpload) {
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
            row->addChildAtPosition(
                box, Anchor::Left, ccp(pad + imageSize / 2.f, 0.f)
            );

            auto info = CCNode::create();
            info->setContentSize({infoWidth, imageSize});
            info->setAnchorPoint({0.f, .5f});
            info->setLayout(AnchorLayout::create());
            row->addChildAtPosition(
                info, Anchor::Left, ccp(pad * 2.f + imageSize, 0.f)
            );

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
            row->addChildAtPosition(
                actions, Anchor::Right, ccp(-(pad + actionWidth / 2.f), 0.f)
            );

            auto actionText = removed ? "Restore" : "Remove";
            auto actionTexture = removed ? "GJ_button_01.png" : "GJ_button_06.png";
            auto action = ButtonSprite::create(
                actionText, "goldFont.fnt", actionTexture, 1.f
            );

            CCMenuItemSpriteExtra* actionItem = nullptr;
            if (pendingUpload) {
                actionItem = CCMenuItemExt::createSpriteExtra(
                    action,
                    [this, path = localPath ? *localPath : std::filesystem::path()](CCMenuItemSpriteExtra*) {
                        if (path.empty()) return;
                        if (m_onRemovePending)
                            m_onRemovePending(path);
                        m_pending.erase(
                            std::remove(m_pending.begin(), m_pending.end(), path),
                            m_pending.end()
                        );
                        rebuild();
                    }
                );
            }
            else {
                actionItem = CCMenuItemExt::createSpriteExtra(
                    action,
                    [this, attachmentID](CCMenuItemSpriteExtra*) {
                        if (m_onToggleDelete)
                            m_onToggleDelete(attachmentID);

                        if (isRemoved(attachmentID))
                            m_removed.erase(
                                std::remove(
                                    m_removed.begin(), m_removed.end(), attachmentID
                                ),
                                m_removed.end()
                            );
                        else
                            m_removed.push_back(attachmentID);

                        rebuild();
                    }
                );
            }

            limitNodeSize(actionItem, {actionWidth, imageSize - 6.f}, .7f, .1f);
            actionItem->setPosition(actions->getContentSize() / 2.f);
            actions->addChild(actionItem);

            m_content->addChild(row);
        };

        for (auto const& attachment : m_attachments) {
            auto removed = isRemoved(attachment.id);
            addRow(
                attachment.filename.empty()
                    ? fmt::format("Image {}", attachment.id)
                    : attachment.filename,
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
            auto empty = CCLabelBMFont::create(
                "No attachments.", "chatFont.fnt"
            );
            empty->setScale(.34f);
            empty->setAnchorPoint({.5f, .5f});
            m_content->addChild(empty);
        }

        auto count = m_attachments.size() + m_pending.size();
        m_content->setContentSize({
            m_scroll->getContentWidth(),
            std::max(
                m_scroll->getContentHeight(),
                10.f + (rowHeight + 4.f) * static_cast<float>(count)
            )
        });
        m_content->updateLayout();
    }

protected:
    bool setup(
        std::vector<CommentAttachment> attachments,
        std::vector<std::filesystem::path> pending,
        std::vector<int> removed,
        std::function<void(int)> onToggleDelete,
        std::function<void(std::filesystem::path const&)> onRemovePending,
        std::function<void()> onAdd
    ) override {
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
        listBG->setContentSize(
            CCSize{scrollWidth + 6.f, scrollHeight + 6.f} / listBG->getScale()
        );
        listBG->setPosition({size.width / 2.f, scrollBottom + scrollHeight / 2.f});
        m_mainLayer->addChild(listBG);

        m_scroll = ScrollLayer::create({scrollWidth, scrollHeight});
        m_scroll->setPosition({(size.width - scrollWidth) / 2.f, scrollBottom});
        m_content = m_scroll->m_contentLayer;
        m_content->setAnchorPoint({0.f, 0.f});
        m_content->setLayout(ColumnLayout::create()
            ->setAxisReverse(true)
            ->setAxisAlignment(AxisAlignment::Start)
            ->setCrossAxisAlignment(AxisAlignment::Center)
            ->setAutoScale(false)
            ->setGap(4.f)
            ->setPadding(Padding::uniform(5.f)));
        m_mainLayer->addChild(m_scroll);

        auto add = ButtonSprite::create(
            "+ Add Image", "goldFont.fnt", "GJ_button_01.png", 1.f
        );
        auto addItem = CCMenuItemExt::createSpriteExtra(
            add, [this](CCMenuItemSpriteExtra*) {
                if (m_onAdd) {
                    m_onAdd();
                    this->onClose(nullptr);
                }
            }
        );
        addItem->setScale(.85f);
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
        if (ret && ret->initAnchored(
            300.f, 235.f,
            std::move(attachments),
            std::move(pending),
            std::move(removed),
            std::move(onToggleDelete),
            std::move(onRemovePending),
            std::move(onAdd)
        )) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

class LockSettingsPopup : public geode::Popup<std::function<void(std::string)>> {
protected:
    bool setup(std::function<void(std::string)> onSelect) override {
        setTitle("Lock Settings");
        
        auto menu = CCMenu::create();
        menu->setContentSize({180.f, 100.f});
        menu->setLayout(ColumnLayout::create()
            ->setAxisAlignment(AxisAlignment::Center)
            ->setGap(8.f));

        auto addChoice = [this, onSelect, menu](char const* label, char const* value) {
            auto button = ButtonSprite::create(label, "bigFont.fnt", "GJ_button_01.png", .36f);
            auto item = CCMenuItemExt::createSpriteExtra(
                button, [this, value, onSelect](CCMenuItemSpriteExtra*) {
                    this->onClose(nullptr);
                    onSelect(value);
                }
            );
            menu->addChild(item);
        };

        addChoice("Unlocked", "none");
        addChoice("Locked", "locked");
        addChoice("Internal", "internal");

        menu->updateLayout();
        m_mainLayer->addChildAtPosition(menu, Anchor::Center);
        return true;
    }
public:
    static LockSettingsPopup* create(std::function<void(std::string)> onSelect) {
        auto ret = new LockSettingsPopup();
        if (ret && ret->initAnchored(200.f, 150.f, onSelect)) {
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
    Notification* m_loadingNotification = nullptr;

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

    void notifyStatus(std::string const& message) {
        Notification::create(
            message,
            NotificationIcon::Info,
            1.f
        )->show();
    }

    void showLoading(std::string const& message) {
        if (m_loadingNotification) {
            m_loadingNotification->hide();
        }
        m_loadingNotification = Notification::create(
            message,
            NotificationIcon::Loading,
            0.f
        );
        m_loadingNotification->show();
    }

    void hideLoading() {
        if (m_loadingNotification) {
            m_loadingNotification->hide();
            m_loadingNotification = nullptr;
        }
    }

    bool init(std::string modID, CCNode* textArea) {
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
                sprite, [this, value](CCMenuItemSpriteExtra*) { setLock(value); }
            );
            item->setScale(kLockButtonScale);
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
            versionSprite, [this](CCMenuItemSpriteExtra*) { showVersionPicker(); }
        );
        m_versionButton->setScale(kBarButtonScale);
        bottomMenu->addChild(m_versionButton);

        auto exitSprite = ButtonSprite::create(
            "Exit Edit", "goldFont.fnt", "GJ_button_06.png", 1.f
        );
        m_exitButton = CCMenuItemExt::createSpriteExtra(
            exitSprite, [this](CCMenuItemSpriteExtra*) { exitEdit(); }
        );
        m_exitButton->setScale(kBarButtonScale);
        m_exitButton->setVisible(false);
        bottomMenu->addChild(m_exitButton);

        auto attachSprite = CCSprite::createWithSpriteFrameName("GJ_plusBtn_001.png");
        if (attachSprite)
            limitNodeSize(attachSprite, {26.f, 26.f}, 1.f, .1f);

        m_attachButton = CCMenuItemExt::createSpriteExtra(
            attachSprite ? static_cast<CCNode*>(attachSprite)
                         : static_cast<CCNode*>(CCLabelBMFont::create("+", "bigFont.fnt")),
            [this](CCMenuItemSpriteExtra*) { showAttachmentsPopup(); }
        );
        bottomMenu->addChild(m_attachButton);

        m_attachmentCountLabel = CCLabelBMFont::create("", "chatFont.fnt");
        m_attachmentCountLabel->setScale(.28f);
        m_attachmentCountLabel->setAnchorPoint({.5f, .5f});
        m_attachmentCountLabel->setVisible(false);
        bottom->addChild(m_attachmentCountLabel);

        m_input = TextInput::create(100.f, "Add a comment...", "chatFont.fnt");
        m_input->setID("opengeode-comment-input"_spr);
        m_input->setCommonFilter(CommonFilter::Any);
        m_input->setMaxCharCount(2000);
        m_input->setAnchorPoint({.5f, .5f});
        bottom->addChild(m_input);

        auto send = ButtonSprite::create(
            "Send", "goldFont.fnt", "GJ_button_01.png", 1.f
        );
        m_sendButton = CCMenuItemExt::createSpriteExtra(
            send, [this](CCMenuItemSpriteExtra*) { submitComment(); }
        );
        m_sendButton->setScale(kBarButtonScale);
        bottomMenu->addChild(m_sendButton);

        root->updateLayout();
        updateBottomLayout();
        load();
        return true;
    }

    void updateBottomLayout() {
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
            60.f,
            rowWidth - pad * 2.f - fixedWidth
                - gap * static_cast<float>(std::max(0, shown - 1))
        );
        m_input->setContentSize({inputWidth, kBottomHeight - 4.f});

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
        showLoading("Loading comments...");

        if (!m_state.loggedIn) {
            loadMod();
            return;
        }

        request("GET", "/v1/me", [this](web::WebResponse response) {
            if (response.ok()) {
                auto json = response.json().unwrapOr(matjson::makeObject());
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
                hideLoading();
                notifyStatus(errorText(response));
                return;
            }

            auto json = response.json().unwrapOr(matjson::makeObject());
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
                hideLoading();
                rebuild();
                return;
            }

            loadSelectedVersion();
        });
    }

    void loadSelectedVersion() {
        if (m_state.selectedVersion.empty()) return;

        showLoading("Loading submission...");

        request(
            "GET",
            fmt::format(
                "/v1/mods/{}/versions/{}/submission",
                m_modID,
                m_state.selectedVersion
            ),
            [this](web::WebResponse response) {
                if (!response.ok()) {
                    hideLoading();
                    m_state.comments.clear();
                    m_state.lock = "none";
                    m_state.lockedBy = 0;
                    rebuild();
                    notifyStatus("This version does not have a submission.");
                    return;
                }

                auto json = response.json().unwrapOr(matjson::makeObject());
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
                        hideLoading();
                        if (!commentsResponse.ok()) {
                            m_state.comments.clear();
                            rebuild();
                            notifyStatus(errorText(commentsResponse));
                            return;
                        }

                        parseComments(commentsResponse.json().unwrapOr(matjson::makeObject()));
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
                    item.filename = stringValue(attachment, "filename");
                    if (item.filename.empty() && !item.url.empty()) {
                        auto slash = item.url.find_last_of('/');
                        item.filename = slash == std::string::npos
                            ? item.url
                            : item.url.substr(slash + 1);
                    }
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

            if (comment.canEdit) {
                auto button = ButtonSprite::create(
                    "Edit", "goldFont.fnt", "GJ_button_01.png", .81f
                );
                button->setScale(.81f);
                auto editItem = CCMenuItemExt::createSpriteExtra(
                    button, [this, comment](CCMenuItemSpriteExtra*) { beginEdit(comment); }
                );
                editItem->setAnchorPoint({.5f, .5f});
                actions->addChild(editItem);
            }
            if (comment.canDelete) {
                auto button = ButtonSprite::create(
                    "Delete", "goldFont.fnt", "GJ_button_06.png", .81f
                );
                button->setScale(.81f);
                auto deleteItem = CCMenuItemExt::createSpriteExtra(
                    button, [this, comment](CCMenuItemSpriteExtra*) { deleteComment(comment.id); }
                );
                deleteItem->setAnchorPoint({.5f, .5f});
                actions->addChild(deleteItem);
            }
            actions->updateLayout();
            header->addChild(actions);
            header->updateLayout();

            auto body = MDTextArea::create(
                comment.body.empty() ? "..." : comment.body,
                {width - 18.f, 66.f},
                true
            );
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
                            [this, url = attachment.url](CCMenuItemSpriteExtra*) {
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
                            [this, populate, commentID, right](CCMenuItemSpriteExtra*) {
                                m_attachmentOffsets[commentID] += right ? 1 : -1;
                                (*populate)();
                            }
                        );
                        item->setScale(.6f);
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

            auto itemHeight = header->getContentHeight() + body->getContentHeight() * 1.05f;
            if (!comment.attachments.empty())
                itemHeight += kAttachmentAreaHeight + 4.f;

            auto item = CCNode::create();
            item->setContentSize({width, itemHeight + 6.f});
            item->setAnchorPoint({.5f, .5f});

            auto itemBG = NineSlice::create("square02b_001.png");
            itemBG->setColor(ccBLACK);
            itemBG->setOpacity(115);
            itemBG->setScale(.3f);
            itemBG->setContentSize(item->getContentSize() / itemBG->getScale());
            item->addChildAtPosition(itemBG, Anchor::Center);

            item->setLayout(ColumnLayout::create()
                ->setAxisReverse(true)
                ->setAxisAlignment(AxisAlignment::End)
                ->setCrossAxisAlignment(AxisAlignment::Center)
                ->setGap(2.f));

            item->addChild(header);
            item->addChild(body);
            if (!comment.attachments.empty())
                item->addChild(attachmentArea);

            item->updateLayout();
            m_commentsContainer->addChild(item);
            totalHeight += itemHeight + 6.f + 7.f;
        }

        if (!any) {
            auto empty = CCLabelBMFont::create("No comments yet.", "chatFont.fnt");
            empty->setScale(.35f);
            empty->setAnchorPoint({.5f, .5f});
            empty->setPosition(m_commentsContainer->getContentSize() / 2.f);
            m_commentsContainer->addChild(empty);
        }

        m_commentsContainer->setContentSize({
            width + 12.f,
            std::max(scroll->getContentHeight(), totalHeight)
        });
        m_commentsContainer->updateLayout();

        if (m_versionButton) {
            auto vSprite = typeinfo_cast<ButtonSprite*>(m_versionButton->getChildren()->objectAtIndex(0));
            if (vSprite) {
                vSprite->setString(
                    m_state.selectedVersion.empty() ? "None" : m_state.selectedVersion.c_str()
                );
            }
        }

        if (m_lockLabel) {
            std::string text = "Unlocked";
            if (m_state.lock == "locked") {
                text = "Locked";
                if (!m_state.lockedByName.empty())
                    text += " by " + m_state.lockedByName;
            }
            else if (m_state.lock == "internal") {
                text = "Internal";
                if (!m_state.lockedByName.empty())
                    text += " by " + m_state.lockedByName;
            }
            m_lockLabel->setString(text.c_str());
            m_lockLabel->limitLabelWidth(m_lockLabelMaxWidth, .42f, .1f);
        }

        if (m_lockControls) {
            m_lockControls->setVisible(m_state.loggedIn && m_state.currentDeveloperAdmin);
        }
        
        if (m_input) m_input->setVisible(canComment());
        if (m_sendButton) m_sendButton->setVisible(canComment());
        if (m_attachButton) m_attachButton->setVisible(canComment());

        updateBottomLayout();
        updateAttachmentCount();
    }

    void addAvatar(CCNode* container, CommentData const& comment) {
        if (!comment.pfp.empty()) {
            createContainedImage(container, {30.f, 30.f}, comment.pfp);
            return;
        }

        auto inner = CCSprite::createWithSpriteFrameName(
            "accountBtn_myProfile_001.png"
        );
        if (!inner) return;

        inner->setAnchorPoint({.5f, .5f});
        limitNodeSize(inner, {30.f, 30.f}, 1.f, .1f);
        container->addChildAtPosition(inner, Anchor::Center);
    }

    void showVersionPicker() {
        if (m_state.versions.empty()) return;
        if (auto popup = VersionSelectPopup::create(
            m_state.versions,
            [this](std::string version) {
                m_state.selectedVersion = std::move(version);
                loadSelectedVersion();
            }
        )) {
            popup->show();
        }
    }

    void showLockPicker() {
        if (auto popup = LockSettingsPopup::create([this](std::string value) {
            setLock(value);
        })) {
            popup->show();
        }
    }

    void setLock(std::string value) {
        if (!m_state.loggedIn || !m_state.currentDeveloperAdmin) return;
        if (m_state.selectedVersion.empty()) return;

        showLoading("Updating lock...");

        matjson::Value body = matjson::makeObject();
        body["lock"] = value;

        auto req = web::WebRequest();
        auto token = getAuthAccessToken();
        if (!token.empty())
            req.header("Authorization", "Bearer " + token);
        req.bodyJSON(body);

        m_requestTask.spawn(
            req.send(
                "PUT",
                fmt::format(
                    "{}/v1/mods/{}/versions/{}/submission",
                    trimSlash(getIndexUrl()),
                    m_modID,
                    m_state.selectedVersion
                )
            ),
            [this](web::WebResponse response) {
                if (!response.ok()) {
                    hideLoading();
                    notifyStatus(errorText(response));
                    return;
                }
                loadSelectedVersion();
            }
        );
    }

    void pickAttachments() {
        if (!canUploadAttachments()) {
            notifyStatus("Attachments require a verified developer, mod developer, or admin.");
            return;
        }

        auto picker = geode::utils::file::FilePicker::create();
        if (!picker) {
            notifyStatus("File picker is unavailable.");
            return;
        }

        picker->setFilter({
            {"Images", {"*.png", "*.jpg", "*.jpeg", "*.webp"}}
        });

        picker->show([this](std::filesystem::path const& path) {
            m_pendingFiles.push_back(path);
            updateAttachmentCount();
        });
    }

    void updateAttachmentCount() {
        if (!m_attachmentCountLabel) return;
        auto count = m_pendingFiles.size();
        if (m_editingCommentID != 0) {
            auto it = std::find_if(
                m_state.comments.begin(),
                m_state.comments.end(),
                [this](auto const& c) { return c.id == m_editingCommentID; }
            );
            if (it != m_state.comments.end())
                count += it->attachments.size() - m_removedAttachments.size();
        }

        m_attachmentCountLabel->setString(count > 0 ? fmt::format("({})", count).c_str() : "");
        m_attachmentCountLabel->setVisible(count > 0);
        updateBottomLayout();
    }

    void submitComment() {
        if (m_state.selectedVersion.empty()) {
            notifyStatus("Select a version first.");
            return;
        }
        if (!canComment()) {
            notifyStatus("You cannot comment on this submission.");
            return;
        }

        if (m_editingCommentID != 0) {
            updateExistingComment();
            return;
        }

        auto text = std::string(m_input->getString());
        if (text.empty() && m_pendingFiles.empty() && m_editingCommentID == 0) {
            notifyStatus("Write something or attach an image.");
            return;
        }

        if (!hasAuthTokens()) {
            notifyStatus("Log in to comment.");
            return;
        }

        m_sendButton->setEnabled(false);
        showLoading("Posting...");

        matjson::Value body = matjson::makeObject();
        body["comment"] = text;

        auto req = web::WebRequest();
        auto token = getAuthAccessToken();
        if (!token.empty())
            req.header("Authorization", "Bearer " + token);
        req.bodyJSON(body);

        m_requestTask.spawn(
            req.send(
                "POST",
                fmt::format(
                    "{}/v1/mods/{}/versions/{}/submission/comments",
                    trimSlash(getIndexUrl()),
                    m_modID,
                    m_state.selectedVersion
                )
            ),
            [this](web::WebResponse response) {
                if (!response.ok()) {
                    hideLoading();
                    m_sendButton->setEnabled(true);
                    notifyStatus(errorText(response));
                    return;
                }

                int commentID = 0;
                auto json = response.json().unwrapOr(matjson::makeObject());
                auto payload = json["payload"].isObject() ? json["payload"] : json;
                if (payload.isObject())
                    commentID = intValue(payload, "id");

                if (commentID == 0) {
                    hideLoading();
                    m_sendButton->setEnabled(true);
                    notifyStatus("Comment was created but no ID was returned.");
                    loadSelectedVersion();
                    return;
                }

                finishCommentAttachments(commentID, false);
            }
        );
    }

    void updateExistingComment() {
        m_sendButton->setEnabled(false);
        showLoading("Saving...");

        matjson::Value body = matjson::makeObject();
        body["comment"] = std::string(m_input->getString());

        auto req = web::WebRequest();
        auto token = getAuthAccessToken();
        if (!token.empty())
            req.header("Authorization", "Bearer " + token);
        req.bodyJSON(body);

        m_requestTask.spawn(
            req.send(
                "PUT",
                fmt::format(
                    "{}/v1/mods/{}/versions/{}/submission/comments/{}",
                    trimSlash(getIndexUrl()),
                    m_modID,
                    m_state.selectedVersion,
                    m_editingCommentID
                )
            ),
            [this](web::WebResponse response) {
                if (!response.ok()) {
                    hideLoading();
                    m_sendButton->setEnabled(true);
                    notifyStatus(errorText(response));
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
            m_input->setString("");
            m_sendButton->setEnabled(true);
            hideLoading();
            notifyStatus(editing ? "Comment updated." : "Comment posted.");
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
        if (m_pendingFiles.empty()) {
            finish();
            return;
        }

        if (!canUploadAttachments()) {
            m_pendingFiles.clear();
            notifyStatus("Comment saved, but you cannot upload attachments.");
            finish();
            return;
        }

        auto request = web::WebRequest();
        auto token = getAuthAccessToken();
        if (!token.empty())
            request.header("Authorization", "Bearer " + token);

        std::string boundary = "----GeodeBoundary" + std::to_string(std::rand());
        request.header("Content-Type", "multipart/form-data; boundary=" + boundary);

        std::vector<uint8_t> bodyData;
        auto append = [&](std::string const& str) {
            bodyData.insert(bodyData.end(), str.begin(), str.end());
        };

        for (size_t i = 0; i < m_pendingFiles.size(); ++i) {
            auto const& path = m_pendingFiles[i];
            auto result = geode::utils::file::readBinary(path);
            if (!result) {
                hideLoading();
                m_sendButton->setEnabled(true);
                notifyStatus("Could not read an attachment.");
                return;
            }
            
            append("--" + boundary + "\r\n");
            append("Content-Disposition: form-data; name=\"files[" + std::to_string(i) + "]\"; filename=\"" + geode::utils::string::pathToString(path.filename()) + "\"\r\n");
            append("Content-Type: application/octet-stream\r\n\r\n");
            
            auto const& data = result.unwrap();
            bodyData.insert(bodyData.end(), data.begin(), data.end());
            append("\r\n");
        }
        append("--" + boundary + "--\r\n");
        request.body(bodyData);

        showLoading("Uploading attachments...");

        m_requestTask.spawn(
            request.send(
                "POST",
                fmt::format(
                    "{}/v1/mods/{}/versions/{}/submission/comments/{}/attachments",
                    trimSlash(getIndexUrl()),
                    m_modID,
                    m_state.selectedVersion,
                    commentID
                )
            ),
            [this, finish = std::move(finish)](web::WebResponse response) mutable {
                if (!response.ok()) {
                    hideLoading();
                    m_sendButton->setEnabled(true);
                    notifyStatus(fmt::format("Comment saved, attachment upload failed: {}", errorText(response)));
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
        auto token = getAuthAccessToken();
        if (!token.empty())
            request.header("Authorization", "Bearer " + token);

        showLoading("Removing attachments...");

        m_requestTask.spawn(
            request.send(
                "DELETE",
                fmt::format(
                    "{}/v1/mods/{}/versions/{}/submission/comments/{}/attachments/{}",
                    trimSlash(getIndexUrl()),
                    m_modID,
                    m_state.selectedVersion,
                    commentID,
                    attachmentID
                )
            ),
            [this, commentID, finish = std::move(finish)](web::WebResponse response) mutable {
                if (!response.ok()) {
                    hideLoading();
                    m_sendButton->setEnabled(true);
                    notifyStatus(errorText(response));
                    return;
                }
                removeNextAttachment(commentID, std::move(finish));
            }
        );
    }

    void beginEdit(CommentData const& comment) {
        m_editingCommentID = comment.id;
        m_input->setString(comment.body);
        m_exitButton->setVisible(true);
        m_pendingFiles.clear();
        m_removedAttachments.clear();
        updateBottomLayout();
        updateAttachmentCount();
        notifyStatus("Editing comment. Press Send to save.");
    }

    void exitEdit() {
        m_editingCommentID = 0;
        m_input->setString("");
        m_exitButton->setVisible(false);
        m_pendingFiles.clear();
        m_removedAttachments.clear();
        updateBottomLayout();
        updateAttachmentCount();
    }

    void showAttachmentsPopup() {
        std::vector<CommentAttachment> existing;
        if (m_editingCommentID != 0) {
            auto it = std::find_if(
                m_state.comments.begin(),
                m_state.comments.end(),
                [this](auto const& c) { return c.id == m_editingCommentID; }
            );
            if (it != m_state.comments.end())
                existing = it->attachments;
        }

        if (auto popup = AttachmentPopup::create(
            std::move(existing),
            m_pendingFiles,
            m_removedAttachments,
            [this](int id) { toggleAttachmentRemoval(id); },
            [this](std::filesystem::path const& path) { removePendingFile(path); },
            [this]() { pickAttachments(); }
        )) {
            popup->show();
        }
    }

    void toggleAttachmentRemoval(int attachmentID) {
        auto it = std::find(m_removedAttachments.begin(), m_removedAttachments.end(), attachmentID);
        if (it == m_removedAttachments.end()) {
            m_removedAttachments.push_back(attachmentID);
            notifyStatus("Attachment marked for removal.");
        }
        else {
            m_removedAttachments.erase(it);
            notifyStatus("Attachment restored.");
        }
        updateAttachmentCount();
    }

    void removePendingFile(std::filesystem::path const& path) {
        auto it = std::find(m_pendingFiles.begin(), m_pendingFiles.end(), path);
        if (it != m_pendingFiles.end()) {
            m_pendingFiles.erase(it);
            notifyStatus("Attachment removed.");
            updateAttachmentCount();
        }
    }

    void deleteComment(int id) {
        createQuickPopup(
            "Delete Comment",
            "Are you sure you want to delete this comment?",
            "Cancel",
            "Delete",
            [this, id](auto, bool confirmed) {
                if (!confirmed) return;

                auto request = web::WebRequest();
                auto token = getAuthAccessToken();
                if (!token.empty())
                    request.header("Authorization", "Bearer " + token);

                showLoading("Deleting comment...");

                m_requestTask.spawn(
                    request.send(
                        "DELETE",
                        fmt::format(
                            "{}/v1/mods/{}/versions/{}/submission/comments/{}",
                            trimSlash(getIndexUrl()),
                            m_modID,
                            m_state.selectedVersion,
                            id
                        )
                    ),
                    [this](web::WebResponse response) {
                        hideLoading();
                        if (!response.ok()) {
                            notifyStatus(errorText(response));
                            return;
                        }
                        loadSelectedVersion();
                    }
                );
            }
        );
    }

    void showAttachmentImage(std::string const& url) {
        if (auto popup = AttachmentImagePopup::create(url))
            popup->show();
    }

public:
    static CommentsLayer* create(std::string modID, CCNode* textArea) {
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

void attachCommentsToTextArea(std::string const& modID, geode::MDTextArea* textArea) {
    if (!textArea) return;

    auto layer = CommentsLayer::create(modID, textArea);
    layer->setID("opengeode-comments-layer"_spr);
    layer->setVisible(false);

    auto bg = textArea->getChildByType<CCScale9Sprite>(0);
    if (bg) {
        layer->setPosition(bg->getPosition());
        layer->setZOrder(bg->getZOrder() + 1);
        textArea->addChild(layer);
    }

    auto scroll = textArea->getScrollLayer();
    if (!scroll) return;
    auto parent = scroll->getParent();
    if (!parent) return;

    auto container = CCNode::create();
    container->setContentSize({parent->getContentWidth(), 35.f});
    container->setAnchorPoint({.5f, 0.f});
    container->setLayout(RowLayout::create()
        ->setAxisAlignment(AxisAlignment::Center)
        ->setCrossAxisAlignment(AxisAlignment::Start)
        ->setGap(2.f));

    auto descriptionTab = OpenGeodeTabSprite::create("GJ_infoIcon_001.png", "Info", 140.f);
    descriptionTab->select(true);

    auto commentsTab = OpenGeodeTabSprite::create("GJ_chatBtn_001.png", "Comments", 140.f);

    auto descriptionItem = CCMenuItemExt::createSpriteExtra(
        descriptionTab,
        [textArea, layer, descriptionTab, commentsTab](CCMenuItemSpriteExtra*) {
            descriptionTab->select(true);
            commentsTab->select(false);
            layer->setVisible(false);
            if (auto s = textArea->getScrollLayer()) s->setVisible(true);
        }
    );

    auto commentsItem = CCMenuItemExt::createSpriteExtra(
        commentsTab,
        [textArea, layer, descriptionTab, commentsTab](CCMenuItemSpriteExtra*) {
            descriptionTab->select(false);
            commentsTab->select(true);
            layer->setVisible(true);
            if (auto s = textArea->getScrollLayer()) s->setVisible(false);
        }
    );

    auto menu = CCMenu::create();
    menu->setContentSize(container->getContentSize());
    menu->setLayout(RowLayout::create()->setGap(0.f));
    menu->addChild(descriptionItem);
    menu->addChild(commentsItem);
    menu->updateLayout();
    container->addChild(menu);
    container->updateLayout();

    parent->addChildAtPosition(container, Anchor::Top, ccp(0, 16));
}

} // namespace opengeode
