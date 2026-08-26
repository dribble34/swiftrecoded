#pragma once
#include <windows.h>
#include <string>

namespace webhook_report {

void write_debug_log(const std::string& msg);
void report_incident_and_die(const std::string& reason);

} // namespace webhook_report
