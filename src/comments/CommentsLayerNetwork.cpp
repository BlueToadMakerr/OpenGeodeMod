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

void CommentsLayer::request(
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
void CommentsLayer::load() {

        m_state.loggedIn = hasAuthTokens();
        auto loading = LoadingNotification::create("Loading comments...");

        if (!m_state.loggedIn) {
            loading->hide();
            loadMod();
            return;
        }

        request("GET", "/v1/me", [this, loading](web::WebResponse response) {
            loading->hide();
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
void CommentsLayer::loadMod() {

        request("GET", fmt::format("/v1/mods/{}", m_modID), [this](web::WebResponse response) {
            if (!response.ok()) {
                notifyStatus(errorText(response));
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
void CommentsLayer::loadSelectedVersion() {

        if (m_state.selectedVersion.empty()) return;

        auto loading = LoadingNotification::create("Loading submission...");

        request(
            "GET",
            fmt::format(
                "/v1/mods/{}/versions/{}/submission",
                m_modID,
                m_state.selectedVersion
            ),
            [this, loading](web::WebResponse response) {
                loading->hide();
                if (!response.ok()) {
                    m_state.comments.clear();
                    m_state.lock = "none";
                    m_state.lockedBy = 0;
                    rebuild();
                    notifyStatus("This version does not have a submission.");
                    return;
                }

                auto json = response.json().unwrapOr(matjson::Value());
                auto payload = json["payload"].isObject() ? json["payload"] : json;

                m_state.lock = stringValue(payload, "lock", "none");
                auto lockedBy = payload["locked_by"];
                m_state.lockedBy = lockedBy.isObject() ? intValue(lockedBy, "id") : 0;
                m_state.lockedByName = lockedBy.isObject() ? stringValue(lockedBy, "username", "Unknown") : "";

                auto commentsLoading = LoadingNotification::create("Loading comments...");

                request(
                    "GET",
                    fmt::format(
                        "/v1/mods/{}/versions/{}/submission/comments",
                        m_modID,
                        m_state.selectedVersion
                    ),
                    [this, commentsLoading](web::WebResponse commentsResponse) {
                        commentsLoading->hide();
                        if (!commentsResponse.ok()) {
                            m_state.comments.clear();
                            rebuild();
                            notifyStatus(errorText(commentsResponse));
                            return;
                        }

                        parseComments(commentsResponse.json().unwrapOr(matjson::Value()));
                        rebuild();
                    }
                );
            }
        );
    
}
void CommentsLayer::parseComments(matjson::Value const& json) {

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
bool CommentsLayer::canComment() const {

        return m_state.loggedIn &&
            (m_state.lock == "none" || m_state.currentDeveloperAdmin);
    
}
bool CommentsLayer::canUploadAttachments() const {

        return m_state.loggedIn &&
            (m_state.currentDeveloperVerified ||
             m_state.currentDeveloperAdmin ||
             m_state.currentDeveloperModDeveloper);
    
}

} // namespace opengeode
