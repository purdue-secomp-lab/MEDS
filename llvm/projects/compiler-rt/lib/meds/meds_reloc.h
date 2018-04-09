#ifndef __MEDS_RELOC_H__
#define __MEDS_RELOC_H__

typedef unsigned long addr_t;

void init_relocate(void);
bool do_relocate(addr_t old_addr, addr_t new_addr, addr_t size);

#endif
