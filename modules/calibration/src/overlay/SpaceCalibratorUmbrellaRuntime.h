#pragma once

#include "AdbController.h"

void CCal_UmbrellaStart();
void CCal_UmbrellaTick();
void CCal_UmbrellaShutdown();

// Returns the overlay's shared AdbController instance. UI code that needs to
// call wkopenvr::adb::ApplyGuardianPauseSetting or SetGuardianPauseValueOverride
// should obtain the instance here rather than constructing a second one.
AdbController& CCal_GetAdb();
