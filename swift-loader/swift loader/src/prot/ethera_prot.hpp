#pragma once
#include <windows.h>
#include <string>

namespace ethera_prot {

void init();
void specter_check_debug();
void specter_mitigations();
void specter_ntdll_unhook();
void specter_reset_peb();
void specter_remap_sections();
void specter_protect_process();
bool specter_self_delete();
void specter_start_watchdog();
void handle_attack();

} // namespace ethera_prot
