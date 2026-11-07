#pragma once
#include "Gcode.h"

void webBegin(Gcode& gcode);
void webService();

static String pendingLog;
static String jsonEscape(const String& input);