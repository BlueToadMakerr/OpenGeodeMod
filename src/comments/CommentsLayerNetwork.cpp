#include "CommentsLayer.hpp"
#include "../Settings.hpp"
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
    void CommentsLayer::request(std::string method, std::string path, std::function < void(web::WebResponse) > callback) {
        auto request = web::WebRequest();
        auto token = getAuthAccessToken();
        if (!token.empty()) request.header("Authorization", "Bearer " + token);
        m_requestTask.spawn(request.send(method, trimSlash(getIndexUrl()) + path), [callback = std::move(callback)](web::WebResponse response) mutable {
            callback(std::move(response));
        }
        );
    }
    void CommentsLayer::load() {
        setLoading(true, "Loading comments...");
        m_state.loggedIn = hasAuthTokens();
        if (!m_state.loggedIn) {
            loadMod();
            return;
        }
        request("GET", "/v1/me", [this](web::WebResponse response) {
            if (response.ok()) {
                auto json = response.json().unwrapOr(matjson::Value()); auto payload = json["payload"].isObject() ? json["payload"]: json; m_state.currentDeveloperID = intValue(payload,
                "id"); m_state.currentDeveloperAdmin = payload["admin"].asBool().unwrapOr(false); m_state.currentDeveloperVerified = payload["verified"].asBool().unwrapOr(false);
            }
            loadMod();
        }
        );
    }
    void CommentsLayer::loadMod() {
        request("GET", fmt::format("/v1/mods/{}", m_modID), [this](web::WebResponse response) {
            if (!response.ok()) {
                setContentMessage(errorText(response)); return;
            }
            auto json = response.json().unwrapOr(matjson::Value()); auto payload = json["payload"].isObject() ? json["payload"]: json; m_state.versions.clear(); m_state.modDeveloperIDs.clear(); auto developers = payload["developers"]; if (developers.isArray()) {
                for (auto const & developer: developers) {
                    auto id = intValue(developer, "id"); if (id) m_state.modDeveloperIDs.push_back(id);
                }
            }
            m_state.currentDeveloperModDeveloper = std::find(m_state.modDeveloperIDs.begin(), m_state.modDeveloperIDs.end(),
            m_state.currentDeveloperID) != m_state.modDeveloperIDs.end(); auto versions = payload["versions"]; if (versions.isArray()) {
                for (auto const & version: versions) {
                    auto value = stringValue(version, "version"); if (!value.empty()) m_state.versions.push_back(value);
                }
            }
            std::sort(m_state.versions.begin(), m_state.versions.end(), [](std::string const & a, std::string const & b) {
                auto normalize = [](std::string value) {
                    if (value.starts_with("v")) value.erase(0, 1); return value;
                }; auto split = [](std::string const & value) {
                    std::vector < int > parts; size_t start = 0; while (start < value.size()) {
                        auto end = value.find('.', start); auto part = value.substr(start, end == std::string::npos ? std::string::npos: end - start); size_t digits = 0; while (digits < part.size() && std::isdigit(static_cast < unsigned char >(part[digits])))++ digits; parts.push_back(digits ? std::stoi(part.substr(0,
                        digits)): 0); if (end == std::string::npos) break; start = end + 1;
                    }
                    return parts;
                }; auto av = split(normalize(a)); auto bv = split(normalize(b)); auto count = std::max(av.size(), bv.size()); for (size_t i = 0; i < count;++ i) {
                    auto ai = i < av.size() ? av[i]: 0; auto bi = i < bv.size() ? bv[i]: 0; if (ai != bi) return ai > bi;
                }
                return a > b;
            }
            ); if (m_state.selectedVersion.empty() && !m_state.versions.empty()) m_state.selectedVersion = m_state.versions.front(); if (m_state.selectedVersion.empty()) {
                setContentMessage("No submission found for this mod."); return;
            }
            loadSelectedVersion();
        }
        );
    }
    void CommentsLayer::loadSelectedVersion() {
        if (m_state.selectedVersion.empty()) return;
        setLoading(true, "Loading submission...");
        request("GET", fmt::format("/v1/mods/{}/versions/{}/submission", m_modID, m_state.selectedVersion), [this](web::WebResponse response) {
            if (!response.ok()) {
                m_state.comments.clear(); m_state.lock = "none"; m_state.lockedBy = 0; setContentMessage("This version does not have a submission."); return;
            }
            auto json = response.json().unwrapOr(matjson::Value()); auto payload = json["payload"].isObject() ? json["payload"]: json; m_state.lock = stringValue(payload,
            "lock", "none"); auto lockedBy = payload["locked_by"]; m_state.lockedBy = lockedBy.isObject() ? intValue(lockedBy,
            "id"): 0; m_state.lockedByName = lockedBy.isObject() ? stringValue(lockedBy, "username", "Unknown"): ""; setLoading(true,
            "Loading comments..."); request("GET", fmt::format("/v1/mods/{}/versions/{}/submission/comments", m_modID, m_state.selectedVersion),
            [this](web::WebResponse commentsResponse) {
                if (!commentsResponse.ok()) {
                    m_state.comments.clear(); setContentMessage(errorText(commentsResponse)); return;
                }
                parseComments(commentsResponse.json().unwrapOr(matjson::Value())); rebuild();
            }
            );
        }
        );
    }
    void CommentsLayer::parseComments(matjson::Value const & json) {
        auto payload = json["payload"].isObject() ? json["payload"]: json;
        auto data = payload["data"];
        m_state.comments.clear();
        if (!data.isArray()) return;
        for (auto const & raw: data) {
            CommentData comment;
            comment.id = intValue(raw, "id");
            comment.body = stringValue(raw, "comment");
            auto author = raw["author"];
            if (author.isObject()) {
                comment.authorID = intValue(author, "id");
                comment.username = stringValue(author, "username", "Unknown");
                auto githubID = intValue(author, "github_id");
                if (githubID > 0)
                    comment.pfp = fmt::format("https://avatars.githubusercontent.com/u/{}?v=4", githubID);
            }
            comment.canEdit = m_state.loggedIn && (comment.authorID == m_state.currentDeveloperID || m_state.currentDeveloperAdmin);
            comment.canDelete = comment.canEdit;
            auto attachments = raw["attachments"];
            if (attachments.isArray()) {
                for (auto const & attachment: attachments) {
                    CommentAttachment item;
                    item.id = intValue(attachment, "id");
                    item.url = stringValue(attachment, "url");
                    item.filename = stringValue(attachment, "filename");
                    if (item.filename.empty() && !item.url.empty()) {
                        auto slash = item.url.find_last_of('/');
                        item.filename = slash == std::string::npos ? item.url: item.url.substr(slash + 1);
                    }
                    if (item.id && !item.url.empty()) comment.attachments.push_back(std::move(item));
                }
            }
            m_state.comments.push_back(std::move(comment));
        }
        std::sort(m_state.comments.begin(), m_state.comments.end(), [](CommentData const & a, CommentData const & b) {
            return a.id > b.id;
        }
        );
    }
    bool CommentsLayer::canComment() const {
        return m_state.loggedIn && (m_state.lock == "none" || m_state.currentDeveloperAdmin);
    }
}
// namespace opengeode
