#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
// #include "rerand_common.h"
// #include "rerand_mem_map.h"

#include <sys/stat.h>
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_procmaps.h"

#include "elf.h"
#include "meds_reloc.h"

using namespace __sanitizer;


#define DOUT(...)
#define WARN(...)

// #define DOUT(...) do {                                  \
//     if (true) {                                         \
//       Printf("[RT] (%s:%d) ", __FUNCTION__, __LINE__);  \
//       Printf(__VA_ARGS__);                              \
//     }                                                   \
//   } while (0)

// #define WARN(...) do {                                  \
//     if (true) {                                         \
//       Printf("[WARN] (%s:%d) ", __FUNCTION__, __LINE__);  \
//       Printf(__VA_ARGS__);                              \
//     }                                                   \
//   } while (0)

#define FATAL(...) do {                                     \
    Printf("\n[FATAL] (%s:%d) ", __FUNCTION__, __LINE__);   \
    Printf(__VA_ARGS__);                                    \
    Die();                                                  \
  } while (0)

unsigned char *program;
size_t program_size;

#include <endian.h>
#if BYTE_ORDER == BIG_ENDIAN
# define byteorder ELFDATA2MSB
#elif BYTE_ORDER == LITTLE_ENDIAN
# define byteorder ELFDATA2LSB
#else
# error "Unknown BYTE_ORDER " BYTE_ORDER
# define byteorder ELFDATANONE
#endif

#define GET_OBJ(type, offset)                                   \
  reinterpret_cast<type*>( reinterpret_cast<size_t>(program)    \
                           + static_cast<size_t>(offset) )

#define CHECK_SIZE(obj, size)                               \
  ((addr_t)obj + size <= (addr_t)program + program_size)

static Elf64_Ehdr *pehdr;
static Elf64_Shdr *pshdr;
static size_t n_symtab;
static Elf64_Sym *symtab;
char *strtab;

size_t n_rel;               /* # of relocation tables */
size_t *n_reltab;           /* # of relocation entry */
static Elf64_Rela **reltab; /* array of pointers to relocation tables */
#define REL_DST_NDX(ofs) ((ofs) >> 32)
#define REL_DST_OFS(ofs) ((ofs) & 0xffffffff)

#define STR_EQUAL(s1, s2, n)                                    \
  str_equal((const uint8_t *)(s1), (const uint8_t *)(s2), (n))
uint8_t str_equal(const uint8_t *s1, const uint8_t *s2, size_t n) {
  for (unsigned i = 0;i < n;++i)
    if (s1[i] != s2[i])
      return 0;
  return 1;
}

static void validate_ehdr(void) {
  static const unsigned char expected[EI_NIDENT] =
    {
      [EI_MAG0] = ELFMAG0,
      [EI_MAG1] = ELFMAG1,
      [EI_MAG2] = ELFMAG2,
      [EI_MAG3] = ELFMAG3,
      [EI_CLASS] = ELFCLASS64,
      [EI_DATA] = byteorder,
      [EI_VERSION] = EV_CURRENT,
      [EI_OSABI] = ELFOSABI_SYSV,
      [EI_ABIVERSION] = 0
    };

  if ((pehdr = GET_OBJ(Elf64_Ehdr, 0)) == NULL)
    FATAL("Ehdr size\n");

  // TODO: check this.
  // if (!str_equal(pehdr->e_ident, expected, EI_ABIVERSION))
  //   DOUT("[WARN] Ehdr ident1\n");

  if (pehdr->e_ident[EI_ABIVERSION] != 0)
    FATAL("Ehdr ident2\n");

  if (!str_equal(&pehdr->e_ident[EI_PAD],
                 &expected[EI_PAD], EI_NIDENT - EI_PAD))
    FATAL("Ehdr ident3\n");

  if (pehdr->e_version != EV_CURRENT)
    FATAL("Ehdr version\n");

  /* ELF format check - allow executable and shared lib */
  if (pehdr->e_type != ET_EXEC && pehdr->e_type != ET_DYN)
    FATAL("[WARN] Ehdr not relocatable\n");

  /* check the architecture - currently only support x86_64 */
  if (pehdr->e_machine != EM_X86_64)
    FATAL("[WARN] Ehdr not x86_64\n");

  if (pehdr->e_shentsize != sizeof(Elf64_Shdr))
    FATAL("[WARN] Shdr entry size\n");
}

