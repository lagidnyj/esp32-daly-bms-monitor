#pragma once

#include <Arduino.h>
#include <vector>

#include "BmsTypes.h"

String buildJson(const std::vector<Bms>& bms);
