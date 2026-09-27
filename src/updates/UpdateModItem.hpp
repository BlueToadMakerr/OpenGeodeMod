#pragma once

#include "../IndexUpdates.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace opengeode {

class UpdateModItem : public CCNode {
    IndexUpdateInfo m_update;
    std::vector<IndexUpdateInfo> m_sources;
    Mod* m_mod = nullptr;
    CCNode* m_description = nullptr;
    CCMenuItemSpriteExtra* m_updateButton = nullptr;
    Slider* m_progress = nullptr;
    CCNode* m_tags = nullptr;
    bool m_updated = false;

    bool init(IndexUpdateInfo update, std::vector<IndexUpdateInfo> sources);
    void onView(CCObject*);
    void onUpdate(CCObject*);
    void showSourcePicker();
    void startUpdate(IndexUpdateInfo update);
    void finishUpdate(IndexUpdateInfo const& update, bool success);
    void setProgress(float progress);
    void refreshStatusTags(bool restartRequired);

public:
    static UpdateModItem* create(IndexUpdateInfo update, std::vector<IndexUpdateInfo> sources);\n    static UpdateModItem* createProgressTest();
};

} // namespace opengeode
