#include "Settings.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>

using namespace geode::prelude;

namespace opengeode {

Notification* g_switchNotif = nullptr;

class $modify(IndexSwitcherMenuLayer, MenuLayer) {
    struct Fields {
        bool m_isSwitching = false;
        int m_attempts = 0;
    };

    void onExit() {
        this->unschedule(schedule_selector(IndexSwitcherMenuLayer::tryOpenMods));
        MenuLayer::onExit();
    }

    void tryOpenMods(float) {
        if (!m_fields->m_isSwitching) return;

        m_fields->m_attempts++;

        if (m_fields->m_attempts > 20) {
            m_fields->m_isSwitching = false;

            if (g_switchNotif) {
                g_switchNotif->cancel();
                g_switchNotif = nullptr;
            }

            if (auto overlay = getChildByID("switch-overlay"_spr)) {
                overlay->removeFromParent();
            }

            this->unschedule(schedule_selector(IndexSwitcherMenuLayer::tryOpenMods));
            return;
        }

        auto director = CCDirector::get();
        bool dontCallWillSwitch = director->getDontCallWillSwitch();
        director->setDontCallWillSwitch(true);
        geode::openModsList();
        director->setDontCallWillSwitch(dontCallWillSwitch);
    }

    bool init() {
        if (!MenuLayer::init()) return false;

        if (g_shouldReopenModsList) {
            g_shouldReopenModsList = false;
            m_fields->m_isSwitching = true;
            m_fields->m_attempts = 0;

            auto winSize = CCDirector::get()->getWinSize();
            auto overlay = CCLayerColor::create(ccc4(0, 0, 0, 255), winSize.width, winSize.height);
            overlay->setID("switch-overlay"_spr);
            this->addChild(overlay, 9999);

            std::string indexName = "Geode Index API";
            for (auto const& entry : getAllIndexes()) {
                if (entry.url == getIndexUrl()) {
                    indexName = entry.name;
                    break;
                }
            }

            if (g_switchNotif) {
                g_switchNotif->cancel();
            }
            g_switchNotif = Notification::create(
                fmt::format("Switching to {}...", indexName),
                NotificationIcon::Loading,
                0.0f
            );
            g_switchNotif->show();

            this->schedule(schedule_selector(IndexSwitcherMenuLayer::tryOpenMods), 0.1f);
        }

        return true;
    }
};

} // namespace opengeode
