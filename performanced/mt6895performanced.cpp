#include <iostream>
#include <fstream>
#include <sys/system_properties.h>
#include <android/log.h>
#include <cstring>
#include <string>
#include <unistd.h>
#include <cerrno>

#define LOG_TAG "mt6895performanced"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

const char* PROP_PERF_MODE = "persist.sys.mt6895_performance_mode";
const char* ON_VALUE = "1";

// CPU cores 0-3 (little A55 cores)
const int CPU_CORES[] = {0, 1, 2, 3};

// Helper to get property value as string
std::string get_property(const char* name) {
    char value[PROP_VALUE_MAX] = {0};
    int len = __system_property_get(name, value);
    if (len <= 0) return "";
    return std::string(value);
}

// Helper to get prop_info and current serial for a property
const prop_info* get_property_info(const char* name, uint32_t* out_serial) {
    const prop_info* pi = __system_property_find(name);
    if (pi) {
        *out_serial = __system_property_serial(pi);
    }
    return pi;
}

// Wait for a specific property to change (returns true if changed, false on timeout)
bool wait_for_property_change(const prop_info* pi, uint32_t old_serial, int timeout_ms = -1) {
    if (!pi) return false;
    struct timespec ts;
    struct timespec* timeout_ptr = nullptr;
    if (timeout_ms >= 0) {
        ts.tv_sec = timeout_ms / 1000;
        ts.tv_nsec = (timeout_ms % 1000) * 1000000;
        timeout_ptr = &ts;
    }
    uint32_t new_serial = old_serial;
    bool success = __system_property_wait(pi, old_serial, &new_serial, timeout_ptr);
    return success;
}

// Helper to set CPU online status
bool set_cpu_online(int cpu, bool online) {
    char path[256];
    snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/online", cpu);

    std::ofstream file(path);
    if (!file) {
        LOGE("Failed to open %s: %s", path, strerror(errno));
        return false;
    }

    file << (online ? "1\n" : "0\n");
    if (!file) {
        LOGE("Failed to write to %s: %s", path, strerror(errno));
        return false;
    }

    LOGI("Set CPU%d to %s", cpu, online ? "online" : "offline");
    return true;
}

// Apply performance mode (ON = offline little cores, OFF = online little cores)
void apply_performance_mode(bool perf_mode) {
    LOGI("Applying performance mode: %s", perf_mode ? "ON (offline E-cores)" : "OFF (online E-cores)");
    for (int cpu : CPU_CORES) {
        set_cpu_online(cpu, !perf_mode);
    }
}


// Main daemon: monitor performance mode property
void run_daemon() {
    uint32_t serial = 0;
    const prop_info* pi = get_property_info(PROP_PERF_MODE, &serial);
    if (!pi) {
        LOGE("Property %s not found, cannot monitor. Exiting.", PROP_PERF_MODE);
        return;
    }

    std::string last_mode = get_property(PROP_PERF_MODE);
    apply_performance_mode(last_mode == ON_VALUE);

    while (true) {
        // Wait for property change
        if (!wait_for_property_change(pi, serial)) {
            // Timeout or error, just re-get serial
            serial = __system_property_serial(pi);
        } else {
            serial = __system_property_serial(pi);
            std::string new_mode = get_property(PROP_PERF_MODE);
            if (new_mode != last_mode) {
                last_mode = new_mode;
                bool perf_mode = (new_mode == ON_VALUE);
                LOGI("Performance mode changed to: %s", perf_mode ? "ON" : "OFF");
                apply_performance_mode(perf_mode);
            }
        }
    }
}

int main() {
    LOGI("Starting mt6895performanced");
    sleep(5);
    // Initial apply
    std::string init_mode = get_property(PROP_PERF_MODE);
    bool init_perf = (init_mode == ON_VALUE);
    LOGI("Initial performance mode: %s", init_perf ? "ON" : "OFF");
    apply_performance_mode(init_perf);

    // Monitor for future changes
    run_daemon();

    return 0;
}
