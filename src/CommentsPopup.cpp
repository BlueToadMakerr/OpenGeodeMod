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

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace geode::prelude;

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

class CommentsLayer : public CCLayer {
    std::string m_modID;
    CCNode* m_textArea = nullptr;
    CommentState m_state;

    async::TaskHolder<web::WebResponse> m_requestTask;
    std::vector<std::filesystem::path> m_pendingFiles;
    std::vector<int> m_removedAttachments;

    int m_editingCommentID = 0;

    CCMenu* m_versionMenu = nullptr;
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

        auto commentsScroll = ScrollLayer::create({width - 4.f, height - 43.f});
        commentsScroll->setPosition({2.f, 41.f});
        commentsScroll->setID("opengeode-comments-scroll"_spr);
        addChild(commentsScroll);
        m_commentsContainer = commentsScroll->m_contentLayer;

        auto bottom = CCMenu::create();
        bottom->setContentSize({width - 4.f, 38.f});
        bottom->setPosition({2.f, 1.f});
        addChild(bottom);

        auto versionSprite = ButtonSprite::create(
            "Version", "goldFont.fnt", "GJ_button_01.png", .42f
        );
        versionSprite->setScale(.42f);
        auto versionItem = CCMenuItemExt::createSpriteExtra(
            versionSprite, [this](auto) { showVersionPicker(); }
        );
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
        auto attachItem = CCMenuItemExt::createSpriteExtra(
            attachLabel, [this](auto) { pickAttachments(); }
        );
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

        auto sendSprite = ButtonSprite::create(
            "Send", "goldFont.fnt", "GJ_button_01.png", .45f
        );
        sendSprite->setScale(.45f);
        m_sendButton = CCMenuItemExt::createSpriteExtra(
            sendSprite, [this](auto) { submitComment(); }
        );
        m_sendButton->setPosition({width - 24.f, 19.f});
        bottom->addChild(m_sendButton);

        auto lockSprite = ButtonSprite::create(
            "Lock", "goldFont.fnt", "GJ_button_01.png", .36f
        );
        lockSprite->setScale(.36f);
        m_lockButton = CCMenuItemExt::createSpriteExtra(
            lockSprite, [this](auto) { showLockPicker(); }
        );
        m_lockButton->setPosition({width - 54.f, 9.f});
        bottom->addChild(m_lockButton);

        m_lockLabel = CCLabelBMFont::create("", "chatFont.fnt");
        m_lockLabel->setScale(.22f);
        m_lockLabel->setAnchorPoint({1.f, .5f});
        m_lockLabel->setPosition({width - 94.f, 9.f});
        bottom->addChild(m_lockLabel);

        m_statusLabel = CCLabelBMFont::create("", "chatFont.fnt");
        m_statusLabel->setScale(.25f);
        m_statusLabel->setColor({255, 220, 90});
        m_statusLabel->setAnchorPoint({.5f, .5f});
        m_statusLabel->setPosition({width / 2.f, 27.f});
        addChild(m_statusLabel);

        m_lockButton->setVisible(false);
        m_lockLabel->setVisible(false);

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

        float y = 4.f;
        bool hasVisibleComment = false;

