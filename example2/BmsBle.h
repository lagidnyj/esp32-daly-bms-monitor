#pragma once

#include <vector>

#include "BmsTypes.h"

std::vector<Bms>& bmsDevices();
void doScan();
void buildTargets();
void scanAndUpdateTargets();
bool pollOne(Bms& bms);
