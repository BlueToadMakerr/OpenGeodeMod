#pragma once

#include "../IndexUpdates.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace opengeode {

class UpdateSourcePopup : public Popup {
    std::vector<IndexUpdateInfo> m_options;
    std::function<void(IndexUpdateInfo)> m_callback;

    bool init();

public:
    static UpdateSourcePopup* create(
        std::vector<IndexUpdateInfo> options,
        std::function<void(IndexUpdateInfo)> callback
    );
};

} // namespace opengeode
