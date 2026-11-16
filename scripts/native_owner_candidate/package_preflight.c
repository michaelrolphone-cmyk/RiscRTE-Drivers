/* Host-only pre-emission checks over the actual pinned loader implementations.
 * This does not load, relocate, authenticate, or execute the target image. */
#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "private/elf_types.h"
#include "private/esp_privileged_manifest_imports.h"

_Static_assert(sizeof(elf32_hdr_t) == 52, "ELF32 header layout");
_Static_assert(sizeof(elf32_shdr_t) == 40, "ELF32 section layout");
_Static_assert(sizeof(elf32_sym_t) == 16, "ELF32 symbol layout");
_Static_assert(sizeof(elf32_rela_t) == 12, "ELF32 relocation layout");

/* Exact signature from the pinned esp_elf_validate.c; avoid unrelated port API. */
bool esp_elf_validate_file(const uint8_t *image, size_t length);

static unsigned char *read_bounded(const char *path, size_t limit, size_t *size) {
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    unsigned char *data = malloc(limit + 1u);
    if (!data) { fclose(file); return NULL; }
    *size = fread(data, 1, limit + 1u, file);
    const bool ok = !ferror(file) && *size <= limit && feof(file);
    fclose(file);
    if (!ok) { free(data); return NULL; }
    data[*size] = 0;
    return data;
}

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    size_t length = 0, declaration_length = 0;
    unsigned char *image = read_bounded(argv[1], 8u * 1024u * 1024u, &length);
    if (!image) { fputs("image-input-rejected\n", stderr); return 3; }
    /* Mandatory ordering: no import or Python mapping parser precedes this. */
    if (!esp_elf_validate_file(image, length)) {
        free(image); fputs("structural-rejected\n", stderr); return 20;
    }
    if (strcmp(argv[2], "--structure") == 0) {
        free(image); puts("RISC_STRUCTURE_V1 PASS"); return 0;
    }
    unsigned char *declaration = read_bounded(argv[2], 128u * 128u, &declaration_length);
    const char *declared[128] = {NULL}; /* Nonnull even for intentional zero. */
    size_t count = 0, start = 0;
    bool canonical = declaration && declaration_length &&
        declaration[declaration_length - 1u] == '\n';
    if (canonical && !(declaration_length == 1 && declaration[0] == '\n')) {
        for (size_t i = 0; i < declaration_length && canonical; ++i) {
            if (declaration[i] != '\n') {
                if (declaration[i] < 0x21 || declaration[i] > 0x7e) canonical = false;
                continue;
            }
            if (i == start || i - start > 127 || count == 128) { canonical = false; break; }
            declaration[i] = 0;
            declared[count++] = (const char *)declaration + start;
            start = i + 1u;
        }
    }
    if (!canonical) {
        free(declaration); free(image); fputs("declaration-rejected\n", stderr); return 21;
    }
    /* This matcher also invokes the actual candidate-v5 import authorization. */
    const bool accepted = esp_elf_privileged_manifest_imports_match_v5(
        image, length, declared, count);
    free(declaration); free(image);
    if (!accepted) { fputs("import-policy-or-exact-set-rejected\n", stderr); return 22; }
    puts("RISC_NATIVE5_PREFLIGHT_V1 PASS");
    return 0;
}
