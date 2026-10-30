/* Pure register-I/O contract fixtures; no electrical/device qualification. */
#include "BqChargerProfile.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t regs[32];
static int fail_read, fail_write, corrupt_read;
static unsigned writes;
static bool rd(void *c,uint8_t reg,uint8_t *out) {
 (void)c; if(reg==fail_read)return false;
 *out=regs[reg]; if(writes && reg==corrupt_read)*out^=0xffu; return true;
}
static bool wr(void *c,uint8_t reg,uint8_t value) {
 (void)c; ++writes; regs[reg]=value; return reg!=fail_write;
}
static void reset(void) {memset(regs,0,sizeof(regs));fail_read=fail_write=corrupt_read=-1;writes=0;}
int main(void) {
 const risc_bq_charger_io io={NULL,rd,wr}; bool uncertain;
 for(size_t i=0;i<sizeof(risc_bq_t5s3_charge_profile)/sizeof(risc_bq_t5s3_charge_profile[0]);++i) {
  const risc_bq_profile_field *f=&risc_bq_t5s3_charge_profile[i];
  reset(); fail_read=f->reg; uncertain=true;
  assert(!risc_bq_apply_charge_profile(&io,&uncertain)); assert(!uncertain && writes==0);
  reset(); regs[f->reg]=(uint8_t)((f->value^f->mask)&f->mask);
  /* Keep OTG clear and BATFET irrelevant to the charger entry veto. */
  if(f->reg==0x03u)regs[f->reg]=0;
  fail_write=f->reg; uncertain=false;
  assert(!risc_bq_apply_charge_profile(&io,&uncertain)); assert(uncertain && writes>0);
  reset(); regs[f->reg]=(uint8_t)((f->value^f->mask)&f->mask);
  if(f->reg==0x03u)regs[f->reg]=0;
  corrupt_read=f->reg; uncertain=false;
  assert(!risc_bq_apply_charge_profile(&io,&uncertain)); assert(uncertain);
 }
 reset(); assert(risc_bq_apply_charge_profile(&io,&uncertain)); assert(!uncertain);
 for(size_t i=0;i<sizeof(risc_bq_t5s3_charge_profile)/sizeof(risc_bq_t5s3_charge_profile[0]);++i) {
  const risc_bq_profile_field *f=&risc_bq_t5s3_charge_profile[i];assert((regs[f->reg]&f->mask)==(f->value&f->mask));
 }
 reset();regs[0x14]=0x08;assert(!risc_bq_apply_charge_profile(&io,&uncertain)&&!writes);
 reset();regs[0x03]=0x20;assert(!risc_bq_apply_charge_profile(&io,&uncertain)&&!writes);
 reset();regs[0x0b]=0xe0;assert(!risc_bq_apply_charge_profile(&io,&uncertain)&&!writes);
 puts("BQ charger pre-read, partial-write/readback uncertainty and chip/OTG veto PASS");
}