        for (auto const& comment : m_state.comments) {
            hasVisibleComment = true;

            auto card = CCNode::create();
            card->setContentSize({
                scroll->getContentWidth() - 8.f,
                0.f
            });

            auto avatar = CCScale9Sprite::create("square02_small.png");
            avatar->setOpacity(70);
            avatar->setContentSize({38.f, 38.f});
            card->addChildAtPosition(
                avatar, Anchor::TopLeft, {19.f, -19.f}
            );

            addAvatar(card, comment);

            auto name = CCLabelBMFont::create(
                comment.username.c_str(), "goldFont.fnt"
            );
            name->setScale(.30f);
            name->setAnchorPoint({0.f, .5f});
            name->setPosition({42.f, -10.f});
            card->addChild(name);

            auto body = MDTextArea::create(
                comment.body.empty() ? "..." : comment.body,
                {card->getContentWidth() - 50.f, 80.f},
                true
            );
            body->setAnchorPoint({0.f, 1.f});
            body->setPosition({42.f, -26.f});
            body->getScrollLayer()->m_cutContent = false;
            body->getScrollLayer()->m_disableMovement = true;
            body->getScrollLayer()->setMouseEnabled(false);
            if (auto bg = body->getChildByType<CCScale9Sprite>(0))
                bg->setVisible(false);
            card->addChild(body);

            auto bodyHeight = std::max(
                28.f,
                body->getScrollLayer()->m_contentLayer->getContentHeight()
            );

            float cardHeight = 33.f + bodyHeight;

            for (auto const& attachment : comment.attachments) {
                auto filename = attachment.url.substr(
                    attachment.url.find_last_of('/') + 1
                );
                auto label = CCLabelBMFont::create(
                    fmt::format("[{}]", filename).c_str(),
                    "chatFont.fnt"
                );
                label->setScale(.23f);
                label->setColor({180, 210, 255});

                auto item = CCMenuItemExt::createSpriteExtra(
                    label,
                    [url = attachment.url](auto) {
                        if (!url.empty())
                            web::openLinkInBrowser(url);
                    }
                );

                auto menu = CCMenu::create();
                menu->setPosition({
                    48.f,
                    -cardHeight + 9.f
                });
                menu->addChild(item);

                if (m_editingCommentID == comment.id && comment.canEdit) {
                    auto remove = CCLabelBMFont::create("[x]", "chatFont.fnt");
                    remove->setScale(.22f);
                    remove->setColor({255, 100, 100});

                    auto removeItem = CCMenuItemExt::createSpriteExtra(
                        remove,
                        [this, id = attachment.id](auto) {
                            removeAttachment(id);
                        }
                    );
                    menu->addChild(removeItem);
                    menu->setLayout(RowLayout::create()->setGap(3.f));
                    menu->updateLayout();
                }

                card->addChild(menu);
                cardHeight += 12.f;
            }

            if (comment.canEdit || comment.canDelete) {
                auto actions = CCMenu::create();
                actions->setPosition({
                    card->getContentWidth() - 4.f,
                    -12.f
                });

                if (comment.canEdit) {
                    auto edit = CCLabelBMFont::create(
                        "[EDIT]", "chatFont.fnt"
                    );
                    edit->setScale(.22f);
                    edit->setColor({255, 210, 80});
                    actions->addChild(
                        CCMenuItemExt::createSpriteExtra(
                            edit,
                            [this, comment](auto) { beginEdit(comment); }
                        )
                    );
                }

                if (comment.canDelete) {
                    auto del = CCLabelBMFont::create(
                        "[DELETE]", "chatFont.fnt"
                    );
                    del->setScale(.22f);
                    del->setColor({255, 100, 100});
                    actions->addChild(
                        CCMenuItemExt::createSpriteExtra(
                            del,
                            [this, comment](auto) {
                                deleteComment(comment.id);
                            }
                        )
                    );
                }

                actions->setLayout(RowLayout::create()->setGap(4.f));
                actions->updateLayout();
                card->addChild(actions);
            }

            auto bg = CCScale9Sprite::create("square02_small.png");
            bg->setOpacity(55);
            bg->setZOrder(-1);
            bg->setContentSize({
                card->getContentWidth(),
                cardHeight
            });
            bg->setAnchorPoint({0.f, 1.f});
            bg->setPosition({0.f, 0.f});
            card->addChild(bg);

            card->setContentHeight(cardHeight);
            card->setPosition({4.f, y + cardHeight});
            m_commentsContainer->addChild(card);

            y += cardHeight + 5.f;
        }

        if (!hasVisibleComment) {
            auto empty = CCLabelBMFont::create(
                "No comments for this version.", "chatFont.fnt"
            );
            empty->setScale(.3f);
            empty->setAnchorPoint({.5f, .5f});
            empty->setPosition({
                scroll->getContentWidth() / 2.f, 55.f
            });
            m_commentsContainer->addChild(empty);
        }

        m_commentsContainer->setContentSize({
            scroll->getContentWidth(),
            std::max(scroll->getContentHeight(), y + 8.f)
        });
        scroll->scrollToTop();

        auto allowed = canComment();
        m_input->setVisible(allowed);
        m_sendButton->setVisible(allowed);
        m_versionMenu->setVisible(!m_state.versions.empty());
        m_attachmentLabel->setVisible(allowed && canUploadAttachments());

        m_lockButton->setVisible(m_state.currentDeveloperAdmin);
        m_lockLabel->setVisible(m_state.currentDeveloperAdmin);

        auto lockText = m_state.lock == "none"
            ? "Unlocked"
            : (m_state.lock == "internal" ? "Internal" : "Locked");
        m_lockLabel->setString(lockText);

