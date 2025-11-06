#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ur_status_t {
    UR_STATUS_OK = 0,
    UR_STATUS_ERROR = -1,
    UR_STATUS_INVALID_ARG = -2
} ur_status_t;

/* Memory */
int ur_memory_read(uintptr_t address, void* buffer, size_t size);
int ur_memory_write(uintptr_t address, const void* buffer, size_t size);
int ur_memory_protect(uintptr_t address, size_t size, int prot);
void ur_memory_flush_icache(uintptr_t address, size_t size);
int ur_memory_atomic_patch(uintptr_t address, const uint8_t* patch_code, size_t patch_size);
int ur_memory_find_mapped_region(uintptr_t address,
                                  uintptr_t* start,
                                  uintptr_t* end,
                                  uintptr_t* offset,
                                  char* perms_buf, size_t perms_bufsz,
                                  char* path_buf, size_t path_bufsz);

/* Inline Hook */
typedef struct ur_inline_hook ur_inline_hook_t;

ur_status_t ur_inline_hook_create(uintptr_t target, void* callback, int enable_now, ur_inline_hook_t** out);
void ur_inline_hook_destroy(ur_inline_hook_t* hook);
int ur_inline_hook_is_valid(const ur_inline_hook_t* hook);
ur_status_t ur_inline_hook_enable(ur_inline_hook_t* hook);
ur_status_t ur_inline_hook_disable(ur_inline_hook_t* hook);
ur_status_t ur_inline_hook_unhook(ur_inline_hook_t* hook);
void* ur_inline_hook_get_trampoline(const ur_inline_hook_t* hook);
ur_status_t ur_inline_hook_set_detour(ur_inline_hook_t* hook, void* callback);

/* Mid Hook */
typedef struct ur_mid_hook ur_mid_hook_t;

typedef struct ur_cpu_context {
    uint64_t gpr[32];
} ur_cpu_context_t;
typedef void (*ur_mid_hook_callback_t)(ur_cpu_context_t* context);

ur_status_t ur_mid_hook_create(uintptr_t target, ur_mid_hook_callback_t callback, ur_mid_hook_t** out);
void ur_mid_hook_destroy(ur_mid_hook_t* hook);
int ur_mid_hook_is_valid(const ur_mid_hook_t* hook);
ur_status_t ur_mid_hook_enable(ur_mid_hook_t* hook);
ur_status_t ur_mid_hook_disable(ur_mid_hook_t* hook);
ur_status_t ur_mid_hook_unhook(ur_mid_hook_t* hook);

/* VMT Hook */
typedef struct ur_vmt_hook ur_vmt_hook_t;
typedef struct ur_vm_hook ur_vm_hook_t;

ur_status_t ur_vmt_hook_create_from_instance(void* instance, ur_vmt_hook_t** out);
ur_status_t ur_vmt_hook_create_from_vmt(void** vmt_address, ur_vmt_hook_t** out);
void ur_vmt_hook_destroy(ur_vmt_hook_t* vmt);

ur_status_t ur_vmt_hook_hook_method(ur_vmt_hook_t* vmt, size_t index, void* hook_function, ur_vm_hook_t** out_vm_hook);

void ur_vm_hook_destroy(ur_vm_hook_t* vm);
ur_status_t ur_vm_hook_enable(ur_vm_hook_t* vm);
ur_status_t ur_vm_hook_disable(ur_vm_hook_t* vm);
ur_status_t ur_vm_hook_unhook(ur_vm_hook_t* vm);
void* ur_vm_hook_get_original(const ur_vm_hook_t* vm);

/* PLT Hook */
typedef struct ur_plthook ur_plthook_t;

ur_status_t ur_plthook_create_from_base(uintptr_t base, ur_plthook_t** out);
ur_status_t ur_plthook_create_from_path(const char* so_path, ur_plthook_t** out);
void ur_plthook_destroy(ur_plthook_t* hook);
int ur_plthook_is_valid(const ur_plthook_t* hook);
ur_status_t ur_plthook_hook_symbol(ur_plthook_t* hook, const char* symbol, void* replacement, void** original_out);
ur_status_t ur_plthook_unhook_symbol(ur_plthook_t* hook, const char* symbol);
#ifdef __cplusplus
}
#endif