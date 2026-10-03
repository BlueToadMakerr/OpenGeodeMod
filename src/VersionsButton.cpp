#include "VersionsPopup.hpp"
#include "ManagementButtonUtils.hpp"
#include "PopupSectionUtils.hpp"
#include <Geode/Geode.hpp>
#include <Geode/ui/IconButtonSprite.hpp>
using namespace geode::prelude;
namespace opengeode {
    namespace {
        std::string getPopupModID(CCNode * popup) {
            auto label = typeinfo_cast < CCLabelBMFont * >(popup->getChildByIDRecursive("mod-id-label"));
            if (!label)
                return "";
            std::string value = label->getString();
            constexpr char const * prefix = "(ID: ";
            if (!value.starts_with(prefix) || value.size() <= 5) return "";
            value.erase(0, 5);
            if (!value.empty() && value.back() == ')') value.pop_back();
            return value;
        }
        IconButtonSprite * createVersionsButtonSprite() {
            auto icon = CCSprite::createWithSpriteFrameName("GJ_timeIcon_001.png");
            if (!icon)
                return nullptr;
            auto button = IconButtonSprite::create(getButtonTexture("GJ_button_01.png"), icon, "Versions", "bigFont.fnt");
            if (button)
                button->setScale(.5f);
            return button;
        }
    }
    void ensureVersionsButton(CCNode * popup) {
        if (!popup)
            return;
        auto modID = getPopupModID(popup);
        if (modID.empty()) return;
        auto sprite = createVersionsButtonSprite();
        if (!sprite)
            return;
        addManagementButton(popup, "opengeode-versions-button", sprite, [modID, popup](CCMenuItemSpriteExtra *) {
            showVersionsPopup(modID, popup);
        }
        );
    }
}
