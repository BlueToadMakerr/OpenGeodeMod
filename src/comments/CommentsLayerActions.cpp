#include "CommentsLayer.hpp"
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

void CommentsLayer::showComment(std::string const& text) {

        if (auto popup = createCommentViewPopup(text))
            popup->show();
    
}
void CommentsLayer::showVersionPicker() {

        if(m_state.versions.empty())return;
        auto popup = createVersionSelectPopup(
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
void CommentsLayer::showLockPicker() {

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
void CommentsLayer::setLock(std::string value) {

        auto json = matjson::makeObject({
            {"lock", value}
        });

        auto request = web::WebRequest();
        request.header(
            "Authorization",
            "Bearer " + getAuthAccessToken()
        );
        request.bodyJSON(json);

        auto loading = LoadingNotification::create("Updating lock...");

        m_requestTask.spawn(
            request.put(
                trimSlash(getIndexUrl()) +
                fmt::format(
                    "/v1/mods/{}/versions/{}/submission",
                    m_modID,
                    m_state.selectedVersion
                )
            ),
            [this, loading](web::WebResponse response) {
                loading->hide();
                if (!response.ok()) {
                    notifyStatus(errorText(response));
                    return;
                }
                loadSelectedVersion();
            }
        );
    
}
void CommentsLayer::submitComment() {

        if (m_state.selectedVersion.empty()) {
            notifyStatus("Select a version first.");
            return;
        }

        if (!canComment()) {
            notifyStatus("You cannot comment on this submission.");
            return;
        }

        auto text = std::string(m_input->getString().c_str());
        if (text.empty() && m_pendingFiles.empty() && m_editingCommentID == 0) {
            notifyStatus("Write something or attach an image.");
            return;
        }

        if (!hasAuthTokens()) {
            notifyStatus("Log in to comment.");
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
        auto loading = LoadingNotification::create("Posting...");

        m_requestTask.spawn(
            request.post(
                trimSlash(getIndexUrl()) +
                fmt::format(
                    "/v1/mods/{}/versions/{}/submission/comments",
                    m_modID,
                    m_state.selectedVersion
                )
            ),
            [this, loading](web::WebResponse response) {
                loading->hide();
                if (!response.ok()) {
                    m_sendButton->setEnabled(true);
                    notifyStatus(errorText(response));
                    return;
                }

                auto json = response.json().unwrapOr(matjson::Value());
                auto payload = json["payload"].isObject()
                    ? json["payload"]
                    : json;
                auto commentID = intValue(payload, "id");

                if (commentID == 0) {
                    m_sendButton->setEnabled(true);
                    notifyStatus("Comment was created but no ID was returned.");
                    loadSelectedVersion();
                    return;
                }

                finishCommentAttachments(commentID, false);
            }
        );
    
}
void CommentsLayer::updateExistingComment(std::string const& text) {

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
        auto loading = LoadingNotification::create("Saving...");

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
            [this, loading](web::WebResponse response) {
                loading->hide();
                if (!response.ok()) {
                    m_sendButton->setEnabled(true);
                    notifyStatus(errorText(response));
                    return;
                }

                finishCommentAttachments(m_editingCommentID, true);
            }
        );
    
}
void CommentsLayer::beginEdit(CommentData const& comment) {

        if (!comment.canEdit) return;

        m_editingCommentID = comment.id;
        m_removedAttachments.clear();
        m_pendingFiles.clear();
        m_input->setString(comment.body.c_str());
        notifyStatus("Editing comment. Press Send to save.");
        rebuild();
        m_input->focus();
    
}
void CommentsLayer::exitEdit() {

        m_editingCommentID = 0;
        m_removedAttachments.clear();
        m_pendingFiles.clear();
        m_input->setString("");
        rebuild();
    
}
void CommentsLayer::deleteComment(int id) {

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

                auto loading = LoadingNotification::create("Deleting comment...");

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
                    [this, loading](web::WebResponse response) {
                        loading->hide();
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

} // namespace opengeode
