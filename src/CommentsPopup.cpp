#include "CommentsPopup.hpp"
#include "Settings.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/MDTextArea.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/Notification.hpp>
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

// ---------------------------------------------------------------------------
// Comments window layout constants (top / middle / bottom sections).
// ---------------------------------------------------------------------------
constexpr float kTopHeight = 29.f;
constexpr float kTopInset = 3.f;
constexpr float kBottomHeight = 38.f;
constexpr float kBottomInset = 3.f;
constexpr float kSectionGap = 3.f;
constexpr float kHorizontalMargin = 20.f; // top/bottom bars inset from each edge

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

// The single image path used for EVERY image (avatars, comment thumbnails,
// attachment previews, the full-size viewer). The sprite is measured after it
// loads and scaled down so it always stays inside `size`.
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

        // Use the real size of the loaded image (the inner sprite if there is
        // one) so huge images can't spill out of their box.
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

// A square box with a dark background that shows where an attachment lives,
// with the image contained inside it.
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

// CCMenuItemSprite eases its OWN scale down/up on press/release; a fast tap
// can catch that ease mid-flight and leave the button bigger than it started.
// Call this at the top of a button's callback to force it back to its real
// size every time, regardless of where the ease landed.
void pinScale(CCMenuItemSpriteExtra* item, float scale) {
    if (!item) return;
    item->stopAllActions();
    item->setScale(scale);
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
        if (!Popup::init(350.f, 260.f)) return false;
        m_url = std::move(url);
        setTitle("Attachment");

        auto holder = CCNode::create();
        holder->setContentSize({320.f, 210.f});
        holder->setAnchorPoint({.5f, .5f});
        holder->setLayout(AnchorLayout::create());
        m_mainLayer->addChildAtPosition(holder, Anchor::Center);
        createContainedImage(holder, {320.f, 210.f}, m_url);
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

        // Row geometry (kept small so more rows fit on screen).
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
            // Everything in the row is pinned to the left / right edge with an
            // AnchorLayout, so nothing depends on a nested RowLayout.
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

            // [Image] - left, in its own background box.
            auto box = createAttachmentBox(imageSize, url, localPath);
            row->addChildAtPosition(
                box, Anchor::Left, ccp(pad + imageSize / 2.f, 0.f)
            );

            // File name (top) + upload status (bottom), left-aligned.
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

            // [Remove/Restore] - right, vertically centred. Auto-sized like
            // Edit/Delete: the button hugs its text (no empty space above or
            // below) and is only scaled down if it would be too wide.
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
                    [this, path = localPath ? *localPath : std::filesystem::path()](auto) {
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
                    [this, attachmentID](auto) {
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

    bool init(
        std::vector<CommentAttachment> attachments,
        std::vector<std::filesystem::path> pending,
        std::vector<int> removed,
        std::function<void(int)> onToggleDelete,
        std::function<void(std::filesystem::path const&)> onRemovePending,
        std::function<void()> onAdd
    ) {
        if (!Popup::init(300.f, 200.f)) return false;

        m_attachments = std::move(attachments);
        m_pending = std::move(pending);
        m_removed = std::move(removed);
        m_onToggleDelete = std::move(onToggleDelete);
        m_onRemovePending = std::move(onRemovePending);
        m_onAdd = std::move(onAdd);
        setTitle("Attachments");

        auto size = m_mainLayer->getContentSize();
        constexpr float scrollWidth = 270.f;
        constexpr float scrollHeight = 128.f;
        constexpr float scrollBottom = 36.f;

        // Background behind the (now longer) list.
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
        // ScrollLayer positions from its bottom-left corner.
        m_scroll->setPosition({(size.width - scrollWidth) / 2.f, scrollBottom});
        m_content = m_scroll->m_contentLayer;
        m_content->setAnchorPoint({0.f, 0.f});
        // Rows are listed top-to-bottom.
        m_content->setLayout(ColumnLayout::create()
            ->setAxisReverse(true)
            ->setAxisAlignment(AxisAlignment::Start)
            ->setCrossAxisAlignment(AxisAlignment::Center)
            ->setAutoScale(false)
            ->setGap(4.f)
            ->setPadding(Padding::uniform(5.f)));
        m_mainLayer->addChild(m_scroll);

        // "+ Add Image": bigger, at the bottom of the popup.
        auto add = ButtonSprite::create(
            "+ Add Image", "goldFont.fnt", "GJ_button_01.png", 1.f
        );
        auto addItem = CCMenuItemExt::createSpriteExtra(add, [](auto) {});
        CCMenuItemExt::assignCallback<CCMenuItemSpriteExtra>(
            addItem, [this, addItem](auto) {
                pinScale(addItem, .85f);
                if (m_onAdd) {
                    m_onAdd();
                    removeFromParent();
                }
            }
        );
        addItem->setScale(.85f);
        m_buttonMenu->addChildAtPosition(
            addItem, Anchor::Bottom, ccp(0.f, scrollBottom / 2.f)
        );

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

class CommentsLayer : public CCLayer {
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

    bool init(std::string modID, CCNode* textArea) {
        if (!CCLayer::init()) return false;

        m_modID = std::move(modID);
        m_textArea = textArea;
        setContentSize(textArea->getContentSize());
        // The chat layer represents the whole textarea-sized window, so its
        // positioning reference must be its center rather than the default
        // bottom-left anchor.
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

        // ------------------------------------------------------------------
        // TOP (anchored to the top):
        //   left  = unlock status (chatFont)
        //   right = Lock / Internal / Unlock (gold Pusab, auto-sized)
        // ------------------------------------------------------------------
        auto top = CCNode::create();
        top->setContentSize({width - kHorizontalMargin, kTopHeight});
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
        // Right edge of the menu = right edge of the window.
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
            // Text at scale 1 so the button hugs the text (no empty space
            // above/below), then the whole button is scaled.
            auto sprite = ButtonSprite::create(
                text, "goldFont.fnt", texture, 1.f
            );
            // CCMenuItemSprite's built-in press animation eases the item's
            // OWN scale down then back "up" to whatever scale it reads at
            // release time; a fast tap can catch it mid-ease and leave the
            // button permanently bigger. Pin the scale back explicitly every
            // time the button is used.
            auto item = CCMenuItemExt::createSpriteExtra(sprite, [](auto) {});
            CCMenuItemExt::assignCallback<CCMenuItemSpriteExtra>(
                item, [this, item, value](auto) {
                    pinScale(item, kLockButtonScale);
                    setLock(value);
                }
            );
            item->setScale(kLockButtonScale);
            lockControls->addChild(item);
        };

        makeLockButton("Lock", "locked", "GJ_button_06.png");
        makeLockButton("Internal", "internal", "GJ_button_02.png");
        makeLockButton("Unlock", "none", "GJ_button_01.png");

        float lockWidth = 4.f * 2.f; // two gaps
        for (auto child : lockControls->getChildrenExt())
            lockWidth += child->getScaledContentSize().width;
        lockControls->setContentSize({lockWidth, kTopHeight});
        lockControls->updateLayout();

        top->addChildAtPosition(lockControls, Anchor::Right, ccp(-8.f, 0.f));
        top->addChildAtPosition(m_lockLabel, Anchor::Left, ccp(8.f, 0.f));

        // Keep the status text from running into the buttons.
        m_lockLabelMaxWidth = std::max(60.f, width - kHorizontalMargin - 16.f - lockWidth - 5.f);
        m_lockLabel->limitLabelWidth(m_lockLabelMaxWidth, .42f, .1f);

        // ------------------------------------------------------------------
        // MIDDLE (anchored to the middle): comments scroll layer only.
        // It is centred in the space between the top and bottom sections.
        // ------------------------------------------------------------------
        auto middleHeight = std::max(
            1.f,
            height
                - (kTopHeight + kTopInset)
                - (kBottomHeight + kBottomInset)
                - kSectionGap * 2.f
        );
        // Centre of the free space, relative to the window's centre.
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

        // ------------------------------------------------------------------
        // BOTTOM (anchored to the bottom):
        //   [v1.0.0] [Exit Edit] [+] [count] [Add a comment...] [Send]
        // Positioned by updateBottomLayout(): the input stretches to fill
        // every pixel the buttons leave over.
        // ------------------------------------------------------------------
        auto bottom = CCNode::create();
        bottom->setContentSize({width - kHorizontalMargin, kBottomHeight});
        bottom->setAnchorPoint({.5f, 0.f});
        root->addChild(bottom);
        m_bottom = bottom;
        bottom->setLayoutOptions(
            AnchorLayoutOptions::create()
                ->setAnchor(Anchor::Bottom)
                ->setOffset(ccp(0.f, kBottomInset))
        );

        // Background lives INSIDE the row and is centred on it, so it is
        // exactly as tall as the row.
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

        // Version button: a regular green button whose label IS the version
        // ("v1.0.0"), in the Pusab font.
        auto versionSprite = ButtonSprite::create(
            "v-", "bigFont.fnt", "GJ_button_01.png", 1.f
        );
        m_versionButton = CCMenuItemExt::createSpriteExtra(versionSprite, [](auto) {});
        CCMenuItemExt::assignCallback<CCMenuItemSpriteExtra>(
            m_versionButton, [this](auto) {
                pinScale(m_versionButton, kBarButtonScale);
                showVersionPicker();
            }
        );
        m_versionButton->setScale(kBarButtonScale);
        bottomMenu->addChild(m_versionButton);

        // Exit Edit: only visible while a comment is being edited.
        auto exitSprite = ButtonSprite::create(
            "Exit Edit", "goldFont.fnt", "GJ_button_06.png", 1.f
        );
        m_exitButton = CCMenuItemExt::createSpriteExtra(exitSprite, [](auto) {});
        CCMenuItemExt::assignCallback<CCMenuItemSpriteExtra>(
            m_exitButton, [this](auto) {
                pinScale(m_exitButton, kBarButtonScale);
                exitEdit();
            }
        );
        m_exitButton->setScale(kBarButtonScale);
        m_exitButton->setVisible(false);
        bottomMenu->addChild(m_exitButton);

        // Add attachment (+)
        auto attachSprite = CCSprite::createWithSpriteFrameName("GJ_plusBtn_001.png");
        if (attachSprite)
            limitNodeSize(attachSprite, {26.f, 26.f}, 1.f, .1f);

        m_attachButton = CCMenuItemExt::createSpriteExtra(
            attachSprite ? static_cast<CCNode*>(attachSprite)
                         : static_cast<CCNode*>(CCLabelBMFont::create("+", "bigFont.fnt")),
            [this](auto) { showAttachmentsPopup(); }
        );
        bottomMenu->addChild(m_attachButton);

        // How many attachments
        m_attachmentCountLabel = CCLabelBMFont::create("", "chatFont.fnt");
        m_attachmentCountLabel->setScale(.28f);
        m_attachmentCountLabel->setAnchorPoint({.5f, .5f});
        m_attachmentCountLabel->setVisible(false);
        bottom->addChild(m_attachmentCountLabel);

        // Add a comment...
        m_input = TextInput::create(100.f, "Add a comment...", "chatFont.fnt");
        m_input->setID("opengeode-comment-input"_spr);
        m_input->setCommonFilter(CommonFilter::Any);
        m_input->setMaxCharCount(2000);
        m_input->setAnchorPoint({.5f, .5f});
        bottom->addChild(m_input);

        // Send
        auto send = ButtonSprite::create(
            "Send", "goldFont.fnt", "GJ_button_01.png", 1.f
        );
        m_sendButton = CCMenuItemExt::createSpriteExtra(send, [](auto) {});
        CCMenuItemExt::assignCallback<CCMenuItemSpriteExtra>(
            m_sendButton, [this](auto) {
                pinScale(m_sendButton, kBarButtonScale);
                submitComment();
            }
        );
        m_sendButton->setScale(kBarButtonScale);
        bottomMenu->addChild(m_sendButton);

        root->updateLayout();
        updateBottomLayout();
        load();
        return true;
    }

    // Lays the bottom row out by hand, left to right:
    //   [version] [Exit Edit] [+] [count] [input .......] [Send]
    // Hidden items take no room, and the input stretches to fill whatever
    // width the buttons leave over (nothing can go out of bounds).
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
        m_input->setContentSize({inputWidth, 34.f});

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
        beginLoading("Loading comments...");

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
                endLoading();
                notify(errorText(response), NotificationIcon::Error);
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
                endLoading();
                return;
            }

            loadSelectedVersion();
        });
    }

    void loadSelectedVersion() {
        if (m_state.selectedVersion.empty()) return;

        beginLoading("Loading submission...");

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
                    endLoading();
                    notify("This version does not have a submission.", NotificationIcon::Info);
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
                            endLoading();
                            notify(errorText(commentsResponse), NotificationIcon::Error);
                            return;
                        }

                        parseComments(commentsResponse.json().unwrapOr(matjson::Value()));
                        rebuild();
                        endLoading();
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

    // Status messages now surface as Geode notifications instead of a label
    // pinned to the popup.
    static void notify(
        std::string const& text,
        NotificationIcon icon = NotificationIcon::Info
    ) {
        if (text.empty()) return;
        Notification::create(text, icon)->show();
    }

    // For a request that actually waits on the network: show a spinner that
    // stays up for the whole operation, one notification per operation (not
    // one per retry/tick). beginLoading() replaces whatever loading
    // notification is currently showing; endLoading() dismisses it.
    Ref<Notification> m_loading;

    void beginLoading(std::string const& text) {
        endLoading();
        m_loading = Notification::create(text, NotificationIcon::Loading, 0.f);
        m_loading->show();
    }

    void endLoading() {
        if (m_loading) m_loading->hide();
        m_loading = nullptr;
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

        // Axis reversed: ColumnLayout stacks bottom-to-top by default, so the
        // comment list would otherwise hug the bottom of the scroll layer.
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
                    button, [this, comment](auto) { beginEdit(comment); }
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
                    button, [this, comment](auto) { deleteComment(comment.id); }
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

            // The attachment strip is a CCMenu whose DIRECT children are the
            // thumbnails and arrows (a CCMenu only sees its direct children,
            // which is why nested items weren't clickable before).
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

                // Swaps only the thumbnails; the rest of the chat is untouched.
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
                        // In-game arrow texture (points left; flipped for right).
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

            auto cardHeight = 36.f + 4.f + 66.f +
                (comment.attachments.empty() ? 0.f : kAttachmentAreaHeight + 4.f);

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

            // username / comment / attachments, top-to-bottom (axis reversed).
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
            // Empty string = zero width when there's nothing to show.
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

        if (!m_state.loggedIn)
            notify("Log in to comment.", NotificationIcon::Warning);
        else if (m_state.lock == "locked" && !m_state.currentDeveloperAdmin)
            notify("This submission is locked.", NotificationIcon::Info);
        else if (m_state.lock == "internal" && !m_state.currentDeveloperAdmin)
            notify("This submission is locked to the index team.", NotificationIcon::Info);
        else if (!any)
            notify("No comments yet.", NotificationIcon::Info);
        else
            ;
    }

    void addAvatar(CCNode* avatar, CommentData const& comment) {
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
                loadSelectedVersion();
            }
        );
        if(popup){popup->m_noElasticity=true;popup->show();}
    }

    void showAttachmentsPopup() {
        std::vector<CommentAttachment> attachments;
        for(auto const& c:m_state.comments)if(c.id==m_editingCommentID){attachments=c.attachments;break;}
        auto popup = AttachmentPopup::create(
            std::move(attachments),
            m_pendingFiles,
            m_removedAttachments,
            [this](int id) { toggleAttachmentRemoval(id); },
            [this](std::filesystem::path const& path) { removePendingFile(path); },
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

        beginLoading("Updating lock...");

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
                endLoading();
                if (!response.ok()) {
                    notify(errorText(response), NotificationIcon::Error);
                    return;
                }
                loadSelectedVersion();
            }
        );
    }

    void pickAttachments() {
        if (!canUploadAttachments()) {
            notify("Attachments require a verified developer, mod developer, or admin.", NotificationIcon::Warning);
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
                rebuild();
            }
        );
    }

    void submitComment() {
        if (m_state.selectedVersion.empty()) {
            notify("Select a version first.", NotificationIcon::Warning);
            return;
        }

        if (!canComment()) {
            notify("You cannot comment on this submission.", NotificationIcon::Error);
            return;
        }

        auto text = std::string(m_input->getString().c_str());
        if (text.empty() && m_pendingFiles.empty() && m_editingCommentID == 0) {
            notify("Write something or attach an image.", NotificationIcon::Warning);
            return;
        }

        if (!hasAuthTokens()) {
            notify("Log in to comment.", NotificationIcon::Warning);
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
        beginLoading("Posting...");

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
                    endLoading();
                    m_sendButton->setEnabled(true);
                    notify(errorText(response), NotificationIcon::Error);
                    return;
                }

                auto json = response.json().unwrapOr(matjson::Value());
                auto payload = json["payload"].isObject()
                    ? json["payload"]
                    : json;
                auto commentID = intValue(payload, "id");

                if (commentID == 0) {
                    endLoading();
                    m_sendButton->setEnabled(true);
                    notify("Comment was created but no ID was returned.", NotificationIcon::Info);
                    loadSelectedVersion();
                    return;
                }

                // Posting is done; the next stage (attachments, if any)
                // shows its own loading notification.
                endLoading();
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
        beginLoading("Saving...");

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
                    endLoading();
                    m_sendButton->setEnabled(true);
                    notify(errorText(response), NotificationIcon::Error);
                    return;
                }

                endLoading();
                finishCommentAttachments(m_editingCommentID, true);
            }
        );
    }

    void finishCommentAttachments(int commentID, bool editing) {
        auto finish = [this, editing]() {
            endLoading();
            m_pendingFiles.clear();
            m_removedAttachments.clear();
            m_editingCommentID = 0;
            m_input->setString("");
            m_sendButton->setEnabled(true);
            notify(editing ? "Comment updated." : "Comment posted.", NotificationIcon::Success);
            loadSelectedVersion();
        };

        if (!m_removedAttachments.empty()) {
            // One notification for the whole removal pass, not one per
            // attachment: removeNextAttachment() recurses without touching it.
            beginLoading("Removing attachments...");
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
            endLoading();
            m_pendingFiles.clear();
            notify("Comment saved, but you cannot upload attachments.", NotificationIcon::Error);
            finish();
            return;
        }

        web::MultipartForm form;
        for (auto const& path : m_pendingFiles) {
            auto result = form.file("image", path);
            if (!result) {
                endLoading();
                m_sendButton->setEnabled(true);
                notify("Could not read an attachment.", NotificationIcon::Error);
                return;
            }
        }

        auto request = web::WebRequest();
        request.header(
            "Authorization",
            "Bearer " + getAuthAccessToken()
        );
        request.bodyMultipart(form);

        // Replaces the "Removing attachments..." spinner if there was one.
        beginLoading("Uploading attachments...");

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
                    endLoading();
                    m_sendButton->setEnabled(true);
                    notify(fmt::format(
                            "Comment saved, attachment upload failed: {}",
                            errorText(response)
                        ), NotificationIcon::Error);
                    return;
                }
                finish();
            }
        );
    }

    void removeNextAttachment(int commentID, std::function<void()> finish) {
        if (m_removedAttachments.empty()) {
            // uploadAttachments() shows its own loading notification; finish()
            // dismisses the "Removing attachments..." one via endLoading().
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
                    endLoading();
                    m_sendButton->setEnabled(true);
                    notify(errorText(response), NotificationIcon::Error);
                    return;
                }
                removeNextAttachment(commentID, std::move(finish));
            }
        );
    }

    void beginEdit(CommentData const& comment) {
        if (!comment.canEdit) return;

        m_editingCommentID = comment.id;
        m_removedAttachments.clear();
        m_pendingFiles.clear();
        m_input->setString(comment.body.c_str());
        notify("Editing comment. Press Send to save.", NotificationIcon::Info);
        rebuild();
        m_input->focus();
    }

    // Leaves edit mode without saving anything.
    void exitEdit() {
        m_editingCommentID = 0;
        m_removedAttachments.clear();
        m_pendingFiles.clear();
        m_input->setString("");
        rebuild();
    }

    void toggleAttachmentRemoval(int attachmentID) {
        if (m_editingCommentID == 0) return;

        auto it = std::find(
            m_removedAttachments.begin(),
            m_removedAttachments.end(),
            attachmentID
        );

        if (it == m_removedAttachments.end()) {
            m_removedAttachments.push_back(attachmentID);
            notify("Attachment marked for removal.", NotificationIcon::Info);
        }
        else {
            m_removedAttachments.erase(it);
            notify("Attachment restored.", NotificationIcon::Success);
        }
        rebuild();
    }

    void removePendingFile(std::filesystem::path const& path) {
        auto it = std::find(m_pendingFiles.begin(), m_pendingFiles.end(), path);
        if (it == m_pendingFiles.end()) return;

        m_pendingFiles.erase(it);
        notify("Attachment removed.", NotificationIcon::Success);
        rebuild();
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

                beginLoading("Deleting comment...");

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
                        endLoading();
                        if (!response.ok()) {
                            notify(errorText(response), NotificationIcon::Error);
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

            auto layer = CommentsLayer::create(modID, textarea);
            if (layer) {
                // textarea's position uses its own (usually bottom-left)
                // anchor. The comments layer uses a centered anchor, so add
                // it to the same parent at the parent's center instead of
                // reusing textarea's bottom-left position.
                layer->setAnchorPoint({.5f, .5f});
                layer->setScale(textarea->getScale());
                layer->setRotation(textarea->getRotation());
                parent->addChildAtPosition(layer, Anchor::Center);
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
