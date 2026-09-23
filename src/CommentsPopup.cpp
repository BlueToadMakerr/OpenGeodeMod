#include "CommentsPopup.hpp"
#include "Settings.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/MDTextArea.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include <Geode/ui/LazySprite.hpp>
#include <Geode/utils/ColorProvider.hpp>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

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
    bool init(std::vector<std::string> versions, std::function<void(std::string)> cb) {
        if (!Popup::init(250.f, 240.f)) return false;
        m_versions = std::move(versions);
        m_onSelect = std::move(cb);
        setTitle("Select Version");
        auto scroll = ScrollLayer::create({224.f, 184.f});
        scroll->setPosition({13.f, 17.f});
        auto content=scroll->m_contentLayer; float y=8.f;
        for (auto const& version:m_versions) {
            auto spr = ButtonSprite::create(
                version.c_str(), "bigFont.fnt", "GJ_button_01.png", .40f
            );
            spr->setScale(.40f);
            auto item = CCMenuItemExt::createSpriteExtra(
                spr,
                [this, version](auto) {
                    if (m_onSelect) m_onSelect(version);
                    removeFromParent();
                }
            );
            item->setPosition({scroll->getContentWidth() / 2.f, y + 13.f});
            content->addChild(item);
            y += 28.f;
        }
        content->setContentSize({scroll->getContentWidth(),std::max(scroll->getContentHeight(),y+4.f)});
        addChild(scroll); return true;
    }
public:
    static VersionSelectPopup* create(std::vector<std::string> versions,std::function<void(std::string)> cb){
        auto ret=new VersionSelectPopup(); if(ret&&ret->init(std::move(versions),std::move(cb))){ret->autorelease();return ret;} delete ret; return nullptr;
    }
};

class AttachmentPopup : public Popup {
    std::function<void(int)> m_onDelete;
    std::function<void()> m_onAdd;

    static void addPreview(CCNode* row, std::string const& url, std::filesystem::path const* localPath) {
        CCNode* previewNode = nullptr;

        if (localPath && std::filesystem::exists(*localPath)) {
            auto sprite = CCSprite::create(localPath->string().c_str());
            if (sprite) {
                limitNodeSize(sprite, {52.f, 52.f}, 1.f, .1f);
                previewNode = sprite;
            }
        }
        else if (!url.empty()) {
            auto preview = LazySprite::create({52.f, 52.f}, false);
            if (preview) {
                preview->loadFromUrl(url);
                preview->setLoadCallback([preview](Result<> result) {
                    if (!result) {
                        preview->setVisible(false);
                        return;
                    }
                    limitNodeSize(preview, {52.f, 52.f}, 1.f, .1f);
                });
                previewNode = preview;
            }
        }

        if (previewNode)
            row->addChildAtPosition(previewNode, Anchor::Left, ccp(34.f, 0.f), false);
    }

