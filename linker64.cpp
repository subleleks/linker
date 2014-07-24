/*
 * linker64.cpp
 *
 *  Created on: Jul 23, 2014
 *      Author: Pimenta
 */

#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <list>
#include <map>
#include <string>

using namespace std;

typedef uint64_t uword_t;
typedef uint32_t address_t;

enum field_t {
  A, B, J
};

struct InstrAbsAddresses {
  bool a, b, j;
  InstrAbsAddresses() : a(false), b(false), j(false) {}
};

#ifndef MEM_WORDS
#define MEM_WORDS 0x2000
#endif

#ifndef WORD_WIDTH
#define WORD_WIDTH 64
#endif

static const address_t ADDRESS_MASK  = MEM_WORDS - 1;
static const uword_t   ADDRESS_WIDTH = log2(MEM_WORDS);
static const uword_t   A_MASK        = ~(uword_t(ADDRESS_MASK) << 2*ADDRESS_WIDTH);
static const uword_t   B_MASK        = ~(uword_t(ADDRESS_MASK) << 1*ADDRESS_WIDTH);
static const uword_t   J_MASK        = ~(uword_t(ADDRESS_MASK) << 0*ADDRESS_WIDTH);

struct ObjectFile64 {
    address_t offset;
    address_t text_offset;
    map<string, address_t> exported;
    map<string, list<pair<address_t, field_t>>> imported;
    map<address_t, InstrAbsAddresses> absolute;
    address_t mem_size;
    uword_t* mem;
    
    ~ObjectFile64() {
      delete[] mem;
    }
    
    void initStart() {
      offset = 0;
      text_offset = 0;
      imported["start"].emplace_back(0, J);
      mem_size = 2;
      mem = new uword_t[2];
      mem[0] = 0x0000000004002000;
      mem[1] = 0x0000000000000000;
    }
    
    void read(const char* fn, address_t offs) {
      offset = offs;
      
      fstream f(fn, fstream::in | fstream::binary);
      
      uint32_t tmp1, tmp2;
      
      // read text section offset
      f.read((char*)&text_offset, sizeof(address_t));
      
      // read number of exported symbols
      f.read((char*)&tmp1, sizeof(uint32_t));
      
      // read exported symbols
      for (uint32_t i = 0; i < tmp1; ++i) {
        string sym;
        uint32_t addr;
        
        // string
        {
          char c;
          f.read(&c, sizeof(char));
          while (c != '\0') {
            sym += c;
            f.read(&c, sizeof(char));
          }
        }
        
        // address
        f.read((char*)&addr, sizeof(uint32_t));
        
        exported[sym] = addr;
      }
      
      // read number of symbols of pending references
      f.read((char*)&tmp1, sizeof(uint32_t));
      
      // read symbols of pending references
      for (uint32_t i = 0; i < tmp1; ++i) {
        string sym;
        
        // string
        {
          char c;
          f.read(&c, sizeof(char));
          while (c != '\0') {
            sym += c;
            f.read(&c, sizeof(char));
          }
        }
        
        // read number of references to current symbol
        f.read((char*)&tmp2, sizeof(uint32_t));
        
        // read references to current symbol
        for (uint32_t j = 0; j < tmp2; ++j) {
          address_t addr;
          field_t field;
          
          // address
          f.read((char*)&addr, sizeof(uint32_t));
          
          // field
          f.read((char*)&field, sizeof(uint32_t));
          
          imported[sym].emplace_back(addr, field);
        }
      }
      
      // read number of absolute addresses
      f.read((char*)&tmp1, sizeof(uint32_t));
      
      // read absolute addresses
      for (uint32_t i = 0; i < tmp1; ++i) {
        address_t addr;
        field_t field;
        
        // address
        f.read((char*)&addr, sizeof(uint32_t));
        
        // field
        f.read((char*)&field, sizeof(uint32_t));
        
        switch (field) {
          case A: absolute[addr].a = true; break;
          case B: absolute[addr].b = true; break;
          case J: absolute[addr].j = true; break;
        }
      }
      
      // read assembled code size
      f.read((char*)&mem_size, sizeof(address_t));
      
      // read assembled code
      mem = new uword_t[mem_size];
      f.read((char*)mem, sizeof(uword_t)*mem_size);
      
      f.close();
    }
};

inline static void relocate(uword_t& instr, field_t field, address_t file_offset) {
  int mult = field == A ? 2 : (field == B ? 1 : 0);
  uword_t mask = field == A ? A_MASK : (field == B ? B_MASK : J_MASK);
  address_t tmp = ((instr >> mult*ADDRESS_WIDTH) & ADDRESS_MASK) + file_offset;
  instr &= mask;
  instr |= (uword_t(tmp) << mult*ADDRESS_WIDTH);
}

int linker64(int argc, char* argv[]) {
  if (argc < 3) {
    fprintf(stderr, "Usage mode: subleq-ld <object_files...> <meminit_file>\n");
    return 0;
  }
  
  // read object files
  ObjectFile64 files[argc - 1];
  files[0].initStart();
  for (int i = 1; i < argc - 1; ++i)
    files[i].read(argv[i], files[i - 1].offset + files[i - 1].mem_size);
  
  // assemble global symbol table
  map<string, address_t> symbols;
  for (auto& file : files) {
    for (auto& sym : file.exported) {
      symbols[sym.first] = sym.second + file.offset;
    }
  }
  
  // link
  address_t mem_size = 0;
  uword_t* mem = new uword_t[MEM_WORDS];
  for (auto& file : files) {
    // relocate addresses
    for (address_t i = file.text_offset; i < file.mem_size; ++i) {
      InstrAbsAddresses absAddr;
      auto it = file.absolute.find(i);
      if (it != file.absolute.end()) {
        absAddr = it->second;
      }
      uword_t& instr = file.mem[i];
      if (!absAddr.a) {
        relocate(instr, A, file.offset);
      }
      if (!absAddr.b) {
        relocate(instr, B, file.offset);
      }
      if (!absAddr.j) {
        relocate(instr, J, file.offset);
      }
    }
    
    // solve pendencies for this file
    for (auto& sym : file.imported) {
      uword_t sym_addr = uword_t(symbols[sym.first]);
      for (auto& ref : sym.second) {
        uword_t& instr = file.mem[ref.first];
        switch (ref.second) {
          case A:
            instr &= A_MASK;
            instr |= (sym_addr << 2*ADDRESS_WIDTH);
            break;
          case B:
            instr &= B_MASK;
            instr |= (sym_addr << 1*ADDRESS_WIDTH);
            break;
          case J:
            instr &= J_MASK;
            instr |= (sym_addr << 0*ADDRESS_WIDTH);
            break;
        }
      }
    }
    
    // copy mem
    memcpy(&mem[mem_size], file.mem, file.mem_size*sizeof(uword_t));
    mem_size += file.mem_size;
  }
  
  // output mif
  fstream f(argv[argc - 1], fstream::out);
  char buf[20];
  f << "DEPTH = " << MEM_WORDS << ";\n";
  f << "WIDTH = " << WORD_WIDTH << ";\n";
  f << "ADDRESS_RADIX = HEX;\n";
  f << "DATA_RADIX = HEX;\n";
  f << "CONTENT\n";
  f << "BEGIN\n";
  f << "\n";
  for (address_t i = 0; i < mem_size; ++i) {
    sprintf(buf, "%08x", i);
    f << buf;
    sprintf(buf, "%016llx", mem[i]);
    f << " : " << buf << ";\n";
  }
  f << "\n";
  f << "END;\n";
  f.close();
  
  delete[] mem;
  
  return 0;
}
