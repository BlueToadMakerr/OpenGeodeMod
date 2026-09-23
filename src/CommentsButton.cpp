#include "CommentsPopup.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace opengeode {

$execute {
    new EventListener<EventFilter<ModPopupUIEvent>>(
        +[](ModPopupUIEvent* event) {
            auto popup = event->getPopup();
            if (!popup) return ListenerResult::Propagate;
            ensureCommentsTab(popup);
            return ListenerResult::Propagate;
        }
    );
}

} // namespace opengeode
