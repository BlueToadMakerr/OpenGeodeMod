#include "CommentsPopup.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/Event.hpp>

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
