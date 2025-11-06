#include "ur/capi.h"
#include "ur/plthook.h"
#include <memory>
#include <new>
#include <string>

extern "C" {

struct ur_plthook {
    std::unique_ptr<ur::plthook::Hook> impl;
};

ur_status_t ur_plthook_create_from_base(uintptr_t base, ur_plthook_t** out) {
    if (out == nullptr) return UR_STATUS_INVALID_ARG;
    *out = nullptr;
    ur_plthook_t* h = new (std::nothrow) ur_plthook_t{};
    if (!h) return UR_STATUS_ERROR;
    try {
        h->impl = std::make_unique<ur::plthook::Hook>(base);
    } catch (...) {
        delete h;
        return UR_STATUS_ERROR;
    }
    *out = h;
    return UR_STATUS_OK;
}

ur_status_t ur_plthook_create_from_path(const char* so_path, ur_plthook_t** out) {
    if (out == nullptr || so_path == nullptr) return UR_STATUS_INVALID_ARG;
    *out = nullptr;
    ur_plthook_t* h = new (std::nothrow) ur_plthook_t{};
    if (!h) return UR_STATUS_ERROR;
    try {
        h->impl = std::make_unique<ur::plthook::Hook>(std::string(so_path));
    } catch (...) {
        delete h;
        return UR_STATUS_ERROR;
    }
    *out = h;
    return UR_STATUS_OK;
}

void ur_plthook_destroy(ur_plthook_t* hook) {
    delete hook;
}

int ur_plthook_is_valid(const ur_plthook_t* hook) {
    if (!hook || !hook->impl) return 0;
    return hook->impl->is_valid() ? 1 : 0;
}

ur_status_t ur_plthook_hook_symbol(ur_plthook_t* hook, const char* symbol, void* replacement, void** original_out) {
    if (!hook || !hook->impl || !symbol || !replacement) return UR_STATUS_INVALID_ARG;
    try {
        bool ok = hook->impl->hook_symbol(symbol, replacement, original_out);
        return ok ? UR_STATUS_OK : UR_STATUS_ERROR;
    } catch (...) {
        return UR_STATUS_ERROR;
    }
}

ur_status_t ur_plthook_unhook_symbol(ur_plthook_t* hook, const char* symbol) {
    if (!hook || !hook->impl || !symbol) return UR_STATUS_INVALID_ARG;
    try {
        bool ok = hook->impl->unhook_symbol(symbol);
        return ok ? UR_STATUS_OK : UR_STATUS_ERROR;
    } catch (...) {
        return UR_STATUS_ERROR;
    }
}

} // extern "C"