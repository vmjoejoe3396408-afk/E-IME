#pragma once
#include "../sync/include/user_preferences_store.h"
#include <memory>
namespace eime {
void show_user_dictionary_dialog(HINSTANCE instance, HWND owner,
    const std::shared_ptr<UserPreferencesStore>& store,
    const std::vector<WordReading>& composition, const WordReading& preset = {});
}
