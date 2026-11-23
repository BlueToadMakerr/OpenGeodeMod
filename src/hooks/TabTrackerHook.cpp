#include "../Settings.hpp"
#include "../CommentsPopup.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/CCMenuItem.hpp>

using namespace geode::prelude;

namespace opengeode {

class $modify(TabTrackerMenuItem, CCMenuItem) {
    void activate() {
        auto id = this->getID();

        // Update the cached tab before Geode handles the click. The native
        // activation can synchronously trigger a reload, so doing this after
        // CCMenuItem::activate() lets that request briefly use the previous
        // tab's filter parameters.
        if (id == "installed-button" ||
            id == "download-button" ||
            id == "recent-button" ||
            id == "featured-button") {
            getCachedActiveTabKey() = id;
        }

        CCMenuItem::activate();

        // Description/Changelog switch the comments section off only once the click is
        // released and activates (like Geode's own tabs), not on press, so cancelling
        // a click no longer leaves the comments tab in a broken state.
        if ((id == "description" || id == "changelog") && this->getParent() &&
            this->getParent()->getID() == "tabs-menu") {
            for (CCNode* node = this->getParent(); node; node = node->getParent()) {
                if (auto popup = typeinfo_cast<FLAlertLayer*>(node)) {
                    clearCommentsTab(popup);
                    break;
                }
            }
        }
    }
};

} // namespace opengeode
