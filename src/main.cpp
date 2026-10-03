#include "Settings.hpp"
#include "InstalledMods.hpp"
#include "hooks/WebRequestHook.hpp"
#include <Geode/Geode.hpp>
using namespace geode::prelude;
namespace opengeode {
    $ on_mod(Loaded) {
        restoreFromBackupIfNeeded();
        migrateLegacyTextSaveData();
        ensurePresetsExist();
        clearPendingModUpdates();
        registerWebRequestHook();
    }
    $ on_mod(DataSaved) {
        backupSaveData();
    }
}
// namespace opengeode
