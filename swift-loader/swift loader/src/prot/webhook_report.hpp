#pragma once
#include <windows.h>
#include <string>

namespace webhook_report {

void report_incident_and_die(const std::string& reason);

} // namespace webhook_report
