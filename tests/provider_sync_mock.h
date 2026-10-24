#ifndef PROVIDER_SYNC_MOCK_H
#define PROVIDER_SYNC_MOCK_H
#include "RiscProviderSyncV1.h"
#include <assert.h>
typedef struct {
    bool foreign, live, held, fail_create, retain_create, zero_create;
    bool busy, fail_unlock, fail_destroy;
    unsigned creates, destroys;
} provider_sync_mock;
static bool psm_owner(void *c) { return !((provider_sync_mock *)c)->foreign; }
static bool psm_create(void *c,uint64_t *out) {
    provider_sync_mock *m=c; *out=0; assert(!m->live); ++m->creates;
    if(m->foreign||m->fail_create)return false;
    if(m->zero_create)return true;
    m->live=true;*out=1;return !m->retain_create;
}
static bool psm_lock(void *c,uint64_t token) {
    provider_sync_mock *m=c; assert(token==1&&m->live);
    if(m->foreign||m->held||m->busy)return false;
    m->held=true;return true;
}
static bool psm_unlock(void *c,uint64_t token) {
    provider_sync_mock *m=c;assert(token==1&&m->live&&m->held);
    if(m->foreign||m->fail_unlock)return false;
    m->held=false;return true;
}
static bool psm_destroy(void *c,uint64_t token) {
    provider_sync_mock *m=c;assert(token==1&&m->live&&!m->held);
    if(m->foreign||m->fail_destroy)return false;
    m->live=false;++m->destroys;return true;
}
#define PROVIDER_SYNC_MOCK_API(model) {1,sizeof(risc_provider_sync_api_v1),&(model),psm_owner,psm_create,psm_lock,psm_unlock,psm_destroy}
#endif
