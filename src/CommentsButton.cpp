#include "CommentsPopup.hpp"
#include "comments/CommentsLayer.hpp"
#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>
using namespace geode::prelude;
namespace opengeode {
    namespace {
        class CommentsTabWatcher: public CCNode {
            CCNode * m_popup = nullptr;
            bool init(CCNode * popup) {
                if (!CCNode::init()) return false;
                m_popup = popup;
                scheduleUpdate();
                return true;
            }
            void finishTabClick(float) {
                if (!m_popup)
                    return;
                auto tabs = m_popup->getChildByIDRecursive("tabs-menu");
                auto comments = tabs ? typeinfo_cast < CCMenuItemSpriteExtra * >(tabs->getChildByID("opengeode-comments-tab")): nullptr;
                auto descriptionTab = tabs ? typeinfo_cast < CCMenuItemSpriteExtra * >(tabs->getChildByID("description")): nullptr;
                auto changelogTab = tabs ? typeinfo_cast < CCMenuItemSpriteExtra * >(tabs->getChildByID("changelog")): nullptr;
                if (!comments)
                    return;
                if ((descriptionTab && descriptionTab->isSelected()) || (changelogTab && changelogTab->isSelected())) clearCommentsTab(m_popup);
            }
            void update(float) override {
                if (!m_popup)
                    return;
                auto tabs = m_popup->getChildByIDRecursive("tabs-menu");
                auto comments = tabs ? typeinfo_cast < CCMenuItemSpriteExtra * >(tabs->getChildByID("opengeode-comments-tab")): nullptr;
                if (!comments)
                    return;
                auto descriptionTab = typeinfo_cast < CCMenuItemSpriteExtra * >(tabs->getChildByID("description"));
                auto changelogTab = typeinfo_cast < CCMenuItemSpriteExtra * >(tabs->getChildByID("changelog"));
                // Geode marks a tab selected as soon as the pointer goes down. Delay
                // the check until the click has had time to finish so the comments
                // tab does not disappear on mouse-down.
                if ((descriptionTab && descriptionTab->isSelected()) || (changelogTab && changelogTab->isSelected())) {
                    scheduleOnce(schedule_selector(CommentsTabWatcher::finishTabClick), 0.08f);
                }
            }
            public: static CommentsTabWatcher * create(CCNode * popup) {
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
    $ execute {
        ModPopupUIEvent().listen( + [](FLAlertLayer * popup, std::string_view, std::optional < Mod * >) {
            if (!popup) return false; ensureCommentsTab(popup); if (!popup->getChildByIDRecursive("opengeode-comments-watcher")) {
                if (auto watcher = CommentsTabWatcher::create(popup)) {
                    watcher->setID("opengeode-comments-watcher"); popup->addChild(watcher);
                }
            }
            return false;
        }
        ).leak();
    }
}
// namespace opengeode
