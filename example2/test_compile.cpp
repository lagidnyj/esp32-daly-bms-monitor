// Simple compile test
#include "BmsConfig.h"
#include "BmsTypes.h"

int main() {
    // Test 1: Config with all fields
    static const struct BmsDeviceConfig test1 = {"02:11:22:33:44:55", "Battery-1", "4455"};
    
    // Test 2: Config with auto-generated ID
    static const struct BmsDeviceConfig test2 = {"02:AA:BB:CC:DD:EE", "Battery-2", nullptr};
    
    // Test 3: Config with auto-generated name
    static const struct BmsDeviceConfig test3 = {"02:12:34:56:78:9A", nullptr, "789A"};
    
    // Test 4: Auto-scan mode (empty array)
    static const struct BmsDeviceConfig test4 = {nullptr, nullptr, nullptr};
    
    return 0;
}
