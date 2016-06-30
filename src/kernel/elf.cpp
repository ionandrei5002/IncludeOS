// This file is a part of the IncludeOS unikernel - www.includeos.org
//
// Copyright 2015 Oslo and Akershus University College of Applied Sciences
// and Alfred Bratterud
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <kernel/elf.hpp>
#include <cassert>
#include <cstdio>
#include <string>
#include <info>
#include <vector>
#include "../../vmbuild/elf.h"

static const char* null_stringz = "(null)";
static const uintptr_t ELF_START = 0x200000;
#define frp(N, ra)                                 \
  (__builtin_frame_address(N) != nullptr) &&       \
    (ra = __builtin_return_address(N)) != nullptr

extern "C" char *
__cxa_demangle(const char *name, char *buf, size_t *n, int *status);

template <typename N>
std::string to_hex_string(N n)
{
  std::string buffer; buffer.reserve(64);
  snprintf((char*) buffer.data(), buffer.capacity(), "%#x", n);
  return buffer;
}

struct SymTab {
  Elf32_Sym* base;
  uint32_t   entries;
};

class ElfTables
{
public:
  ElfTables(uintptr_t elf_base)
    : strtab(nullptr), ELF_BASE(elf_base)
  {
    auto& elf_hdr = elf_header();
    
    // enumerate all section headers
    auto* shdr = (Elf32_Shdr*) (ELF_BASE + elf_hdr.e_shoff);
    for (Elf32_Half i = 0; i < elf_hdr.e_shnum; i++)
    {
      switch (shdr[i].sh_type)
      {
      case SHT_SYMTAB:
        symtab[num_syms] = { (Elf32_Sym*) (ELF_BASE + shdr[i].sh_offset), 
                              shdr[i].sh_size / sizeof(Elf32_Sym) };
        num_syms++;
        //printf("found symtab at %#x\n", shdr[i].sh_offset);
        //debug("found symbol table at %p with %u entries\n", 
        //    this->symtab, this->st_entries);
        break;
      case SHT_STRTAB:
        this->strtab = (char*) (ELF_BASE + shdr[i].sh_offset);
        break;
      case SHT_DYNSYM:
      default:
        // don't care tbh
        break;
      }
    }
    if (num_syms == 0 || strtab == nullptr) {
      INFO("ELF", "symtab or strtab is empty, indicating image may be stripped\n");
    }
  }
  
  func_offset getsym(Elf32_Addr addr)
  {
    // probably just a null pointer with ofs=addr
    if (addr < 0x7c00) return {null_stringz, 0, addr};
    // definitely in the bootloader
    if (addr < 0x7e00) return {"Bootloader area", 0x7c00, addr - 0x7c00};
    // resolve manually from symtab
    auto* sym = getaddr(addr);
    if (sym) {
      auto base   = sym->st_value;
      auto offset = addr - base;
      // return string name for symbol
      return {demangle( sym_name(sym) ), base, offset};
    }
    // function or space not found
    return {to_hex_string(addr), addr, 0};
  }
  safe_func_offset getsym_safe(Elf32_Addr addr, char* buffer, size_t length)
  {
    // probably just a null pointer with ofs=addr
    if (addr < 0x7c00) return {null_stringz, 0, addr};
    // definitely in the bootloader
    if (addr < 0x7e00) return {0, 0x7c00, addr - 0x7c00};
    // resolve manually from symtab
    auto* sym = getaddr(addr);
    if (sym) {
      auto base   = sym->st_value;
      auto offset = addr - base;
      // return string name for symbol
      return {demangle_safe( sym_name(sym), buffer, length ), base, offset};
    }
    // function or space not found
    return {0, addr, 0};
  }
  
  Elf32_Addr getaddr(const std::string& name)
  {
    for (size_t t = 0; t < num_syms; t++)
    for (size_t i = 0; i < symtab[t].entries; i++) {
      
      //printf("sym %s\n", sym_name(&symtab[t].base[i]));
      if (demangle( sym_name(&symtab[t].base[i]) ) == name)
          return symtab[t].base[i].st_value;
      
    }
    return 0;
  }
  Elf32_Sym* getaddr(Elf32_Addr addr)
  {
    for (size_t t = 0; t < num_syms; t++)
    for (size_t i = 0; i < symtab[t].entries; i++) {
      
      if (addr >= symtab[t].base[i].st_value
      && (addr <  symtab[t].base[i].st_value + symtab[t].base[i].st_size))
          return &symtab[t].base[i];
    }
    return nullptr;
  }
  
