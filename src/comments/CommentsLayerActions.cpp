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

void CommentsLayer::showComment(std::string const& text) {
    auto popup = createCommentViewPopup(text);
    if (popup) {
        popup->m_noElasticity = true;
        popup->show();
    }
}

void CommentsLayer::showVersionPicker() {
    if (m_state.versions.empty()) return;
    auto popup = createVersionSelectPopup(m_state.versions, [this](std::string version) {
        if (version == m_state.selectedVersion) return;
        m_state.selectedVersion = std::move(version);
        m_editingCommentID = 0;
        m_pendingFiles.clear();
        m_removedAttachments.clear();
        m_input->setString("");
        loadSelectedVersion();
    });
    if (popup) {
        popup->m_noElasticity = true;
        popup->show();
    }
}

} // namespace opengeode
