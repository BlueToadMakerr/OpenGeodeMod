#pragma once

#include "../IndexUpdates.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace opengeode {

class UpdateSourcePopup : public Popup {
    std::vector<IndexUpdateInfo> m_options;
    std::function<void(IndexUpdateInfo)> m_callback;
    std::function<void()> m_closeCallback;
    std::function<void()> m_alwaysInstalledCallback;
    bool m_selected = false;

    bool init();
    void onClose(CCObject* sender) override;

public:
    static UpdateSourcePopup* create(
        std::vector<IndexUpdateInfo> options,
        std::function<void(IndexUpdateInfo)> callback,
        std::function<void()> closeCallback = {},
        std::function<void()> alwaysInstalledCallback = {}
    );
};

} // namespace opengeode
