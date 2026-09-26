#pragma once

#include "CommentsTypes.hpp"

#include <Geode/ui/Popup.hpp>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace opengeode {

Popup* createCommentViewPopup(std::string text);

Popup* createVersionSelectPopup(
    std::vector<std::string> versions,
    std::function<void(std::string)> callback
);

Popup* createAttachmentPopup(
    std::vector<CommentAttachment> attachments,
    std::vector<std::filesystem::path> pending,
    std::vector<int> removed,
    std::function<void(int)> onToggleDelete,
    std::function<void(std::filesystem::path const&)> onRemovePending,
    std::function<void()> onAdd
);

Popup* createAttachmentImagePopup(std::string url);

} // namespace opengeode
