#include "CommentsPopup.hpp"
#include "Settings.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/MDTextArea.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/web.hpp>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace geode::prelude;

namespace opengeode {
namespace {

struct CommentAttachment {
    std::string id;
    std::string name;
    std::string url;
};

struct CommentData {
    int id = 0;
    std::string body;
    std::string version;
    std::string username;
    std::string pfp;
    bool canEdit = false;
    bool canDelete = false;
    std::vector<CommentAttachment> attachments;
};

struct CommentState {
    std::vector<CommentData> comments;
    std::vector<std::string> versions;
    std::string selectedVersion;
    bool locked = false;
    bool devOnly = false;
    bool admin = false;
    bool dev = false;
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

bool boolValue(matjson::Value const& value, char const* key, bool fallback = false) {
    return value[key].asBool().unwrapOr(fallback);
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

class CommentsLayer : public CCLayer {
    std::string m_modID;
    CCNode* m_textArea = nullptr;
    CommentState m_state;
    async::TaskHolder<web::WebResponse> m_requestTask;
    async::TaskHolder<web::WebResponse> m_uploadTask;
    std::vector<std::filesystem::path> m_pendingFiles;

    CCMenu* m_versionMenu = nullptr;
    CCLabelBMFont* m_versionLabel = nullptr;
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

        auto commentsScroll = ScrollLayer::create({width - 4.f, height - 43.f});
        commentsScroll->setPosition({2.f, 41.f});
        commentsScroll->setID("opengeode-comments-scroll"_spr);
        addChild(commentsScroll);
        m_commentsContainer = commentsScroll->m_contentLayer;

        auto bottom = CCMenu::create();
        bottom->setContentSize({width - 4.f, 38.f});
        bottom->setPosition({2.f, 1.f});
        addChild(bottom);

        auto versionSprite = ButtonSprite::create("Version", "goldFont.fnt", getButtonTexture("GJ_button_01.png"), .42f);
        versionSprite->setScale(.42f);
        auto versionItem = CCMenuItemExt::createSpriteExtra(versionSprite, [this](auto) { showVersionPicker(); });
        versionItem->setPosition({42.f, 19.f});
        m_versionMenu = bottom;
        bottom->addChild(versionItem);

        m_versionLabel = CCLabelBMFont::create("Version: -", "chatFont.fnt");
        m_versionLabel->setScale(.27f);
        m_versionLabel->setAnchorPoint({0.f, .5f});
        m_versionLabel->setPosition({68.f, 28.f});
        bottom->addChild(m_versionLabel);

        auto attachLabel = CCLabelBMFont::create("Attach", "goldFont.fnt");
        attachLabel->setScale(.36f);
        auto attachItem = CCMenuItemExt::createSpriteExtra(attachLabel, [this](auto) { pickAttachments(); });
        attachItem->setPosition({68.f, 9.f});
        bottom->addChild(attachItem);

        m_attachmentLabel = CCLabelBMFont::create("", "chatFont.fnt");
        m_attachmentLabel->setScale(.23f);
        m_attachmentLabel->setAnchorPoint({0.f, .5f});
        m_attachmentLabel->setPosition({91.f, 9.f});
        bottom->addChild(m_attachmentLabel);

        m_input = TextInput::create(width - 151.f, "Write a comment...", "chatFont.fnt");
        m_input->setID("opengeode-comment-input"_spr);
        m_input->setCommonFilter(CommonFilter::Any);
        m_input->setMaxCharCount(2000);
        m_input->setPosition({width / 2.f + 32.f, 19.f});
        bottom->addChild(m_input);

        auto sendSprite = ButtonSprite::create("Send", "goldFont.fnt", getButtonTexture("GJ_button_01.png"), .45f);
        sendSprite->setScale(.45f);
        m_sendButton = CCMenuItemExt::createSpriteExtra(sendSprite, [this](auto) { submitComment(); });
        m_sendButton->setPosition({width - 24.f, 19.f});
        bottom->addChild(m_sendButton);

        m_statusLabel = CCLabelBMFont::create("", "chatFont.fnt");
        m_statusLabel->setScale(.25f);
        m_statusLabel->setColor({255, 220, 90});
        m_statusLabel->setAnchorPoint({.5f, .5f});
        m_statusLabel->setPosition({width / 2.f, 27.f});
        addChild(m_statusLabel);

        load();
        return true;
    }

    void request(std::string method, std::string path, std::function<void(web::WebResponse)> callback) {
        auto request = web::WebRequest();
        auto token = getAuthAccessToken();
        if (!token.empty()) request.header("Authorization", "Bearer " + token);
        m_requestTask.spawn(request.send(method, trimSlash(getIndexUrl()) + path), [callback = std::move(callback)](web::WebResponse response) mutable {
            callback(std::move(response));
        });
    }

    void load() {
        m_statusLabel->setString("Loading comments...");
        request("GET", fmt::format("/v1/mods/{}/comments", m_modID), [this](web::WebResponse response) {
            if (!response.ok()) {
                m_statusLabel->setString(errorText(response).c_str());
                return;
            }
            auto json = response.json().unwrapOr(matjson::Value());
            parseState(json);
            rebuild();
        });
    }

    void parseState(matjson::Value const& json) {
        auto payload = json["payload"].isObject() ? json["payload"] : json;
        m_state.loggedIn = hasAuthTokens();
        m_state.locked = boolValue(payload, "locked");
        m_state.devOnly = boolValue(payload, "dev_only") || boolValue(payload, "internal");
        m_state.admin = boolValue(payload, "admin");
        m_state.dev = boolValue(payload, "dev") || boolValue(payload, "developer");

        m_state.versions.clear();
        auto versions = payload["versions"];
        if (versions.isArray()) {
            for (auto const& version : versions) {
                auto value = version.isObject() ? stringValue(version, "version") : version.asString().unwrapOr("");
                if (!value.empty()) m_state.versions.push_back(value);
            }
        }

        m_state.comments.clear();
        auto comments = payload["comments"];
        if (comments.isArray()) {
            for (auto const& raw : comments) {
                CommentData comment;
                comment.id = raw["id"].asInt().unwrapOr(0);
                comment.body = stringValue(raw, "body");
                comment.version = stringValue(raw, "version");
                comment.username = stringValue(raw, "username", stringValue(raw, "user", "Unknown"));
                comment.pfp = stringValue(raw, "pfp", stringValue(raw, "avatar_url", stringValue(raw, "avatar")));
                comment.canEdit = boolValue(raw, "can_edit");
                comment.canDelete = boolValue(raw, "can_delete");

                auto user = raw["user"];
                if (user.isObject()) {
                    comment.username = stringValue(user, "username", comment.username);
                    comment.pfp = stringValue(user, "pfp", stringValue(user, "avatar_url", comment.pfp));
                }

                auto attachments = raw["attachments"];
                if (attachments.isArray()) {
                    for (auto const& attachment : attachments) {
                        CommentAttachment item;
                        item.id = stringValue(attachment, "id");
                        item.name = stringValue(attachment, "name", "Attachment");
                        item.url = stringValue(attachment, "url");
                        comment.attachments.push_back(std::move(item));
                    }
                }
                m_state.comments.push_back(std::move(comment));
            }
        }

        if (m_state.selectedVersion.empty() && !m_state.versions.empty())
            m_state.selectedVersion = m_state.versions.front();
        if (m_state.selectedVersion.empty() && !m_state.comments.empty())
            m_state.selectedVersion = m_state.comments.front().version;

        bool allowed = m_state.loggedIn && (!m_state.locked || m_state.devOnly == false || m_state.admin || m_state.dev);
        if (!allowed) {
            if (!m_state.loggedIn) m_statusLabel->setString("Log in to comment.");
            else if (m_state.locked && !m_state.admin && !m_state.dev) m_statusLabel->setString("Comments are locked.");
        }
    }

    void rebuild() {
        if (!m_commentsContainer) return;
        m_commentsContainer->removeAllChildren();

        auto scroll = typeinfo_cast<ScrollLayer*>(getChildByID("opengeode-comments-scroll"_spr));
        if (!scroll) return;

        float y = 4.f;
        for (auto const& comment : m_state.comments) {
            if (!m_state.selectedVersion.empty() && comment.version != m_state.selectedVersion) continue;
            auto card = CCNode::create();
            card->setContentSize({scroll->getContentWidth() - 8.f, 0.f});

            auto avatar = CCScale9Sprite::create("square02_small.png");
            avatar->setOpacity(70);
            avatar->setContentSize({38.f, 38.f});
            card->addChildAtPosition(avatar, Anchor::TopLeft, {19.f, -19.f});

            addAvatar(card, comment, {19.f, -19.f});

            auto name = CCLabelBMFont::create(comment.username.c_str(), "goldFont.fnt");
            name->setScale(.30f);
            name->setAnchorPoint({0.f, .5f});
            name->setPosition({42.f, -10.f});
            card->addChild(name);

            auto version = CCLabelBMFont::create(comment.version.c_str(), "chatFont.fnt");
            version->setScale(.22f);
            version->setColor({180, 180, 180});
            version->setAnchorPoint({0.f, .5f});
            version->setPosition({42.f, -22.f});
            card->addChild(version);

            auto body = MDTextArea::create(comment.body.empty() ? "..." : comment.body, {card->getContentWidth() - 50.f, 80.f}, true);
            body->setAnchorPoint({0.f, 1.f});
            body->setPosition({42.f, -31.f});
            body->getScrollLayer()->m_cutContent = false;
            body->getScrollLayer()->m_disableMovement = true;
            body->getScrollLayer()->setMouseEnabled(false);
            body->getChildByType<CCScale9Sprite>(0)->setVisible(false);
            card->addChild(body);

            auto bodyHeight = std::max(28.f, body->getScrollLayer()->m_contentLayer->getContentHeight());
            float cardHeight = 38.f + bodyHeight;
            if (!comment.attachments.empty()) {
                auto attachments = CCLabelBMFont::create(fmt::format("📎 {} attachment{}", comment.attachments.size(), comment.attachments.size() == 1 ? "" : "s").c_str(), "chatFont.fnt");
                attachments->setScale(.24f);
                attachments->setColor({180, 210, 255});
                attachments->setAnchorPoint({0.f, .5f});
                attachments->setPosition({42.f, -cardHeight + 10.f});
                card->addChild(attachments);
                cardHeight += 14.f;
            }

            if (comment.canEdit || comment.canDelete) {
                auto actions = CCMenu::create();
                actions->setPosition({card->getContentWidth() - 4.f, -12.f});
                if (comment.canEdit) {
                    auto edit = CCLabelBMFont::create("[EDIT]", "chatFont.fnt");
                    edit->setScale(.22f);
                    edit->setColor({255, 210, 80});
                    auto item = CCMenuItemExt::createSpriteExtra(edit, [this, comment](auto) { beginEdit(comment); });
                    actions->addChild(item);
                }
                if (comment.canDelete) {
                    auto del = CCLabelBMFont::create("[DELETE]", "chatFont.fnt");
                    del->setScale(.22f);
                    del->setColor({255, 100, 100});
                    auto item = CCMenuItemExt::createSpriteExtra(del, [this, comment](auto) { deleteComment(comment.id); });
                    actions->addChild(item);
                }
                actions->setLayout(RowLayout::create()->setGap(4.f));
                actions->updateLayout();
                card->addChild(actions);
            }

            auto bg = CCScale9Sprite::create("square02_small.png");
            bg->setOpacity(55);
            bg->setZOrder(-1);
            bg->setContentSize({card->getContentWidth(), cardHeight});
            bg->setAnchorPoint({0.f, 1.f});
            bg->setPosition({0.f, 0.f});
            card->addChild(bg);

            card->setContentHeight(cardHeight);
            card->setPosition({4.f, y + cardHeight});
            m_commentsContainer->addChild(card);
            y += cardHeight + 5.f;
        }

        if (m_state.comments.empty()) {
            auto empty = CCLabelBMFont::create("No comments for this version.", "chatFont.fnt");
            empty->setScale(.3f);
            empty->setAnchorPoint({.5f, .5f});
            empty->setPosition({scroll->getContentWidth() / 2.f, 55.f});
            m_commentsContainer->addChild(empty);
        }

        m_commentsContainer->setContentSize({scroll->getContentWidth(), std::max(scroll->getContentHeight(), y + 8.f)});
        scroll->scrollToTop();

        bool allowed = m_state.loggedIn && (!m_state.locked || m_state.admin || m_state.dev);
        m_input->setVisible(allowed);
        m_sendButton->setVisible(allowed);
        m_versionMenu->setVisible(allowed || !m_state.versions.empty());
        m_attachmentLabel->setVisible(allowed);
        if (allowed) m_statusLabel->setString("");
    }

    void addAvatar(CCNode* card, CommentData const& comment, CCPoint center) {
        if (comment.pfp.empty()) return;
        auto key = std::hash<std::string>{}(comment.pfp);
        auto path = Mod::get()->getSaveDir() / fmt::format("pfp-{:x}.png", key);
        auto request = web::WebRequest();
        request.get(comment.pfp, Mod::get())->listen([card = Ref(card), path](web::WebResponse response) {
            if (!card || !response.ok()) return;
            if (!response.into(path)) return;
            auto sprite = CCSprite::create(path.string().c_str());
            if (!sprite) return;
            sprite->setScale(std::min(32.f / sprite->getContentSize().width, 32.f / sprite->getContentSize().height));
            sprite->setPosition({19.f, -19.f});
            card->addChild(sprite);
        });
    }

    void showVersionPicker() {
        if (m_state.versions.empty()) return;
        auto popup = createQuickPopup("Select Version", "", "Cancel", nullptr, nullptr);
        if (!popup) return;
        auto menu = CCMenu::create();
        menu->setContentSize({150.f, 160.f});
        menu->setLayout(ColumnLayout::create()->setGap(4.f));
        for (auto const& version : m_state.versions) {
            auto item = CCMenuItemExt::createSpriteExtra(ButtonSprite::create(version.c_str(), "bigFont.fnt", getButtonTexture("GJ_button_01.png"), .35f), [this, version, popup](auto) {
                m_state.selectedVersion = version;
                m_versionLabel->setString(fmt::format("Version: {}", version).c_str());
                popup->removeFromParent();
                rebuild();
            });
            menu->addChild(item);
        }
        popup->m_mainLayer->addChildAtPosition(menu, Anchor::Center);
        popup->show();
    }

    void pickAttachments() {
        file::FilePickOptions options;
        options.filters.push_back({"Images", {"png", "jpg", "jpeg", "gif", "webp"}});
        options.filters.push_back({"All Files", {}});
        m_uploadTask.spawn(
            "Pick comment attachments",
            file::pickMany(options),
            [this](std::vector<std::filesystem::path> paths) {
                m_pendingFiles = std::move(paths);
                m_attachmentLabel->setString(fmt::format("{} file{}", m_pendingFiles.size(), m_pendingFiles.size() == 1 ? "" : "s").c_str());
            }
        );
    }

    void submitComment() {
        if (m_state.selectedVersion.empty()) {
            m_statusLabel->setString("Select a version first.");
            return;
        }
        if (m_input->getString().empty() && m_pendingFiles.empty()) {
            m_statusLabel->setString("Write something or attach a file.");
            return;
        }
        if (!hasAuthTokens()) {
            m_statusLabel->setString("Log in to comment.");
            return;
        }

        auto form = web::MultipartForm();
        form.param("version", m_state.selectedVersion);
        form.param("body", std::string(m_input->getString().c_str()));
        for (auto const& path : m_pendingFiles) {
            auto result = form.file("attachments", path);
            if (!result) {
                m_statusLabel->setString("Could not read an attachment.");
                return;
            }
        }

        auto request = web::WebRequest();
        auto token = getAuthAccessToken();
        request.header("Authorization", "Bearer " + token);
        request.bodyMultipart(form);
        m_sendButton->setEnabled(false);
        m_statusLabel->setString("Posting...");
        m_requestTask.spawn(request.post(trimSlash(getIndexUrl()) + fmt::format("/v1/mods/{}/comments", m_modID)), [this](web::WebResponse response) {
            m_sendButton->setEnabled(true);
            if (!response.ok()) {
                m_statusLabel->setString(errorText(response).c_str());
                return;
            }
            m_pendingFiles.clear();
            m_attachmentLabel->setString("");
            m_input->setString("");
            load();
        });
    }

    void beginEdit(CommentData const& comment) {
        m_input->setString(comment.body.c_str());
        m_input->focus();
        auto edit = CCNode::create();
        edit->setTag(comment.id);
        edit->setID("opengeode-editing-comment"_spr);
        addChild(edit);
        m_statusLabel->setString("Editing comment. Press Send to save.");
        if (auto old = getChildByID("opengeode-editing-comment"_spr)) {
            if (old != edit) old->removeFromParent();
        }
    }

    void deleteComment(int id) {
        createQuickPopup("Delete Comment", "Delete this comment?", "Cancel", "Delete", [this, id](auto, bool confirmed) {
            if (!confirmed) return;
            auto request = web::WebRequest();
            auto token = getAuthAccessToken();
            if (!token.empty()) request.header("Authorization", "Bearer " + token);
            m_requestTask.spawn(request.send("DELETE", trimSlash(getIndexUrl()) + fmt::format("/v1/mods/{}/comments/{}", m_modID, id)), [this](web::WebResponse response) {
                if (!response.ok()) {
                    m_statusLabel->setString(errorText(response).c_str());
                    return;
                }
                load();
            });
        });
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

void ensureCommentsTab(CCNode* popup) {
    if (!popup) return;
    auto modID = getModID(popup);
    if (modID.empty()) return;

    auto tabsMenu = popup->getChildByIDRecursive("tabs-menu");
    auto textarea = popup->getChildByIDRecursive("textarea");
    if (!tabsMenu || !textarea) return;
    if (tabsMenu->getChildByID("opengeode-comments-tab")) return;

    auto description = typeinfo_cast<CCMenuItemSpriteExtra*>(tabsMenu->getChildByID("description"));
    auto changelog = typeinfo_cast<CCMenuItemSpriteExtra*>(tabsMenu->getChildByID("changelog"));
    if (!description || !changelog) return;

    auto descriptionSprite = typeinfo_cast<GeodeTabSprite*>(description->getNormalImage());
    auto changelogSprite = typeinfo_cast<GeodeTabSprite*>(changelog->getNormalImage());
    if (!descriptionSprite || !changelogSprite) return;

    auto tabSprite = GeodeTabSprite::create("GJ_chatIcon_001.png", "Comments", 140.f);
    if (!tabSprite) return;
    tabSprite->select(0);

    auto oldListener = description->m_pListener;
    auto oldSelector = description->m_pfnSelector;
    auto tabIndex = tabsMenu->getChildrenCount();

    auto callback = [modID, oldListener, oldSelector, tabIndex, textarea, descriptionSprite, changelogSprite, tabSprite](CCMenuItemSpriteExtra* sender) {
        auto parent = textarea->getParent();
        if (!parent) return;
        while (auto old = parent->getChildByType<CommentsLayer>(0)) old->removeFromParent();

        if (sender->getTag() < 2) {
            (oldListener->*oldSelector)(sender);
            tabSprite->select(0);
            textarea->setVisible(true);
        }
        else {
            descriptionSprite->select(0);
            changelogSprite->select(0);
            tabSprite->select(1);
            textarea->setVisible(false);
            parent->addChild(CommentsLayer::create(modID, parent));
        }
        sender->getParent()->setTag(sender->getTag());
    };

    CCMenuItemExt::assignCallback<CCMenuItemSpriteExtra>(description, callback);
    CCMenuItemExt::assignCallback<CCMenuItemSpriteExtra>(changelog, callback);

    auto item = CCMenuItemExt::createSpriteExtra(tabSprite, callback);
    item->m_pListener = description->m_pListener;
    item->setID("opengeode-comments-tab");
    tabsMenu->addChild(item, 0, tabIndex);
    tabsMenu->updateLayout();
}

} // namespace opengeode