    bool init(
        std::vector<CommentAttachment> attachments,
        std::vector<std::filesystem::path> pending,
        std::function<void(int)> cb,
        std::function<void()> onAdd
    ) {
        if (!Popup::init(370.f, 310.f)) return false;

        m_onDelete = std::move(cb);
        m_onAdd = std::move(onAdd);
        setTitle("Attachments");

        auto root = CCNode::create();
        root->setContentSize({340.f, 250.f});
        root->setLayout(
            ColumnLayout::create()
                ->setAxisAlignment(AxisAlignment::Between)
                ->setCrossAxisAlignment(AxisAlignment::Center)
                ->setPadding(Padding::uniform(4.f))
        );
        m_mainLayer->addChildAtPosition(root, Anchor::Center);

        auto scroll = ScrollLayer::create({332.f, 205.f});
        auto content = scroll->m_contentLayer;
        content->setLayout(
            ColumnLayout::create()
                ->setAxisAlignment(AxisAlignment::Start)
                ->setCrossAxisAlignment(AxisAlignment::Center)
                ->setGap(6.f)
                ->setPadding(Padding::uniform(4.f))
        );

        auto addRow = [this, content](
            std::string name,
            std::string status,
            std::string url,
            std::filesystem::path const* localPath,
            int attachmentID
        ) {
            auto row = CCNode::create();
            row->setContentSize({320.f, 62.f});
            row->setLayout(
                RowLayout::create()
                    ->setAxisAlignment(AxisAlignment::Between)
                    ->setCrossAxisAlignment(AxisAlignment::Center)
                    ->setPadding(Padding::horizontal(8.f))
            );

            auto bg = NineSlice::create("square02b_001.png");
            bg->setColor(ccBLACK);
            bg->setOpacity(70);
            bg->setScale(.3f);
            bg->setContentSize(row->getContentSize() / bg->getScale());
            row->addChildAtPosition(bg, Anchor::Center, 0, false);

            auto previewHolder = CCNode::create();
            previewHolder->setContentSize({58.f, 54.f});
            previewHolder->setLayout(
                RowLayout::create()
                    ->setAxisAlignment(AxisAlignment::Center)
                    ->setCrossAxisAlignment(AxisAlignment::Center)
            );
            row->addChild(previewHolder);
            addPreview(previewHolder, url, localPath);

            auto info = CCNode::create();
            info->setContentSize({145.f, 54.f});
            info->setLayout(
                ColumnLayout::create()
                    ->setAxisAlignment(AxisAlignment::Center)
                    ->setCrossAxisAlignment(AxisAlignment::Start)
                    ->setGap(2.f)
            );
            auto nameLabel = CCLabelBMFont::create(name.c_str(), "bigFont.fnt");
            nameLabel->setScale(.30f);
            auto statusLabel = CCLabelBMFont::create(status.c_str(), "chatFont.fnt");
            statusLabel->setScale(.25f);
            info->addChild(nameLabel);
            if (!status.empty())
                info->addChild(statusLabel);
            info->updateLayout();
            row->addChild(info);

            if (attachmentID != 0) {
                auto actions = CCMenu::create();
                actions->setContentSize({100.f, 54.f});
                actions->setLayout(
                    RowLayout::create()
                        ->setAxisAlignment(AxisAlignment::End)
                        ->setCrossAxisAlignment(AxisAlignment::Center)
                        ->setGap(5.f)
                );

                auto view = ButtonSprite::create(
                    "View", "goldFont.fnt", "GJ_button_01.png", .32f
                );
                view->setScale(.32f);
                auto viewItem = CCMenuItemExt::createSpriteExtra(
                    view, [url](auto) {
                        if (!url.empty())
                            web::openLinkInBrowser(url);
                    }
                );

                auto del = ButtonSprite::create(
                    "Delete", "goldFont.fnt", "GJ_button_06.png", .30f
                );
                del->setScale(.30f);
                auto delItem = CCMenuItemExt::createSpriteExtra(
                    del, [this, attachmentID](auto) {
                        if (m_onDelete) m_onDelete(attachmentID);
                    }
                );

                actions->addChild(viewItem);
                actions->addChild(delItem);
                actions->updateLayout();
                row->addChild(actions);
            }

            content->addChild(row);
        };

        for (auto const& attachment : attachments)
            addRow(
                fmt::format("Attachment {}", attachment.id),
                "",
                attachment.url,
                nullptr,
                attachment.id
            );

        for (auto const& path : pending)
            addRow(
                path.filename().string(),
                "Pending upload",
                "",
                &path,
                0
            );

        if (attachments.empty() && pending.empty()) {
            auto empty = CCLabelBMFont::create("No attachments.", "chatFont.fnt");
            empty->setScale(.34f);
            content->addChild(empty);
        }

        content->updateLayout();
        content->setContentSize({
            content->getContentWidth(),
            std::max(content->getContentHeight(), 62.f * static_cast<float>(attachments.size() + pending.size()) + 20.f)
        });
        scroll->setContentSize({332.f, 205.f});
        root->addChild(scroll);

        auto addMenu = CCMenu::create();
        addMenu->setContentSize({332.f, 36.f});
        addMenu->setLayout(
            RowLayout::create()
                ->setAxisAlignment(AxisAlignment::End)
                ->setCrossAxisAlignment(AxisAlignment::Center)
        );

        auto add = ButtonSprite::create(
            "Add attachment", "goldFont.fnt", "GJ_button_01.png", .34f
        );
        add->setScale(.34f);
        auto addItem = CCMenuItemExt::createSpriteExtra(
            add, [this](auto) {
                if (m_onAdd) {
                    m_onAdd();
                    removeFromParent();
                }
            }
        );
        addMenu->addChild(addItem);
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
    CCNode* m_commentsContainer = nullptr;

    bool init(std::string modID, CCNode* textArea) {
        if (!CCLayer::init()) return false;

        m_modID = std::move(modID);
        m_textArea = textArea;
        setContentSize(textArea->getContentSize());
        setAnchorPoint({0.f, 0.f});
        setKeyboardEnabled(true);

        auto width = getContentWidth();
        auto height = getContentHeight();

        auto bg = NineSlice::create("square02b_001.png");
        bg->setColor(ccBLACK);
        bg->setOpacity(105);
        bg->setScale(.3f);
        bg->setContentSize(getContentSize() / bg->getScale());
        addChildAtPosition(bg, Anchor::Center, 0, false);

        auto root = CCNode::create();
        root->setContentSize(getContentSize());
        root->setLayout(AnchorLayout::create());
        addChild(root);

        auto top = CCNode::create();
        top->setContentSize({width - 6.f, 30.f});
        auto topBg = NineSlice::create("square02b_001.png");
        topBg->setColor(ccBLACK);
        topBg->setOpacity(130);
        topBg->setScale(.3f);
        topBg->setContentSize(top->getContentSize() / topBg->getScale());
        top->addChildAtPosition(topBg, Anchor::Center, 0, false);

        m_lockLabel = CCLabelBMFont::create("Unlocked", "goldFont.fnt");
        m_lockLabel->setScale(.38f);
        m_lockLabel->setAnchorPoint({0.f, .5f});
        top->addChildAtPosition(m_lockLabel, Anchor::Left, ccp(10.f, 0.f), false);

        auto lockSpr = ButtonSprite::create("Lock", "goldFont.fnt", "GJ_button_01.png", .40f);
        lockSpr->setScale(.40f);
        m_lockButton = CCMenuItemExt::createSpriteExtra(
            lockSpr, [this](auto) { showLockPicker(); }
        );

        auto topMenu = CCMenu::create();
        topMenu->setContentSize(top->getContentSize());
        topMenu->setLayout(
            RowLayout::create()
                ->setAxisAlignment(AxisAlignment::End)
                ->setPadding(Padding::horizontal(8.f))
        );
        topMenu->addChild(m_lockButton);
        topMenu->updateLayout();
        top->addChild(topMenu);
        root->addChild(top);
        top->setLayoutOptions(
            AnchorLayoutOptions::create()
                ->setAnchor(Anchor::Top)
        );

        auto side = CCNode::create();
        side->setContentSize({76.f, height - 100.f});
        side->setLayout(
            ColumnLayout::create()
                ->setAxisAlignment(AxisAlignment::Between)
                ->setCrossAxisAlignment(AxisAlignment::Center)
                ->setPadding(Padding::symmetric(0.f, 8.f))
        );

        auto versionTop = CCNode::create();
        versionTop->setContentSize({76.f, 54.f});
        versionTop->setLayout(
            ColumnLayout::create()
                ->setAxisAlignment(AxisAlignment::Between)
                ->setCrossAxisAlignmentAlignment(AxisAlignment::Center)
        );

        auto currentTitle = CCLabelBMFont::create("Current", "goldFont.fnt");
        currentTitle->setScale(.30f);
        auto currentVersion = CCLabelBMFont::create("v-", "chatFont.fnt");
        currentVersion->setScale(.34f);
        m_versionLabel = currentVersion;
        versionTop->addChild(currentTitle);
        versionTop->addChild(currentVersion);
        versionTop->updateLayout();

        auto versionTab = OpenGeodeTabSprite::create(
            "version.png"_spr, "Versions", 110.f
        );
        versionTab->setScale(.58f);
        auto versionItem = CCMenuItemExt::createSpriteExtra(
            versionTab, [this](auto) { showVersionPicker(); }
        );
        auto versionMenu = CCMenu::create();
        versionMenu->setContentSize({76.f, 40.f});
        versionMenu->setLayout(
            RowLayout::create()
                ->setAxisAlignment(AxisAlignment::Center)
                ->setCrossAxisAlignment(AxisAlignment::Center)
        );
        versionMenu->addChild(versionItem);
        versionMenu->updateLayout();

        side->addChild(versionTop);
        side->addChild(versionMenu);
        side->updateLayout();
        root->addChild(side);
        side->setLayoutOptions(
            AnchorLayoutOptions::create()
                ->setAnchor(Anchor::Left)
                ->setOffset(ccp(3.f, -15.f))
        );

        auto content = CCNode::create();
        content->setContentSize({
            std::max(1.f, width - 88.f),
            std::max(1.f, height - 100.f)
        });
        content->setLayout(AnchorLayout::create());
        root->addChild(content);
        content->setLayoutOptions(
            AnchorLayoutOptions::create()
                ->setAnchor(Anchor::Right)
                ->setOffset(ccp(-3.f, -15.f))
        );

        auto scroll = ScrollLayer::create({
            content->getContentWidth(),
            content->getContentHeight() - 4.f
        });
        scroll->setID("opengeode-comments-scroll"_spr);
        content->addChild(scroll);
        scroll->setLayoutOptions(
            AnchorLayoutOptions::create()
                ->setAnchor(Anchor::Top)
                ->setOffset(ccp(0.f, -2.f))
        );
        m_commentsContainer = scroll->m_contentLayer;

        auto bottom = CCNode::create();
        bottom->setContentSize({
            content->getContentWidth(),
            56.f
        });
        bottom->setLayout(
            RowLayout::create()
                ->setAxisAlignment(AxisAlignment::Center)
                ->setCrossAxisAlignment(AxisAlignment::Center)
                ->setGap(6.f)
                ->setPadding(Padding::horizontal(4.f))
        );

        auto attachSprite = CCSprite::createWithSpriteFrameName("GJ_plusBtn_001.png");
        if (!attachSprite)
            attachSprite = CCSprite::createWithSpriteFrameName("GJ_plusBtn_001.png");
        if (attachSprite)
            limitNodeSize(attachSprite, {28.f, 28.f}, 1.f, .1f);

        auto attachItem = CCMenuItemExt::createSpriteExtra(
            attachSprite ? static_cast<CCNode*>(attachSprite) : static_cast<CCNode*>(CCLabelBMFont::create("+", "bigFont.fnt")),
            [this](auto) { showAttachmentsPopup(); }
        );
        auto attachMenu = CCMenu::create();
        attachMenu->setContentSize({36.f, 40.f});
        attachMenu->setLayout(
            RowLayout::create()
                ->setAxisAlignment(AxisAlignment::Center)
                ->setCrossAxisAlignment(AxisAlignment::Center)
        );
        attachMenu->addChild(attachItem);
        attachMenu->updateLayout();

        m_input = TextInput::create(
            std::max(80.f, content->getContentWidth() - 145.f),
            "Write a comment...",
            "chatFont.fnt"
        );
        m_input->setID("opengeode-comment-input"_spr);
        m_input->setCommonFilter(CommonFilter::Any);
        m_input->setMaxCharCount(2000);

        auto send = ButtonSprite::create(
            "Send", "goldFont.fnt", "GJ_button_01.png", .52f
        );
        send->setScale(.52f);
        m_sendButton = CCMenuItemExt::createSpriteExtra(
            send, [this](auto) { submitComment(); }
        );
        auto sendMenu = CCMenu::create();
        sendMenu->setContentSize({62.f, 44.f});
        sendMenu->setLayout(
            RowLayout::create()
                ->setAxisAlignment(AxisAlignment::Center)
                ->setCrossAxisAlignment(AxisAlignment::Center)
        );
        sendMenu->addChild(m_sendButton);
        sendMenu->updateLayout();

        bottom->addChild(attachMenu);
        bottom->addChild(m_input);
        bottom->addChild(sendMenu);
        bottom->updateLayout();

        content->addChild(bottom);
        bottom->setLayoutOptions(
            AnchorLayoutOptions::create()
                ->setAnchor(Anchor::Bottom)
                ->setOffset(ccp(0.f, 2.f))
        );

        m_statusLabel = CCLabelBMFont::create("", "chatFont.fnt");
        m_statusLabel->setScale(.24f);
        m_statusLabel->setAnchorPoint({1.f, .5f});
        content->addChild(m_statusLabel);
        m_statusLabel->setLayoutOptions(
            AnchorLayoutOptions::create()
                ->setAnchor(Anchor::TopRight)
                ->setOffset(ccp(-8.f, -8.f))
        );

        root->updateLayout();
        content->updateLayout();

        load();
        return true;
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
            getChildByID("opengeode-comments-scroll"_spr)
        );
        if (!scroll) return;

        auto width = scroll->getContentWidth() - 10.f;
        float totalHeight = 8.f;
        bool any = false;

        auto list = CCNode::create();
        list->setContentSize({width, 0.f});
        list->setLayout(
            ColumnLayout::create()
                ->setAxisAlignment(AxisAlignment::Start)
                ->setCrossAxisAlignment(AxisAlignment::Center)
                ->setGap(7.f)
                ->setPadding(Padding::uniform(4.f))
        );

        for (auto const& comment : m_state.comments) {
            any = true;

            auto body = MDTextArea::create(
                comment.body.empty() ? "..." : comment.body,
                {width - 76.f, 68.f},
                true
            );
            body->setScale(1.05f);
            body->getScrollLayer()->m_cutContent = false;
            body->getScrollLayer()->m_disableMovement = true;
            body->getScrollLayer()->setMouseEnabled(false);
            if (auto bg = body->getChildByType<CCScale9Sprite>(0))
                bg->setVisible(false);

            auto header = CCNode::create();
            header->setContentSize({width - 16.f, 34.f});
            header->setLayout(
                RowLayout::create()
                    ->setAxisAlignment(AxisAlignment::Start)
                    ->setCrossAxisAlignment(AxisAlignment::Center)
                    ->setGap(7.f)
            );

            auto avatar = CCNode::create();
            avatar->setContentSize({34.f, 34.f});
            avatar->setLayout(
                RowLayout::create()
                    ->setAxisAlignment(AxisAlignment::Center)
                    ->setCrossAxisAlignment(AxisAlignment::Center)
            );
            auto avatarBG = CCScale9Sprite::create("square02_small.png");
            avatarBG->setColor(ccBLACK);
            avatarBG->setOpacity(100);
            avatarBG->setContentSize({28.f, 28.f});
            avatar->addChild(avatarBG);
            addAvatar(avatar, comment);

            auto name = CCLabelBMFont::create(
                comment.username.c_str(), "goldFont.fnt"
            );
            name->setScale(.32f);
            header->addChild(avatar);
            header->addChild(name);
            header->updateLayout();

            auto card = CCNode::create();
            auto cardHeight = 42.f + 68.f +
                (comment.attachments.empty() ? 0.f :
                    28.f * static_cast<float>(comment.attachments.size()) + 4.f) +
                ((comment.canEdit || comment.canDelete) ? 32.f : 0.f);
            card->setContentSize({width, cardHeight});
            card->setLayout(AnchorLayout::create());

            auto cardBG = NineSlice::create("square02b_001.png");
            cardBG->setColor(ccBLACK);
            cardBG->setOpacity(75);
            cardBG->setScale(.3f);
            cardBG->setContentSize(card->getContentSize() / cardBG->getScale());
            card->addChild(cardBG, -1);
            cardBG->setLayoutOptions(
                AnchorLayoutOptions::create()->setAnchor(Anchor::Center)
            );

            auto stack = CCNode::create();
            stack->setContentSize({
                width - 12.f,
                cardHeight - 8.f
            });
            stack->setLayout(
                ColumnLayout::create()
                    ->setAxisAlignment(AxisAlignment::Start)
                    ->setCrossAxisAlignment(AxisAlignment::Center)
                    ->setGap(4.f)
                    ->setPadding(Padding::uniform(2.f))
            );
            card->addChild(stack);
            stack->setLayoutOptions(
                AnchorLayoutOptions::create()->setAnchor(Anchor::Center)
            );

            stack->addChild(header);
            stack->addChild(body);

            for (auto const& attachment : comment.attachments) {
                auto itemMenu = CCMenu::create();
                itemMenu->setContentSize({width - 28.f, 24.f});
                itemMenu->setLayout(
                    RowLayout::create()
                        ->setAxisAlignment(AxisAlignment::Start)
                        ->setCrossAxisAlignment(AxisAlignment::Center)
                        ->setGap(5.f)
                );

                auto image = LazySprite::create({24.f, 24.f}, false);
                if (image) {
                    image->loadFromUrl(attachment.url);
                    image->setLoadCallback([image](Result<> result) {
                        if (!result) {
                            image->setVisible(false);
                            return;
                        }
                        limitNodeSize(image, {24.f, 24.f}, 1.f, .1f);
                    });
                }

                auto imageItem = CCMenuItemExt::createSpriteExtra(
                    image ? static_cast<CCNode*>(image)
                          : static_cast<CCNode*>(CCLabelBMFont::create("?", "chatFont.fnt")),
                    [url = attachment.url](auto) {
                        if (!url.empty())
                            web::openLinkInBrowser(url);
                    }
                );
                itemMenu->addChild(imageItem);

                auto attachmentLabel = CCLabelBMFont::create(
                    fmt::format("Attachment {}", attachment.id).c_str(),
                    "chatFont.fnt"
                );
                attachmentLabel->setScale(.25f);
                itemMenu->addChild(attachmentLabel);
                itemMenu->updateLayout();
                stack->addChild(itemMenu);
            }

            if (comment.canEdit || comment.canDelete) {
                auto actions = CCMenu::create();
                actions->setContentSize({width - 16.f, 26.f});
                actions->setLayout(
                    RowLayout::create()
                        ->setAxisAlignment(AxisAlignment::End)
                        ->setCrossAxisAlignment(AxisAlignment::Center)
                        ->setGap(5.f)
                );

                if (comment.canEdit) {
                    auto button = ButtonSprite::create(
                        "Edit", "goldFont.fnt", "GJ_button_01.png", .30f
                    );
                    button->setScale(.30f);
                    actions->addChild(
                        CCMenuItemExt::createSpriteExtra(
                            button, [this, comment](auto) {
                                beginEdit(comment);
                            }
                        )
                    );
                }

                if (comment.canDelete) {
                    auto button = ButtonSprite::create(
                        "Delete", "goldFont.fnt", "GJ_button_06.png", .30f
                    );
                    button->setScale(.30f);
                    actions->addChild(
                        CCMenuItemExt::createSpriteExtra(
                            button, [this, comment](auto) {
                                deleteComment(comment.id);
                            }
                        )
                    );
                }

                actions->updateLayout();
                stack->addChild(actions);
            }

            stack->updateLayout();
            list->addChild(card);
            totalHeight += cardHeight + 7.f;
        }

        if (!any) {
            auto empty = CCLabelBMFont::create(
                "No comments yet.", "chatFont.fnt"
            );
            empty->setScale(.35f);
            list->addChild(empty);
            totalHeight += 35.f;
        }

        list->setContentSize({width, std::max(totalHeight, 20.f)});
        list->updateLayout();
        m_commentsContainer->setContentSize({
            scroll->getContentWidth(),
            std::max(scroll->getContentHeight(), totalHeight + 8.f)
        });
        list->setPosition({
            scroll->getContentWidth() / 2.f,
            list->getContentHeight() / 2.f
        });
        m_commentsContainer->addChild(list);
        scroll->scrollToTop();

        if (!m_state.selectedVersion.empty()) {
            m_versionLabel->setString(
                fmt::format("v{}", m_state.selectedVersion).c_str()
            );
        }

        auto allowed = canComment();
        m_input->setVisible(allowed);
        m_sendButton->setVisible(allowed);
        m_lockButton->setVisible(m_state.currentDeveloperAdmin);
        m_lockLabel->setVisible(true);

        auto lockText = m_state.lock == "none"
            ? "Unlocked"
            : (m_state.lock == "internal" ? "Internal" : "Locked");
        auto lockDisplayText = m_state.lock == "none"
            ? std::string("Unlocked")
            : fmt::format("{} by {}", lockText, m_state.lockedByName);
        m_lockLabel->setString(lockDisplayText.c_str());

        if (!m_state.loggedIn)
            m_statusLabel->setString("Log in to comment.");
        else if (
            m_state.lock == "locked" &&
            !m_state.currentDeveloperAdmin
        )
            m_statusLabel->setString("This submission is locked.");
        else if (
            m_state.lock == "internal" &&
            !m_state.currentDeveloperAdmin
        )
            m_statusLabel->setString(
                "This submission is locked to the index team."
            );
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

        if (std::filesystem::exists(path)) {
            auto sprite = CCSprite::create(path.string().c_str());
            if (sprite) {
                limitNodeSize(sprite, {28.f, 28.f}, 1.f, .1f);
                avatar->addChildAtPosition(
                    sprite, Anchor::Center, 0, false
                );
            }
            return;
        }

        auto sprite = LazySprite::create({28.f, 28.f}, false);
        if (!sprite) return;

        avatar->addChildAtPosition(
            sprite, Anchor::Center, 0, false
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

    void showVersionPicker() {
        if(m_state.versions.empty())return;
        auto popup=VersionSelectPopup::create(m_state.versions,[this](std::string version){
            m_state.selectedVersion=std::move(version);m_editingCommentID=0;m_pendingFiles.clear();m_removedAttachments.clear();m_input->setString("");m_attachmentLabel->setString("");loadSelectedVersion();
        });
        if(popup){popup->m_noElasticity=true;popup->show();}
    }

    void showAttachmentsPopup() {
        std::vector<CommentAttachment> attachments;
        for(auto const& c:m_state.comments)if(c.id==m_editingCommentID){attachments=c.attachments;break;}
        auto popup=AttachmentPopup::create(
            std::move(attachments), m_pendingFiles,
            [this](int id) { removeAttachment(id); },\n            [this]() { pickAttachments(); }\n        );
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

                m_pendingFiles = std::move(result).unwrap();
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

    void beginEdit(CommentData const& comment) {
        if (!comment.canEdit) return;

        m_editingCommentID = comment.id;
        m_removedAttachments.clear();
        m_pendingFiles.clear();
        m_input->setString(comment.body.c_str());
        m_input->focus();
        m_attachmentLabel->setString("");
        m_statusLabel->setString(
            "Editing comment. Press Send to save."
        );
        rebuild();
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
