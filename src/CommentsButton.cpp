#include "CommentsPopup.hpp"
#include "comments/CommentsLayer.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/modify/ModPopup.hpp>

using namespace geode::prelude;

namespace opengeode {

class $modify(OpenGeodeModPopup, ModPopup) {
    void loadTab(ModPopup::Tab tab) {
        clearCommentsTab(this);
        ModPopup::loadTab(tab);
    }
};

$execute {
    ModPopupUIEvent().listen(
        +[](FLAlertLayer* popup, std::string_view, std::optional<Mod*>) {
            if (!popup) return false;
            ensureCommentsTab(popup);
            return false;
        }
    ).leak();
}

} // namespace opengeode
