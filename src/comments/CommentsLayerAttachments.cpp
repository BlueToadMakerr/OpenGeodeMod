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

void CommentsLayer::showAttachmentImage(std::string const& url) {

        if (url.empty()) return;
        if (auto popup = AttachmentImagePopup::create(url))
            popup->show();
    
}
void CommentsLayer::showAttachmentsPopup() {

        std::vector<CommentAttachment> attachments;
        for(auto const& c:m_state.comments)if(c.id==m_editingCommentID){attachments=c.attachments;break;}
        auto popup = createAttachmentPopup(
            std::move(attachments),
            m_pendingFiles,
            m_removedAttachments,
            [this](int id) { toggleAttachmentRemoval(id); },
            [this](std::filesystem::path const& path) { removePendingFile(path); },
            [this]() { pickAttachments(); }
        );
        if(popup){popup->m_noElasticity=true;popup->show();}
    
}
void CommentsLayer::pickAttachments() {

        if (!canUploadAttachments()) {
            notifyStatus("Attachments require a verified developer, mod developer, or admin.");
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
void CommentsLayer::finishCommentAttachments(int commentID, bool editing) {

        auto finish = [this, editing]() {
            m_pendingFiles.clear();
            m_removedAttachments.clear();
            m_editingCommentID = 0;
            m_input->setString("");
            m_sendButton->setEnabled(true);
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
void CommentsLayer::uploadAttachments(int commentID, std::function<void()> finish) {

        if (!canUploadAttachments()) {
            m_pendingFiles.clear();
            notifyStatus("Comment saved, but you cannot upload attachments.");
            finish();
            return;
        }

        web::MultipartForm form;
        for (auto const& path : m_pendingFiles) {
            auto result = form.file("image", path);
            if (!result) {
                m_sendButton->setEnabled(true);
                notifyStatus("Could not read an attachment.");
                return;
            }
        }

        auto request = web::WebRequest();
        request.header(
            "Authorization",
            "Bearer " + getAuthAccessToken()
        );
        request.bodyMultipart(form);

        auto loading = LoadingNotification::create("Uploading attachments...");

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
            [this, finish = std::move(finish), loading](web::WebResponse response) mutable {
                loading->hide();
                if (!response.ok()) {
                    m_sendButton->setEnabled(true);
                    notifyStatus(fmt::format(
                        "Comment saved, attachment upload failed: {}",
                        errorText(response)
                    ));
                    return;
                }
                finish();
            }
        );
    
}
void CommentsLayer::removeNextAttachment(int commentID, std::function<void()> finish) {

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
                    notifyStatus(errorText(response));
                    return;
                }
                removeNextAttachment(commentID, std::move(finish));
            }
        );
    
}
void CommentsLayer::toggleAttachmentRemoval(int attachmentID) {

        if (m_editingCommentID == 0) return;

        auto it = std::find(
            m_removedAttachments.begin(),
            m_removedAttachments.end(),
            attachmentID
        );

        if (it == m_removedAttachments.end()) {
            m_removedAttachments.push_back(attachmentID);
            notifyStatus("Attachment marked for removal.");
        }
        else {
            m_removedAttachments.erase(it);
            notifyStatus("Attachment restored.");
        }
        rebuild();
    
}
void CommentsLayer::removePendingFile(std::filesystem::path const& path) {

        auto it = std::find(m_pendingFiles.begin(), m_pendingFiles.end(), path);
        if (it == m_pendingFiles.end()) return;

        m_pendingFiles.erase(it);
        notifyStatus("Attachment removed.");
        rebuild();
    
}

} // namespace opengeode