// TODO: This should be changed for SGX environment.
static void relocate_init_nosgx() {
  MemoryMappingLayout proc_maps(true);
  char filename[256];
  uptr start, end, offset, prot;

  // TODO. In the Non-SGX environment, the very first section is the program
  // section. No need to handle sections for shared objects (.so), as there will
  // be such sections in the SGX environment.
  proc_maps.Next(&start, &end, &offset, filename, ARRAY_SIZE(filename), &prot);

  DOUT("memory map : %llx - %llx %llx, (%s)\n", start, end, prot, filename);
  CHECK(prot & MemoryMappingLayout::kProtectionRead);
  CHECK(prot & MemoryMappingLayout::kProtectionExecute);
  CHECK(!(prot & MemoryMappingLayout::kProtectionWrite));

  // Simply read the whole executable file. In runtime memory, some elf headers
  // are not loaded.
  struct stat file_status;
  if(stat(filename, &file_status) != 0){
    FATAL("ERROR: Could not stat or file does not exist");
  }

  FILE *fp = fopen(filename, "r");

  program_size = file_status.st_size+1;
  program = (unsigned char*) malloc(program_size);
  CHECK(program != 0);

  if (!fread(program, file_status.st_size,1,fp)) {
    FATAL("ERROR: Could not read file");
  }
}

static void *get_buf(size_t size) {
  return malloc(size);
}

static void get_elf_info(void) {
  /* read shdr */
  if ((pshdr = GET_OBJ(Elf64_Shdr, pehdr->e_shoff)) == NULL
      || !CHECK_SIZE(pshdr, pehdr->e_shnum*sizeof(Elf64_Shdr)))
    FATAL("[WARN]: Shdr size\n");

  /* pointers to symbol, string, relocation tables */
  n_rel = 0;
  for (unsigned i = 0; i < pehdr->e_shnum; ++i) {
    if (pshdr[i].sh_type == SHT_RELA || pshdr[i].sh_type == SHT_REL)
    // if (pshdr[i].sh_type == SHT_RELA)
      ++n_rel;
    else if (pshdr[i].sh_type == SHT_SYMTAB) {
      symtab = GET_OBJ(Elf64_Sym, pshdr[i].sh_offset);
      n_symtab = pshdr[i].sh_size / sizeof(Elf64_Sym);
    } else if (pshdr[i].sh_type == SHT_STRTAB)
      strtab = GET_OBJ(char, pshdr[i].sh_offset);
  }

  n_reltab = (size_t *)get_buf(n_rel * sizeof(size_t));
  reltab = (Elf64_Rela **)get_buf(n_rel * sizeof(Elf64_Rela *));
  n_rel = 0;
  for (unsigned i = 0; i < pehdr->e_shnum; ++i) {
    if ((pshdr[i].sh_type == SHT_RELA || pshdr[i].sh_type == SHT_REL)
    // if ((pshdr[i].sh_type == SHT_RELA)
        && pshdr[i].sh_size) {
      // check: reltab must always assign symtab as sh_link
      // assert(GET_OBJ(pshdr[pshdr[i].sh_link].sh_offset) == symtab);

      reltab[n_rel] = GET_OBJ(Elf64_Rela, pshdr[i].sh_offset);
      n_reltab[n_rel] = pshdr[i].sh_size / sizeof(Elf64_Rela);
      ++n_rel;
    }
  }

  // dump reltab
  DOUT("n_rel : 0x%llx\n", n_rel);
  DOUT("n_symtab : 0x%llx\n", n_symtab);
}

// OK first let me hard-code symbols
// This should be replaced by
//      g_memory_maps->is_shuffled_recently(symbol)
static const char *cand[] = {
  "_Z3foov",
  "_Z3barv",
  "_Z9my_memcpyPvPKvm",
  "_Z11simple_loopPKvm",
  "_Z6testmev",
};

#include <string.h>
#include <sys/mman.h>

bool do_relocate(addr_t old_addr, addr_t new_addr, addr_t size) {
  int updated = false;

  for (unsigned k = 0; k < n_rel; ++k)
    for (unsigned i = 0; i < n_reltab[k]; ++i) {
      addr_t dst = (addr_t)reltab[k][i].r_offset;
      const unsigned int type = ELF64_R_TYPE(reltab[k][i].r_info);

      addr_t addend = reltab[k][i].r_addend;
      unsigned int src_sym = ELF64_R_SYM(reltab[k][i].r_info);
      addr_t st_value = symtab[src_sym].st_value;

      addr_t write_value = 0;
      bool found = false;

      if (st_value != (addr_t)old_addr)
        continue;
      if (type == R_X86_64_64) {
        DOUT("R_X86_64_64: (%llx, %llx) -> %llx): %llx, %llx\n",
             old_addr, old_addr+size, new_addr, dst, addend);

        write_value = new_addr + addend;
        found = true;

      // } else if (type == R_X86_64_32S) {
      //   DOUT("R_X86_64_32S: (%llx, %llx) -> %llx): %llx, %llx\n",
      //        old_addr, old_addr+size, new_addr, dst, addend);
      //   // write_value = new_addr + addend;
      } else {
        DOUT("unknown reloc (%llx): (%llx, %llx) -> %llx): %llx, %llx\n",
             type, old_addr, old_addr+size, new_addr, dst, addend);
      }

      // allow write operation
      if (found) {
        int res = mprotect((void *)(dst & ~4095UL), 2UL * 4096UL, PROT_READ|PROT_WRITE|PROT_EXEC);
        if (res == 0) {
          *(addr_t *)dst = new_addr + addend;
          // mprotect((void *)(dst & ~4095UL), 2UL * 4096UL, PROT_READ|PROT_EXEC);

          updated = true;

        } else {
          WARN("[warning] failed mprotect\n");
        }
      }
    }
  return updated;
}

