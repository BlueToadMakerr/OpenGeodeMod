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

void CommentsLayer::showAttachmentImage(std::string const& url) {
    if (url.empty()) return;
    if (auto popup = createAttachmentImagePopup(url))
        popup->show();
}

void CommentsLayer::showAttachmentsPopup() {
    std::vector<CommentAttachment> attachments;
    for (auto const& c : m_state.comments) {
        if (c.id == m_editingCommentID) {
            attachments = c.attachments;
            break;
        }
    }

    auto popup = createAttachmentPopup(
        std::move(attachments),
        m_pendingFiles,
        m_removedAttachments,
        [this](int id) { toggleAttachmentRemoval(id); },
        [this](std::filesystem::path const& path) { removePendingFile(path); },
        [this]() { pickAttachments(); }
    );
    if (popup) {
        popup->m_noElasticity = true;
        popup->show();
    }
}

} // namespace opengeode
