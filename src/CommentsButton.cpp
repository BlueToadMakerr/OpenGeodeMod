#include "CommentsPopup.hpp"
#include "comments/CommentsLayer.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>

using namespace geode::prelude;

namespace opengeode {

namespace {

class CommentsTabWatcher : public CCNode {
    CCNode* m_popup = nullptr;

    bool init(CCNode* popup) {
        if (!CCNode::init()) return false;
        m_popup = popup;
        scheduleUpdate();
        return true;
    }

    void update(float) override {
        if (!m_popup) return;

        auto tabs = m_popup->getChildByIDRecursive("tabs-menu");
        auto comments = tabs ? typeinfo_cast<CCMenuItemSpriteExtra*>(
            tabs->getChildByID("opengeode-comments-tab")
        ) : nullptr;
        if (!comments) return;

        auto layer = m_popup->getChildByIDRecursive("opengeode-comments-layer");
        if (!layer)
            clearCommentsTab(m_popup);
    }

public:
    static CommentsTabWatcher* create(CCNode* popup) {
        auto ret = new CommentsTabWatcher();
        if (ret && ret->init(popup)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

}

$execute {
    ModPopupUIEvent().listen(
        +[](FLAlertLayer* popup, std::string_view, std::optional<Mod*>) {
            if (!popup) return false;
            ensureCommentsTab(popup);
            if (!popup->getChildByIDRecursive("opengeode-comments-watcher")) {
                if (auto watcher = CommentsTabWatcher::create(popup)) {
                    watcher->setID("opengeode-comments-watcher");
                    popup->addChild(watcher);
                }
            }
            return false;
        }
    ).leak();
}

} // namespace opengeode