  size_t end_of_file() const {
    auto& hdr = elf_header();
    return hdr.e_ehsize + (hdr.e_phnum * hdr.e_phentsize) + (hdr.e_shnum * hdr.e_shentsize);
  }

private:
  const char* sym_name(Elf32_Sym* sym) const {
    return &strtab[sym->st_name];
  }
  bool is_func(Elf32_Sym* sym) const
  {
    return ELF32_ST_TYPE(sym->st_info) == STT_FUNC;
  }
  std::string demangle(const char* name) const
  {
    if (name[0] == '_') {
      int status;
      // internally, demangle just returns buf when status is ok
      auto* res = __cxa_demangle(name, nullptr, 0, &status);
      if (status == 0) {
        std::string result(res);
        delete[] res;
        return result;
      }
    }
    return std::string(name);
  }
  const char* demangle_safe(const char* name, char* buffer, size_t buflen) const
  {
    int status;
    // internally, demangle just returns buf when status is ok
    auto* res = __cxa_demangle(name, (char*) buffer, &buflen, &status);
    if (status) return name;
    return res;
  }
  
  Elf32_Ehdr& elf_header() const {
    return *(Elf32_Ehdr*) ELF_BASE;
  }
  
  SymTab  symtab[4];
  size_t  num_syms;
  const char* strtab;
  uintptr_t   ELF_BASE;
};

ElfTables& get_parser() {
  static ElfTables parser(ELF_START);
  return parser;
}

size_t Elf::end_of_file() {
  return get_parser().end_of_file();
}

func_offset Elf::resolve_symbol(uintptr_t addr)
{
  return get_parser().getsym(addr);
}
func_offset Elf::resolve_symbol(void* addr)
{
  return get_parser().getsym((uintptr_t) addr);
}

safe_func_offset Elf::safe_resolve_symbol(void* addr, char* buffer, size_t length)
{
  return get_parser().getsym_safe((Elf32_Addr) addr, buffer, length);
}
uintptr_t Elf::resolve_name(const std::string& name)
{
  return get_parser().getaddr(name);
}

func_offset Elf::get_current_function()
{
  return resolve_symbol(__builtin_return_address(0));
}
std::vector<func_offset> Elf::get_functions()
{
  std::vector<func_offset> vec;
  #define ADD_TRACE(N, ra)                      \
      vec.push_back(Elf::resolve_symbol(ra));

  void* ra;
  if (frp(0, ra)) {
    ADD_TRACE(0, ra);
    if (frp(1, ra)) {
      ADD_TRACE(1, ra);
      if (frp(2, ra)) {
        ADD_TRACE(2, ra);
        if (frp(3, ra)) {
          ADD_TRACE(3, ra);
          if (frp(4, ra)) {
            ADD_TRACE(4, ra);
            if (frp(5, ra)) {
              ADD_TRACE(5, ra);
              if (frp(6, ra)) {
                ADD_TRACE(6, ra);
                if (frp(7, ra)) {
                  ADD_TRACE(7, ra);
                  if (frp(8, ra)) {
                    ADD_TRACE(8, ra);
  }}}}}}}}}
  return vec;
}

void print_backtrace()
{
  #define PRINT_TRACE(N, ra)                      \
    auto symb = Elf::resolve_symbol(ra);          \
    printf("[%d] %8p + 0x%.4x: %s\n",             \
        N, ra, symb.offset, symb.name.c_str());

  printf("\n");
  void* ra;
  if (frp(0, ra)) {
    PRINT_TRACE(0, ra);
    if (frp(1, ra)) {
      PRINT_TRACE(1, ra);
      if (frp(2, ra)) {
        PRINT_TRACE(2, ra);
        if (frp(3, ra)) {
          PRINT_TRACE(3, ra);
          if (frp(4, ra)) {
            PRINT_TRACE(4, ra);
            if (frp(5, ra)) {
              PRINT_TRACE(5, ra);
              if (frp(6, ra)) {
                PRINT_TRACE(6, ra);
                if (frp(7, ra)) {
                  PRINT_TRACE(7, ra);
                  if (frp(8, ra)) {
                    PRINT_TRACE(8, ra);
  }}}}}}}}}
}

extern "C" {
  uintptr_t __elf_header_end() {
    auto& hdr = *(Elf32_Ehdr*) ELF_START;
    uintptr_t last = 0;
    // find last SH, calculate offset + size
    auto* shdr = (Elf32_Shdr*) (ELF_START + hdr.e_shoff);
    for (Elf32_Half i = 0; i < hdr.e_shnum; i++)
    {
      uintptr_t size = shdr[i].sh_offset + shdr[i].sh_size;
      if (last < size) last = size;
    }
    // add base ELF address
    return ELF_START + last;
  }
}