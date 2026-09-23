#include "CommentsPopup.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>

using namespace geode::prelude;

namespace opengeode {

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