#if 0
static void do_relocate(void) {
  for (unsigned k = 0; k < n_rel; ++k)
    for (unsigned i = 0; i < n_reltab[k]; ++i) {
      /*
       for the relocatable file (i.e., object file),
          r_offset = offset from the section
       for the shared lib or executable
          r_offset = virtual address

        --> this must be updated if the target elf format is changed
        --> here it is under an assumption that the target elf
            format is executable

        addr_t dst = (addr_t) section_base or sharedlib_base
                      + (addr_t)reltab[k][i].r_offset;
      */
      addr_t dst = (addr_t)reltab[k][i].r_offset;

      unsigned int src_sym = ELF64_R_SYM(reltab[k][i].r_info);
      const unsigned int type = ELF64_R_TYPE(reltab[k][i].r_info);

      bool cand_check = false;
      for (unsigned int p = 0; p < sizeof(cand)/sizeof(char *); p++) {
        if (!strcmp(cand[p], &strtab[symtab[src_sym].st_name])) {
          cand_check = true;
      DOUT("matched: %s, %s\n", cand[p], &strtab[symtab[src_sym].st_name]);
          break;
        }
      }
      if (!cand_check) continue;
      DOUT("symbol: %s\n", &strtab[symtab[src_sym].st_name]);

      // r_addend :
      DOUT("rel[%d] %llx --> sym %d\n", i, dst, src_sym);
      DOUT("\t r_addend: %llx \n", reltab[k][i].r_addend);
      DOUT("\t dst: %llx \n", dst);

      /* NOTE: sym.st_value = offset from section in relocatable file
       *                      virtual addr in shared lib or exec
       *
       * In relocatable file, the translation addr must be changed (+ section_base)
       */
      void *vaddr = g_memory_maps->translate((ADDRTY)symtab[src_sym].st_value);
      DOUT("vaddr: %p\n", vaddr);

      // skip if address is not changed
      if ((addr_t)vaddr == (addr_t)symtab[src_sym].st_value) continue;
      if (!vaddr) continue; // or FATAL("%d th sym is not found\n", src_sym)

      // allow write operation
      mprotect((void *)(dst & ~4095UL), 2UL * 4096UL, PROT_READ|PROT_WRITE);

      addr_t off = (addr_t)vaddr;
      if (type == R_X86_64_64) {
        /* word 64 */
        *(addr_t *)dst = off + reltab[k][i].r_addend;
      } else if (type == R_X86_64_32) {
        /* word 32 */
        *(uint32_t*)dst = (uint32_t)(off + reltab[k][i].r_addend);
      } else if (type == R_X86_64_32S) {
        /* word 32 */
        *(int32_t*)dst = (int32_t)(off + reltab[k][i].r_addend);
      } else if (type == R_X86_64_PC32 || type == R_X86_64_PLT32) {
        /* word 32 */
        *(uint32_t*)dst = (uint32_t)(off - dst + reltab[k][i].r_addend);
      } else if (type == R_X86_64_GOTPCREL) {
        // NOTE: well .. I have no idea how to support this type
        /* word 32 */
        /*
        *(uint32_t*)dst = (uint32_t)((Elf64_Addr)&(symtab[src_sym].st_value)
                                     - dst + reltab[k][i].r_addend);
        */
        FATAL("Not supported type: R_X86_64_GOTPCREL\n");
      } else if (type == R_X86_64_IRELATIVE) {
        // ptr = base + reltab[k][i].r_addend;
        FATAL("Not supported type: R_X86_64_IRELATIVE\n");
      } else {
        FATAL("Not supported type: %d\n", type);
      }
      // disallow write operation
      mprotect((void *)(dst & ~4095UL), 2UL * 4096UL, PROT_READ|PROT_EXEC);

      DOUT("%s done\n", &strtab[symtab[src_sym].st_name]);
    }
}
#endif

void init_relocate(void) {
  static bool is_reloc_init = false;
  if (!is_reloc_init) {
    DOUT("init_relocate\n");
    relocate_init_nosgx();

    // checking elf format and getting metadata
    // can be done once
    validate_ehdr();
    get_elf_info();

    is_reloc_init = true;
    CHECK(program != 0);
    CHECK(program_size != 0);
  }
}
