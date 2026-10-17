/* Included by driver.c; FatFs is linked into this ELF, not into the host HAL. */
#include "fatfs/ff.h"
#include "fatfs/diskio.h"

/* External owner-task guard: the provider owns token lifecycle and failure
 * retention. Hooks return bool; false rejects admission or poisons release.
 * This mode never creates a synthetic OS identity or a provider-BSS atomic. */
#if defined(STORAGE_VOLUME_EXTERNAL_GUARD) && defined(STORAGE_VOLUME_OS_CPU_MUTEX)
#error "Choose exactly one storage synchronization backend"
#endif
#if defined(STORAGE_VOLUME_EXTERNAL_GUARD) && (!defined(STORAGE_VOLUME_GUARD_ENTER) || !defined(STORAGE_VOLUME_GUARD_LEAVE))
#error "External storage guard requires enter and leave hooks"
#endif
#if defined(STORAGE_VOLUME_OS_CPU_MUTEX) || defined(STORAGE_VOLUME_EXTERNAL_GUARD)
#define STORAGE_VOLUME_SERIALIZED
#endif
#if defined(STORAGE_VOLUME_COMMIT_POWER_DOWN) || defined(STORAGE_VOLUME_TRY_COMMIT_POWER_DOWN)
#define STORAGE_VOLUME_HAS_POWER_COMMIT
#endif
#ifdef STORAGE_VOLUME_TRY_RESUME_POWER_DOWN
#if !defined(STORAGE_VOLUME_TRY_COMMIT_POWER_DOWN) || !defined(STORAGE_VOLUME_SLEEP_UNSAFE)
#error "Sleep recovery requires checked commit, resume, and custody hooks"
#endif
#define STORAGE_VOLUME_HAS_SLEEP_RECOVERY
#endif
#if defined(STORAGE_VOLUME_EXTERNAL_GUARD)
#define STORAGE_VOLUME_SET_PREPARED(value) (power_down_prepared = (value))
#else
#define STORAGE_VOLUME_SET_PREPARED(value) __atomic_store_n(&power_down_prepared, (value), __ATOMIC_RELEASE)
#endif

#define FILE_SLOTS 12u
#define DIR_SLOTS 8u
#define OP_BUDGET_MS 15000u
#define OP_SECTOR_LIMIT 2048u
#ifndef STORAGE_VOLUME_OPERATION_BUDGET_MS
#define STORAGE_VOLUME_OPERATION_BUDGET_MS OP_BUDGET_MS
#endif
#ifndef STORAGE_VOLUME_OPERATION_SECTOR_LIMIT
#define STORAGE_VOLUME_OPERATION_SECTOR_LIMIT OP_SECTOR_LIMIT
#endif
static FATFS filesystem;
static uint64_t operation_start;
static uint32_t operation_steps, operation_sectors;
#ifdef STORAGE_VOLUME_OS_CPU_MUTEX
/* The X4 loader may place BSS in PSRAM. Reuse the privileged OS/CPU ABI1
 * mutex so no raw Xtensa compare-and-set can target this storage. */
