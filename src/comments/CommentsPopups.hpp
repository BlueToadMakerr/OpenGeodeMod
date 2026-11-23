#pragma once

#include "CommentsTypes.hpp"

#include <Geode/ui/Popup.hpp>

#include <asp/fs/fs.hpp>
#include <functional>
#include <string>
#include <vector>

namespace opengeode {

geode::Popup* createCommentViewPopup(std::string text);

geode::Popup* createVersionSelectPopup(
    std::vector<std::string> versions,
    std::string selectedVersion,
    std::function<void(std::string)> callback
);

geode::Popup* createAttachmentPopup(
    std::vector<CommentAttachment> attachments,
    std::vector<asp::fs::Path> pending,
    std::vector<int> removed,
    std::function<void(int)> onToggleDelete,
    std::function<void(asp::fs::Path const&)> onRemovePending,
    std::function<void()> onAdd
);

geode::Popup* createAttachmentImagePopup(std::string url);

} // namespace opengeode
