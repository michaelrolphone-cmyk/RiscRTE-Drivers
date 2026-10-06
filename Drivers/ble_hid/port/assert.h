#pragma once
void hid_panic(void) __attribute__((noreturn));
#define assert(x) ((x) ? (void)0 : hid_panic())