static x4_cpu_mutex operation_mutex;
static x4_cpu_task operation_owner;
static bool mutex_poisoned, quiescing, quiesced;
static bool valid_task(void) { return !xPortInIsrContext() && xTaskGetCurrentTaskHandle() != 0; }
#elif !defined(STORAGE_VOLUME_EXTERNAL_GUARD)
static bool operation_busy;
#endif
static bool power_down_prepared;
#ifdef STORAGE_VOLUME_HAS_POWER_COMMIT
static bool power_down_committed;
#endif
#ifdef STORAGE_VOLUME_HAS_SLEEP_RECOVERY
enum { SLEEP_ACTIVE, SLEEP_PREPARED, SLEEP_COMMITTED, SLEEP_RETAINED };
static unsigned sleep_state;
#endif
static uint32_t next_generation = 1;
typedef struct {
    FIL object;
    uint32_t handle, flags, error;
    bool abortable;
    char path[RISC_STORAGE_VOLUME_PATH_MAX];
} file_slot;
typedef struct { DIR object; uint32_t handle, error; char path[RISC_STORAGE_VOLUME_PATH_MAX]; } dir_slot;
static file_slot files[FILE_SLOTS];
static dir_slot dirs[DIR_SLOTS];
static bool has_handles(void) {
    for (unsigned i = 0; i < FILE_SLOTS; ++i) if (files[i].handle) return true;
    for (unsigned i = 0; i < DIR_SLOTS; ++i) if (dirs[i].handle) return true;
    return false;
}
static bool enter_lifecycle(void) {
#ifdef STORAGE_VOLUME_EXTERNAL_GUARD
    if (!STORAGE_VOLUME_GUARD_ENTER()) return false;
#elif defined(STORAGE_VOLUME_OS_CPU_MUTEX)
    if (!valid_task() || !operation_mutex || __atomic_load_n(&mutex_poisoned, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&quiescing, __ATOMIC_ACQUIRE)) return false;
    /* Zero wait, nonrecursive. Reject contenders before state or hardware. */
    if (xQueueSemaphoreTake(operation_mutex, 0) != 1) return false;
    if (__atomic_load_n(&mutex_poisoned, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&quiescing, __ATOMIC_ACQUIRE)) {
        if (xQueueGenericSend(operation_mutex, 0, 0, 0) != 1)
            __atomic_store_n(&mutex_poisoned, true, __ATOMIC_RELEASE);
        return false;
    }
    operation_owner = xTaskGetCurrentTaskHandle();
#else
    if (__atomic_test_and_set(&operation_busy, __ATOMIC_ACQUIRE)) return false;
#endif
    operation_start = clock_api ? clock_api->monotonic_ms(clock_api->context) : 0;
    operation_steps = operation_sectors = 0;
    return true;
}
static bool leave(void) {
#ifdef STORAGE_VOLUME_EXTERNAL_GUARD
    return STORAGE_VOLUME_GUARD_LEAVE();
#elif defined(STORAGE_VOLUME_OS_CPU_MUTEX)
    if (!valid_task() || operation_owner != xTaskGetCurrentTaskHandle()) {
        __atomic_store_n(&mutex_poisoned, true, __ATOMIC_RELEASE);
        return false;
    }
    operation_owner = 0;
    if (xQueueGenericSend(operation_mutex, 0, 0, 0) != 1) {
        /* Ownership is uncertain. Retain the mutex, ELF and dependencies;
         * no later callback, including quiesce, can erase this poison. */
        __atomic_store_n(&mutex_poisoned, true, __ATOMIC_RELEASE);
        return false;
    }
#else
    __atomic_clear(&operation_busy, __ATOMIC_RELEASE);
#endif
    return true;
}
static bool enter(void) {
    if (!enter_lifecycle()) return false;
    if (power_down_prepared
#ifdef STORAGE_VOLUME_ADMISSION_FROZEN
        || STORAGE_VOLUME_ADMISSION_FROZEN()
#endif
    ) { (void)leave(); return false; }
    return true;
}
static bool enter_ready(void) {
#ifdef STORAGE_VOLUME_SERIALIZED
    if (!enter()) return false;
    if (!mounted || io_failed) { (void)leave(); return false; }
    return true;
#else
    /* Keep the T5 transport's existing readiness-before-admission behavior. */
    return mounted && !io_failed &&
        !__atomic_load_n(&power_down_prepared, __ATOMIC_ACQUIRE) && enter();
#endif
}
int risc_fatfs_checkpoint(void) {
    if (!clock_api || io_failed) return 0;
    if ((++operation_steps & 255u) == 0) cooperate(4096);
    else cooperate(0);
    if (operation_steps > 1048576u ||
        clock_api->monotonic_ms(clock_api->context) - operation_start >= STORAGE_VOLUME_OPERATION_BUDGET_MS) {
        fail("filesystem operation budget exceeded"); io_failed = true; mounted = false; return 0;
    }
    return 1;
}
static bool disk_budget(void) {
    if (++operation_sectors > STORAGE_VOLUME_OPERATION_SECTOR_LIMIT || !risc_fatfs_checkpoint()) {
        fail("filesystem I/O budget exceeded"); io_failed = true; mounted = false; return false;
    }
    cooperate(0);
    return true;
}
DSTATUS disk_initialize(BYTE drive) { return drive || !card_ready || io_failed ? STA_NOINIT : 0; }
DSTATUS disk_status(BYTE drive) { return disk_initialize(drive); }
DRESULT disk_read(BYTE drive, BYTE *buffer, LBA_t lba, UINT count) {
    if (disk_status(drive) || !buffer || !count || count > 128u || lba > UINT32_MAX - count) return RES_PARERR;
    for (UINT i = 0; i < count; ++i) {
        if (!disk_budget() || !read_sector((uint32_t)lba + i, buffer + i * 512u)) {
            fail("SD sector read/CRC failure"); io_failed = true; mounted = false; return RES_ERROR;
        }
    }
    return RES_OK;
}
DRESULT disk_write(BYTE drive, const BYTE *buffer, LBA_t lba, UINT count) {
    if (disk_status(drive) || !buffer || !count || count > 128u || lba > UINT32_MAX - count) return RES_PARERR;
    for (UINT i = 0; i < count; ++i) {
        if (!disk_budget() || !write_sector((uint32_t)lba + i, buffer + i * 512u)) {
            fail("SD write uncertain; remount required"); io_failed = true; mounted = false; return RES_ERROR;
        }
    }
    return RES_OK;
}
DRESULT disk_ioctl(BYTE drive, BYTE command_code, void *buffer) {
    (void)buffer;
    if (disk_status(drive)) return RES_NOTRDY;
    return command_code == CTRL_SYNC && sync_card() ? RES_OK : RES_ERROR;
}
static bool result_ok(FRESULT result) {
    if (result == FR_OK) return true;
    if (result == FR_DISK_ERR || result == FR_INT_ERR || result == FR_TIMEOUT) {
        io_failed = true; mounted = false;
        if (!error[0]) fail("filesystem media/integrity failure");
    } else if (result == FR_LOCKED || result == FR_TOO_MANY_OPEN_FILES) fail("filesystem object busy");
    else if (result == FR_EXIST) fail("destination already exists");
    else if (result == FR_DENIED) fail("filesystem access denied or full");
    else fail("filesystem path/operation failed");
    return false;
}
/* Bounded absolute UTF-8 paths, no drive syntax, traversal or empty components. */
static bool valid_path(const char *path) {
    if (!path || path[0] != '/') return false;
    size_t count = 0, component = 0, depth = 0;
    for (const char *p = path + 1; ; ++p) {
        if (++count >= RISC_STORAGE_VOLUME_PATH_MAX) return false;
        const unsigned char c = (unsigned char)*p;
        if (!c || c == '/') {
            if (!component) return !c && p == path + 1;
            const char *start = p - component;
            if ((component == 1 && start[0] == '.') ||
                (component == 2 && start[0] == '.' && start[1] == '.') ||
                start[component - 1] == '.' || start[component - 1] == ' ' || ++depth > 24u) return false;
            if (!c) return true;
            component = 0;
        } else {
            if (c < 32u || c == ':' || c == '\\' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') return false;
            ++component;
        }
    }
}
static uint32_t allocate_handle(unsigned slot) {
    if (next_generation >= 0xffffffu) { fail("handle generation exhausted"); return 0; }
    return (++next_generation << 8) | (slot + 1u);
}
static file_slot *file_for(uint32_t handle) {
    const unsigned slot = (handle & 255u);
    return slot && slot <= FILE_SLOTS && files[slot - 1].handle == handle ? &files[slot - 1] : 0;
}
static dir_slot *dir_for(uint32_t handle) {
    const unsigned slot = (handle & 255u);
    return slot && slot <= DIR_SLOTS && dirs[slot - 1].handle == handle ? &dirs[slot - 1] : 0;
}
static bool mount_filesystem(void) {
    mounted = result_ok(f_mount(&filesystem, "", 1));
    return mounted;
}
static bool refresh(void *context) {
    (void)context;
    if (!enter()) return false;
    if (has_handles()) { fail("remount refused with live handles"); leave(); return false; }
    (void)f_mount(0, "", 0);
    mounted = card_ready = io_failed = false;
    error[0] = 0;
    if (started) (void)init_card();
    return leave() && started;
}
static bool ready(void *context) {
    (void)context;
#ifdef STORAGE_VOLUME_SERIALIZED
    if (!enter_ready()) return false;
    return leave();
#else
    return mounted && !io_failed && !__atomic_load_n(&power_down_prepared, __ATOMIC_ACQUIRE);
#endif
}
static bool label(void *context, char *out, size_t size) {
    (void)context;
    if (!out || size < sizeof(STORAGE_VOLUME_LABEL)) return false;
#ifdef STORAGE_VOLUME_SERIALIZED
    if (!enter_ready()) return false;
    memcpy(out, STORAGE_VOLUME_LABEL, sizeof(STORAGE_VOLUME_LABEL)); return leave();
#else
    if (!ready(0)) return false;
    memcpy(out, STORAGE_VOLUME_LABEL, sizeof(STORAGE_VOLUME_LABEL)); return true;
#endif
}
static bool stat_path(void *context, const char *path, uint64_t *size, bool *directory) {
    (void)context;
    if (!valid_path(path) || !size || !directory || !enter_ready()) return false;
    FILINFO info;
    FRESULT result = FR_OK;
    if (equal(path, "/")) { memset(&info, 0, sizeof(info)); info.fattrib = AM_DIR; }
    else result = f_stat(path, &info);
    if (result == FR_OK) { *size = info.fsize; *directory = (info.fattrib & AM_DIR) != 0; }
    bool ok = result_ok(result); return leave() && ok;
}
static risc_storage_dir_t dir_open(void *context, const char *path) {
    (void)context;
    if (!valid_path(path) || !enter_ready()) return 0;
    uint32_t handle = 0;
    for (unsigned i = 0; i < DIR_SLOTS; ++i) if (!dirs[i].handle) {
        const uint32_t candidate = allocate_handle(i);
        if (!candidate) break;
        if (result_ok(f_opendir(&dirs[i].object, path))) {
            dirs[i].error = 0; dirs[i].handle = handle = candidate;
            memcpy(dirs[i].path, path, strlen(path) + 1);
        }
        break;
    }
    return leave() ? handle : 0;
}
static bool dir_next(void *context, risc_storage_dir_t handle, risc_storage_dirent_v1 *out) {
    (void)context;
    if (!out || !enter_ready()) return false;
    dir_slot *slot = dir_for(handle);
    bool ok = false;
    if (slot && !slot->error) {
        FILINFO info;
        const FRESULT result = f_readdir(&slot->object, &info);
        if (result_ok(result)) {
            const size_t length = strlen(info.fname);
            if (length && length < sizeof(out->name)) {
                memset(out, 0, sizeof(*out)); memcpy(out->name, info.fname, length + 1);
                out->size = info.fsize; out->is_directory = !!(info.fattrib & AM_DIR); ok = true;
            } else if (length) slot->error = FR_INVALID_NAME;
        } else slot->error = result;
    }
    return leave() && ok;
}
static bool dir_rewind(void *context, risc_storage_dir_t handle) {
    (void)context; if (!enter_ready()) return false;
    dir_slot *slot = dir_for(handle);
    const bool ok = slot && result_ok(f_readdir(&slot->object, 0));
    if (ok) slot->error = 0;
    return leave() && ok;
}
static bool dir_close_checked(void *context, risc_storage_dir_t handle) {
    (void)context; if (!enter()) return false;
    dir_slot *slot = dir_for(handle);
    const bool ok = slot && (io_failed || result_ok(f_closedir(&slot->object)));
    if (ok) slot->handle = 0;
    if (!leave()) { if (ok) slot->handle = handle; return false; }
    return ok;
}
static void dir_close(void *context, risc_storage_dir_t handle) { (void)dir_close_checked(context, handle); }
static risc_storage_file_t file_open_impl(void *context, const char *path, uint32_t flags, uint64_t *size, bool abortable) {
    (void)context;
    if (!valid_path(path) || !(flags & 3u) || (flags & ~63u) ||
        ((flags & 60u) && !(flags & RISC_STORAGE_OPEN_WRITE)) ||
        ((flags & RISC_STORAGE_OPEN_EXCLUSIVE) && !(flags & RISC_STORAGE_OPEN_CREATE)) || !enter_ready()) return 0;
    BYTE mode = (flags & RISC_STORAGE_OPEN_READ ? FA_READ : 0) | (flags & RISC_STORAGE_OPEN_WRITE ? FA_WRITE : 0);
    if (flags & RISC_STORAGE_OPEN_EXCLUSIVE) mode |= FA_CREATE_NEW;
    else if (flags & RISC_STORAGE_OPEN_CREATE) mode |= flags & RISC_STORAGE_OPEN_TRUNCATE ? FA_CREATE_ALWAYS : FA_OPEN_ALWAYS;
    uint32_t handle = 0;
    for (unsigned i = 0; i < FILE_SLOTS; ++i) if (!files[i].handle) {
        const uint32_t candidate = allocate_handle(i);
        if (!candidate) break;
        file_slot *slot = &files[i];
        if (!result_ok(f_open(&slot->object, path, mode))) break;
        slot->handle = candidate; slot->flags = flags; slot->error = 0; slot->abortable = abortable;
        memcpy(slot->path, path, strlen(path) + 1);
        if ((flags & RISC_STORAGE_OPEN_TRUNCATE) && !(flags & RISC_STORAGE_OPEN_CREATE)) {
            if (!result_ok(f_truncate(&slot->object))) { slot->error = FR_DISK_ERR; break; }
        }
        if ((flags & RISC_STORAGE_OPEN_APPEND) && !result_ok(f_lseek(&slot->object, f_size(&slot->object)))) {
            slot->error = FR_DISK_ERR; break;
        }
        if (size) *size = f_size(&slot->object);
        handle = candidate; break;
    }
    return leave() ? handle : 0;
}
static risc_storage_file_t file_open(void *context, const char *path, uint32_t flags) {
    return file_open_impl(context, path, flags, 0, false);
}
static risc_storage_file_t file_open_read(void *context, const char *path, uint64_t *size) {
    return size ? file_open_impl(context, path, RISC_STORAGE_OPEN_READ, size, false) : 0;
}
static risc_storage_file_t file_open_write(void *context, const char *path) {
    return file_open_impl(context, path,
        RISC_STORAGE_OPEN_WRITE | RISC_STORAGE_OPEN_CREATE | RISC_STORAGE_OPEN_EXCLUSIVE, 0, true);
}
static size_t file_read(void *context, uint32_t handle, void *buffer, size_t capacity) {
    (void)context;
    if (!buffer || !capacity || !enter_ready()) return 0;
    file_slot *slot = file_for(handle); UINT count = 0;
    if (slot && !slot->error && (slot->flags & RISC_STORAGE_OPEN_READ)) {
        const FRESULT result = f_read(&slot->object, buffer, capacity < RISC_STORAGE_VOLUME_IO_MAX ? capacity : RISC_STORAGE_VOLUME_IO_MAX, &count);
        if (!result_ok(result)) slot->error = result;
    } else if (slot && !slot->error) slot->error = FR_DENIED;
    return leave() ? count : 0;
}
static size_t file_write(void *context, uint32_t handle, const void *buffer, size_t size) {
    (void)context;
    if (!buffer || !size || !enter_ready()) return 0;
    file_slot *slot = file_for(handle); UINT count = 0;
    if (slot && !slot->error && (slot->flags & RISC_STORAGE_OPEN_WRITE)) {
        FRESULT result = FR_OK;
        if (slot->flags & RISC_STORAGE_OPEN_APPEND) result = f_lseek(&slot->object, f_size(&slot->object));
        const UINT request = size < RISC_STORAGE_VOLUME_IO_MAX ? size : RISC_STORAGE_VOLUME_IO_MAX;
        if (result == FR_OK) result = f_write(&slot->object, buffer, request, &count);
        if (!result_ok(result)) slot->error = result;
        else if (count != request) { slot->error = FR_DENIED; fail("volume full"); }
    } else if (slot && !slot->error) slot->error = FR_DENIED;
    return leave() ? count : 0;
}
static bool file_seek(void *context, uint32_t handle, uint64_t offset) {
    (void)context; if (offset > UINT32_MAX || !enter_ready()) return false;
    file_slot *slot = file_for(handle);
    bool ok = false;
    /* Do not create uninitialized holes. Callers extend with explicit writes. */
    if (slot && !slot->error && offset <= f_size(&slot->object)) {
        const FRESULT result = f_lseek(&slot->object, (FSIZE_t)offset);
        ok = result_ok(result) && f_tell(&slot->object) == offset;
        if (!ok) slot->error = result == FR_OK ? FR_INT_ERR : result;
    }
    return leave() && ok;
}
static bool file_info(void *context, uint32_t handle, uint64_t *size, uint64_t *position) {
    (void)context; if (!size || !position || !enter_ready()) return false;
    file_slot *slot = file_for(handle);
    if (slot) { *size = f_size(&slot->object); *position = f_tell(&slot->object); }
    return leave() && slot != 0;
}
static bool file_sync(void *context, uint32_t handle) {
    (void)context; if (!enter_ready()) return false;
    file_slot *slot = file_for(handle);
    bool ok = false;
    if (slot && !slot->error) {
        FRESULT result = f_sync(&slot->object); ok = result_ok(result); if (!ok) slot->error = result;
    }
    return leave() && ok;
}
static bool file_close(void *context, uint32_t handle, bool commit) {
    (void)context; if (!enter()) return false;
    file_slot *slot = file_for(handle);
    bool ok = false;
    if (slot) {
        if (io_failed && (slot->flags & RISC_STORAGE_OPEN_WRITE)) {
            /* Retain uncertain writable ownership; quiesce/remount must refuse. */
            leave(); return false;
        }
        ok = io_failed || result_ok(f_close(&slot->object));
        if (ok && slot->abortable && !commit && !io_failed) ok = result_ok(f_unlink(slot->path));
        if (ok) slot->handle = 0;
    }
    if (!leave()) { if (ok) slot->handle = handle; return false; }
    return ok;
}
static uint32_t handle_error(void *context, uint32_t handle, bool directory) {
    (void)context;
    if (!enter()) return FR_LOCKED;
    const dir_slot *dir = directory ? dir_for(handle) : 0;
    const file_slot *file = directory ? 0 : file_for(handle);
    uint32_t result = (!mounted || io_failed) ? FR_DISK_ERR : dir ? dir->error : file ? file->error : FR_INVALID_OBJECT;
    return leave() ? result : FR_LOCKED;
}
/* Compare actual directory clusters rather than textual prefixes: SFN aliases
 * and case variants must not let a rename invalidate a live descendant. */
static bool path_uses_cluster(const char *path, bool leaf_directory, DWORD cluster) {
    char prefix[RISC_STORAGE_VOLUME_PATH_MAX];
    memcpy(prefix, path, strlen(path) + 1);
    const size_t length = strlen(prefix);
    for (size_t i = 1; i <= length; ++i) {
        if (prefix[i] != '/' && !(i == length && leaf_directory)) continue;
        const char saved = prefix[i]; prefix[i] = 0;
        DIR probe;
        FRESULT result = f_opendir(&probe, prefix);
        if (result != FR_OK) { (void)result_ok(result); return true; }
        const bool match = probe.obj.sclust == cluster;
        result = f_closedir(&probe);
        prefix[i] = saved;
        if (!result_ok(result) || match) return true;
    }
    return false;
}
static bool directory_in_use(const char *path) {
    FILINFO info;
    if (f_stat(path, &info) != FR_OK || !(info.fattrib & AM_DIR)) return false;
    DIR probe;
    FRESULT result = f_opendir(&probe, path);
    if (!result_ok(result)) return true;
    const DWORD cluster = probe.obj.sclust;
    if (!result_ok(f_closedir(&probe))) return true;
    for (unsigned i = 0; i < FILE_SLOTS; ++i)
        if (files[i].handle && path_uses_cluster(files[i].path, false, cluster)) return true;
    for (unsigned i = 0; i < DIR_SLOTS; ++i)
        if (dirs[i].handle && path_uses_cluster(dirs[i].path, true, cluster)) return true;
    return false;
}
static bool remove_path(void *context, const char *path) {
    (void)context; if (!valid_path(path) || equal(path, "/") || !enter_ready()) return false;
    if (directory_in_use(path)) { fail("directory has live handles"); leave(); return false; }
    bool ok = result_ok(f_unlink(path)); return leave() && ok;
}
static bool mkdir_path(void *context, const char *path) {
    (void)context; if (!valid_path(path) || !enter_ready()) return false;
    bool ok = result_ok(f_mkdir(path)); return leave() && ok;
}
static bool rename_path(void *context, const char *from, const char *to) {
    (void)context; if (!valid_path(from) || !valid_path(to) || equal(from, "/") || equal(to, "/") || !enter_ready()) return false;
    FILINFO info;
    FRESULT destination = f_stat(to, &info);
    if (destination == FR_OK) { fail("destination already exists"); leave(); return false; }
    if (destination != FR_NO_FILE && destination != FR_NO_PATH) {
        (void)result_ok(destination); leave(); return false;
    }
    if (!result_ok(f_stat(from, &info))) { leave(); return false; }
    if (info.fattrib & AM_DIR) {
        DIR probe;
        if (!result_ok(f_opendir(&probe, from))) { leave(); return false; }
        DWORD cluster = probe.obj.sclust;
        if (!result_ok(f_closedir(&probe)) || path_uses_cluster(to, false, cluster)) {
            fail("cannot move directory into itself"); leave(); return false;
        }
    }
    if (directory_in_use(from)) { fail("rename has live descendants"); leave(); return false; }
    bool ok = result_ok(f_rename(from, to)); return leave() && ok;
}
#include "fs_extensions.inc"
static bool last_error_api(void *context, char *out, size_t capacity) {
    (void)context; if (!out || !capacity) return false;
#ifdef STORAGE_VOLUME_SERIALIZED
    if (!enter_lifecycle()) return false;
#endif
    size_t i = 0; while (error[i] && i + 1 < capacity) { out[i] = error[i]; ++i; }
    out[i] = 0;
    const bool present = error[0] != 0;
#ifdef STORAGE_VOLUME_SERIALIZED
    return leave() && present;
#else
    return present;
#endif
}
static bool prepare_power_down(void *context) {
    (void)context;
    if (!enter_lifecycle()) return false;
#ifdef STORAGE_VOLUME_ADMISSION_FROZEN
    if (STORAGE_VOLUME_ADMISSION_FROZEN()) { (void)leave(); return false; }
#endif
#ifdef STORAGE_VOLUME_HAS_SLEEP_RECOVERY
    if (sleep_state != SLEEP_ACTIVE) { (void)leave(); return false; }
#endif
    if (power_down_prepared) { return leave(); }
    if (io_failed || !transport_idle()) { leave(); return false; }
    for (unsigned i = 0; i < FILE_SLOTS; ++i) {
        if (files[i].handle && (files[i].flags & RISC_STORAGE_OPEN_WRITE)) {
            fail("power down refused with live writer"); leave(); return false;
        }
    }
    // Read handles carry only RAM metadata; keep their ELF and dependencies
    // pinned. No filesystem operation or active transport survives this barrier.
    if (card_ready && !sync_card()) { fail("power down media sync failed"); leave(); return false; }
    STORAGE_VOLUME_SET_PREPARED(true);
    return leave();
}
static bool cancel_power_down(void *context) {
    (void)context;
    if (!enter_lifecycle()) return false;
#ifdef STORAGE_VOLUME_ADMISSION_FROZEN
    if (STORAGE_VOLUME_ADMISSION_FROZEN()) { (void)leave(); return false; }
#endif
#ifdef STORAGE_VOLUME_HAS_SLEEP_RECOVERY
    if (sleep_state != SLEEP_ACTIVE) { (void)leave(); return false; }
#endif
    const bool safe = !io_failed && transport_idle()
#ifdef STORAGE_VOLUME_HAS_POWER_COMMIT
        && !power_down_committed
#endif
        ;
    if (safe) STORAGE_VOLUME_SET_PREPARED(false);
    return leave() && safe;
}
#ifdef STORAGE_VOLUME_HAS_POWER_COMMIT
static bool commit_power_down(void *context) {
    (void)context;
    if (!enter_lifecycle()) return false;
#ifdef STORAGE_VOLUME_ADMISSION_FROZEN
    if (STORAGE_VOLUME_ADMISSION_FROZEN()) { (void)leave(); return false; }
#endif
#ifdef STORAGE_VOLUME_HAS_SLEEP_RECOVERY
    if (sleep_state != SLEEP_ACTIVE) { (void)leave(); return false; }
#endif
    const bool safe = power_down_prepared && !io_failed && transport_idle();
    if (safe && !power_down_committed) {
#ifdef STORAGE_VOLUME_TRY_COMMIT_POWER_DOWN
        /* Scoped transports may fail; never publish commit on uncertainty. */
        if (!STORAGE_VOLUME_TRY_COMMIT_POWER_DOWN()) { (void)leave(); return false; }
#else
        /* Legacy board transition is fixed, synchronous and infallible after
         * its frozen/synced precondition. No fallible I/O follows. */
        STORAGE_VOLUME_COMMIT_POWER_DOWN();
#endif
        power_down_committed = true;
    }
    return leave() && safe;
}
#ifdef STORAGE_VOLUME_HAS_SLEEP_RECOVERY
/* This opt-in path never carries a live FatFs object across unmount/rail-off.
 * The hook runs under the same owner-task operation guard as ordinary I/O.
 * It must run normal init_card(), check GPIO/hold recovery, and leave mounted /
 * card_ready accurate even when no card or filesystem is available. */
static bool sleep_leave(void) {
    if (leave()) return true;
    sleep_state = SLEEP_RETAINED;
    STORAGE_VOLUME_SET_PREPARED(true);
    return false;
}
static bool prepare_sleep(void *context) {
    (void)context;
    if (!enter_lifecycle()) return false;
#ifdef STORAGE_VOLUME_ADMISSION_FROZEN
    if (STORAGE_VOLUME_ADMISSION_FROZEN()) { (void)leave(); return false; }
#endif
    if (sleep_state == SLEEP_PREPARED) return sleep_leave();
    if (sleep_state != SLEEP_ACTIVE || power_down_prepared || power_down_committed ||
        !started || io_failed || !transport_idle() || has_handles()) {
        (void)sleep_leave(); return false;
    }
    if (card_ready && !sync_card()) {
        fail("sleep media sync failed");
        sleep_state = SLEEP_RETAINED;
        STORAGE_VOLUME_SET_PREPARED(true);
        (void)sleep_leave(); return false;
    }
    sleep_state = SLEEP_PREPARED;
    STORAGE_VOLUME_SET_PREPARED(true);
    return sleep_leave();
}
static bool commit_sleep(void *context) {
    (void)context;
    if (!enter_lifecycle()) return false;
#ifdef STORAGE_VOLUME_ADMISSION_FROZEN
    if (STORAGE_VOLUME_ADMISSION_FROZEN()) { (void)leave(); return false; }
#endif
    if (sleep_state == SLEEP_COMMITTED) return sleep_leave();
    if (sleep_state != SLEEP_PREPARED || io_failed || !transport_idle() || has_handles()) {
        (void)sleep_leave(); return false;
    }
    /* Mark retention before the first destructive transition. A partial rail
     * failure must never fall through legacy cancel or normal refresh. */
    sleep_state = SLEEP_RETAINED;
    const FRESULT unmounted = f_mount(NULL, "", 0);
    mounted = card_ready = false;
    if (unmounted != FR_OK || !STORAGE_VOLUME_TRY_COMMIT_POWER_DOWN()) {
        fail("sleep power down retained"); (void)sleep_leave(); return false;
    }
    sleep_state = SLEEP_COMMITTED;
    return sleep_leave();
}
static int32_t resume_sleep(void *context) {
    (void)context;
    /* The custody hook must be safe even for rejected non-owner callers. Do
     * not inspect transaction state without owning the operation guard. */
    if (!enter_lifecycle())
        return STORAGE_VOLUME_SLEEP_UNSAFE() ? RISC_STORAGE_SLEEP_RETAINED : RISC_STORAGE_SLEEP_REFUSED;
#ifdef STORAGE_VOLUME_ADMISSION_FROZEN
    if (STORAGE_VOLUME_ADMISSION_FROZEN())
        return leave() ? RISC_STORAGE_SLEEP_REFUSED : RISC_STORAGE_SLEEP_RETAINED;
#endif
    int32_t result;
    if (sleep_state == SLEEP_RETAINED || STORAGE_VOLUME_SLEEP_UNSAFE()) {
        sleep_state = SLEEP_RETAINED;
        STORAGE_VOLUME_SET_PREPARED(true);
        result = RISC_STORAGE_SLEEP_RETAINED;
    } else if (!started || power_down_committed ||
               (sleep_state == SLEEP_ACTIVE && power_down_prepared)) {
        result = RISC_STORAGE_SLEEP_REFUSED;
    } else {
        if (sleep_state == SLEEP_COMMITTED) {
            /* No stale filesystem state is reused. Transport initialization is
             * checked separately from usable/absent media. */
            mounted = card_ready = io_failed = false;
            error[0] = 0;
            if (!STORAGE_VOLUME_TRY_RESUME_POWER_DOWN() || io_failed ||
                !transport_idle() || STORAGE_VOLUME_SLEEP_UNSAFE())
                sleep_state = SLEEP_RETAINED;
        }
        if (sleep_state == SLEEP_RETAINED || io_failed || !transport_idle()) {
            sleep_state = SLEEP_RETAINED;
            STORAGE_VOLUME_SET_PREPARED(true);
            result = RISC_STORAGE_SLEEP_RETAINED;
        } else {
            sleep_state = SLEEP_ACTIVE;
            STORAGE_VOLUME_SET_PREPARED(false);
            result = mounted && card_ready ? RISC_STORAGE_SLEEP_READY : RISC_STORAGE_SLEEP_MEDIA_UNAVAILABLE;
        }
    }
    return sleep_leave() ? result : RISC_STORAGE_SLEEP_RETAINED;
}
static const risc_storage_volume_api_v1_sleep api = { { {
#else
static const risc_storage_volume_api_v1_power_commit api = { {
#endif
#else
static const risc_storage_volume_api_v1_power api = {
#endif
  {
    {RISC_STORAGE_VOLUME_API_V1, sizeof(api), 0, refresh, ready, label, stat_path,
     dir_open, dir_next, dir_close, file_open_read, file_read, file_open_write,
     file_write, file_close, remove_path, last_error_api},
    file_open, file_seek, file_info, file_sync, dir_rewind, dir_close_checked,
    handle_error, mkdir_path, rename_path
  }, prepare_power_down, cancel_power_down
#ifdef STORAGE_VOLUME_HAS_POWER_COMMIT
  }, RISC_STORAGE_POWER_COMMIT_TAG, 1u, commit_power_down
#endif
#ifdef STORAGE_VOLUME_HAS_SLEEP_RECOVERY
  }, RISC_STORAGE_SLEEP_TAG, 1u, prepare_sleep, commit_sleep, resume_sleep
#endif
};
