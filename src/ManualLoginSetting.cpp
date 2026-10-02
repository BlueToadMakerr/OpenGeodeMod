#include "ManualLoginPopup.hpp"
#include "Settings.hpp"

#include <Geode/loader/SettingV3.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>

using namespace geode::prelude;

namespace opengeode {

class ManualLoginSettingV3 : public SettingV3 {
public:
    static Result<std::shared_ptr<SettingV3>> parse(std::string const& key, std::string const& modID, matjson::Value const& json) {
        auto res = std::make_shared<ManualLoginSettingV3>();
        auto root = checkJson(json, "ManualLoginSettingV3");
        res->init(key, modID, root);
        res->parseNameAndDescription(root);
        res->parseEnableIf(root);
        root.checkUnknownKeys();
        return root.ok(std::static_pointer_cast<SettingV3>(res));
    }
    bool load(matjson::Value const&) override { return true; }
    bool save(matjson::Value&) const override { return true; }
    bool isDefaultValue() const override { return true; }
    void reset() override {}
    SettingNodeV3* createNode(float width) override;
};

class ManualLoginSettingNodeV3 : public SettingNodeV3 {
    ButtonSprite* m_buttonSprite = nullptr;
    CCMenuItemSpriteExtra* m_button = nullptr;
    bool init(std::shared_ptr<ManualLoginSettingV3> setting, float width) {
        if (!SettingNodeV3::init(setting, width)) return false;
        m_buttonSprite = ButtonSprite::create("Configure Login", "goldFont.fnt", "GJ_button_01.png", .8f);
        m_buttonSprite->setScale(.5f);
        m_button = CCMenuItemSpriteExtra::create(m_buttonSprite, this, menu_selector(ManualLoginSettingNodeV3::onButton));
        getButtonMenu()->addChildAtPosition(m_button, Anchor::Center);
        getButtonMenu()->setContentWidth(75.f);
        getButtonMenu()->updateLayout();
        updateState(nullptr);
        return true;
    }
    void updateState(CCNode* invoker) override {
        SettingNodeV3::updateState(invoker);
        auto enabled = getSetting()->shouldEnable();
        m_button->setEnabled(enabled);
        m_buttonSprite->setCascadeColorEnabled(true);
        m_buttonSprite->setCascadeOpacityEnabled(true);
        m_buttonSprite->setOpacity(enabled ? 255 : 155);
        m_buttonSprite->setColor(enabled ? ccWHITE : ccGRAY);
    }
    void onButton(CCObject*) {
        auto url = getIndexUrl();
        IndexEntry entry;
        for (auto const& candidate : getAllIndexes()) if (candidate.url == url) { entry = candidate; break; }
        if (entry.url.empty()) { entry.id = getActiveIndexId(); entry.name = "Active Index"; entry.url = url; }
        showManualLoginPopup(std::move(entry));
    }
    void onCommit() override {}
    void onResetToDefault() override {}
public:
    static ManualLoginSettingNodeV3* create(std::shared_ptr<ManualLoginSettingV3> setting, float width) {
        auto ret = new ManualLoginSettingNodeV3();
        if (ret->init(std::move(setting), width)) { ret->autorelease(); return ret; }
        delete ret;
        return nullptr;
    }
    bool hasUncommittedChanges() const override { return false; }
    bool hasNonDefaultValue() const override { return false; }
    std::shared_ptr<ManualLoginSettingV3> getSetting() const { return std::static_pointer_cast<ManualLoginSettingV3>(SettingNodeV3::getSetting()); }
};

SettingNodeV3* ManualLoginSettingV3::createNode(float width) {
    return ManualLoginSettingNodeV3::create(std::static_pointer_cast<ManualLoginSettingV3>(shared_from_this()), width);
}

$on_mod(Loaded) {
    (void)Mod::get()->registerCustomSettingType("manual-login", &ManualLoginSettingV3::parse);
}

} // namespace opengeode
