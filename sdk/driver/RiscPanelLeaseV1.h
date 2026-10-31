#pragma once
#include <stdbool.h>
#include <stdint.h>
#define RISC_PANEL_LEASE_CAPABILITY "display.panel-lease"
#define RISC_PANEL_LEASE_API_V1 1u
/* Distinct from lifecycle display.power@1. One selected board panel authority
 * owns electrical sequencing. Generation grants are exclusive; failed acquire
 * may return a cleanup token, and failed release retains that token/dependencies.
 * Owner-task calls only; no worker/IRQ authorization is implied.
 * prepare_sleep fences new leases, refuses any live lease, and confirms safe
 * outputs only when idle. Every attempt requires paired resume, even refusal.
 * resume removes the fence only after safe cleanup; it never acquires a lease.
 * No deep-sleep pad retention or terminal entry guarantee is implied. */
typedef struct {
 uint32_t api_version,struct_size;
 void *context;
 bool (*is_owner)(void *);
 bool (*acquire)(void *,uint64_t *);
 bool (*release)(void *,uint64_t);
 bool (*prepare_sleep)(void *);
 bool (*resume)(void *);
} risc_panel_lease_api_v1;