        if (!m_state.loggedIn)
            m_statusLabel->setString("Log in to comment.");
        else if (m_state.lock == "locked" && !m_state.currentDeveloperAdmin)
            m_statusLabel->setString("This submission is locked.");
        else if (m_state.lock == "internal" && !m_state.currentDeveloperAdmin)
            m_statusLabel->setString("This submission is locked to the index team.");
        else if (!hasVisibleComment)
            m_statusLabel->setString("No comments yet.");
        else
            m_statusLabel->setString("");
    }

    void addAvatar(CCNode* card, CommentData const& comment) {
        if (comment.pfp.empty()) return;

        auto key = std::hash<std::string>{}(comment.pfp);
        auto path = Mod::get()->getSaveDir() /
            fmt::format("pfp-{:x}.png", key);

        if (std::filesystem::exists(path)) {
            auto sprite = CCSprite::create(path.string().c_str());
            if (sprite) {
                sprite->setScale(std::min(
                    32.f / sprite->getContentSize().width,
                    32.f / sprite->getContentSize().height
                ));
                sprite->setPosition({19.f, -19.f});
                card->addChild(sprite);
            }
            return;
        }

        auto sprite = LazySprite::create({32.f, 32.f}, false);
        if (!sprite) return;
        sprite->setPosition({19.f, -19.f});
        card->addChild(sprite);

        sprite->setLoadCallback([sprite](Result<> result) {
            if (!result) sprite->setVisible(false);
        });
        sprite->loadFromUrl(comment.pfp, LazySprite::Format::PNG, false);
    }

    void showVersionPicker() {
        if (m_state.versions.empty()) return;

        auto popup = createQuickPopup(
            "Select Version", "", "Cancel", nullptr, nullptr
        );
        if (!popup) return;

        auto menu = CCMenu::create();
        menu->setContentSize({150.f, 160.f});
        menu->setLayout(ColumnLayout::create()->setGap(4.f));

        for (auto const& version : m_state.versions) {
            auto item = CCMenuItemExt::createSpriteExtra(
                ButtonSprite::create(
                    version.c_str(),
                    "bigFont.fnt",
                    "GJ_button_01.png",
                    .35f
                ),
                [this, version, popup](auto) {
                    m_state.selectedVersion = version;
                    m_editingCommentID = 0;
                    m_pendingFiles.clear();
                    m_removedAttachments.clear();
                    m_input->setString("");
                    m_attachmentLabel->setString("");
                    popup->removeFromParent();
                    loadSelectedVersion();
                }
            );
            menu->addChild(item);
        }

        popup->m_mainLayer->addChildAtPosition(menu, Anchor::Center);
        popup->show();
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

    auto tabsMenu = popup->getChildByIDRecursive("tabs-menu");
    auto textarea = popup->getChildByIDRecursive("textarea");
    if (!tabsMenu || !textarea) return;
    if (tabsMenu->getChildByID("opengeode-comments-tab")) return;

    auto description = typeinfo_cast<CCMenuItemSpriteExtra*>(
        tabsMenu->getChildByID("description")
    );
    auto changelog = typeinfo_cast<CCMenuItemSpriteExtra*>(
        tabsMenu->getChildByID("changelog")
    );
    if (!description || !changelog) return;

    auto tabSprite = TabButtonSprite::create(
        "Comments", TabBaseColor::Unselected
    );
    if (!tabSprite) return;

    auto oldListener = description->m_pListener;
    auto oldSelector = description->m_pfnSelector;
    auto tabIndex = tabsMenu->getChildrenCount();

    auto callback =
        [modID, oldListener, oldSelector, textarea, tabSprite](CCMenuItemSpriteExtra* sender) {
            auto parent = textarea->getParent();
            if (!parent) return;

            while (auto old = parent->getChildByType<CommentsLayer>(0))
                old->removeFromParent();

            if (sender->getTag() < 2) {
                (oldListener->*oldSelector)(sender);
                textarea->setVisible(true);
            }
            else {
                textarea->setVisible(false);
                parent->addChild(
                    CommentsLayer::create(modID, parent)
                );
            }

            sender->getParent()->setTag(sender->getTag());
        };

    CCMenuItemExt::assignCallback<CCMenuItemSpriteExtra>(
        description, callback
    );
    CCMenuItemExt::assignCallback<CCMenuItemSpriteExtra>(
        changelog, callback
    );

    auto item = CCMenuItemExt::createSpriteExtra(
        tabSprite, callback
    );
    item->m_pListener = description->m_pListener;
    item->setID("opengeode-comments-tab");
    tabsMenu->addChild(item, 0, tabIndex);
    tabsMenu->updateLayout();
}

} // namespace opengeode
