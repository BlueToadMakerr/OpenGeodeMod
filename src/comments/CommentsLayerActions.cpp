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

        auto popup = CommentViewPopup::create(text);
        if (popup) {
            popup->m_noElasticity = true;
            popup->show();
        }
    
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

} // namespace opengeode
