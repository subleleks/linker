/*
 * linker32.cpp
 *
 *  Created on: Jul 23, 2014
 *      Author: Pimenta
 */

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <set>
#include <map>
#include <string>

using namespace std;

typedef uint32_t uword_t;

#ifndef MEM_WORDS
#define MEM_WORDS 0x2000
#endif

#ifndef WORD_WIDTH
#define WORD_WIDTH 32
#endif

struct ObjectFile32 {
    uword_t offset;
    map<string, uword_t> exported;
    map<string, set<uword_t>> imported;
    set<uword_t> relative;
    uword_t mem_size;
    uword_t* mem;
    
    ~ObjectFile32() {
      delete[] mem;
    }
    
    void read(const char* fn, uword_t offs) {
      offset = offs;
      
      fstream f(fn, fstream::in | fstream::binary);
      
      uword_t tmp;
      
      // read number of exported symbols
      f.read((char*)&tmp, sizeof(uword_t));
      
      // read exported symbols
      for (uword_t i = 0; i < tmp; ++i) {
        string sym;
        uword_t addr;
        
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
        f.read((char*)&addr, sizeof(uword_t));
        
        exported[sym] = addr;
      }
      
      // read number of symbols of pending references
      f.read((char*)&tmp, sizeof(uword_t));
      
      // read symbols of pending references
      for (uword_t i = 0; i < tmp; ++i) {
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
        
        uword_t tmp2;
        
        // read number of references to current symbol
        f.read((char*)&tmp2, sizeof(uword_t));
        
        // read references to current symbol
        for (uword_t j = 0; j < tmp2; ++j) {
          uword_t addr;
          f.read((char*)&addr, sizeof(uword_t));
          imported[sym].emplace(addr);
        }
      }
      
      // read number of relative addresses
      f.read((char*)&tmp, sizeof(uword_t));
      
      // read relative addresses
      for (uword_t i = 0; i < tmp; ++i) {
        uword_t addr;
        f.read((char*)&addr, sizeof(uword_t));
        relative.emplace(addr);
      }
      
      // read assembled code size
      f.read((char*)&mem_size, sizeof(uword_t));
      
      // read assembled code
      mem = new uword_t[mem_size];
      f.read((char*)mem, sizeof(uword_t)*mem_size);
      
      f.close();
    }
};

int linker32(int argc, char* argv[]) {
  if (argc < 3) {
    fprintf(stderr, "Usage mode: subleq-ld <object_files...> <meminit_file>\n");
    return 0;
  }
  
  // read object files
  ObjectFile32 files[argc - 2];
  files[0].read(argv[1], 0);
  for (int i = 2; i < argc - 1; ++i)
    files[i].read(argv[i], files[i - 1].offset + files[i - 1].mem_size);
  
  uword_t mem_size = 0;
  uword_t* mem = new uword_t[MEM_WORDS];
  map<string, uword_t> symbols;
  map<string, set<uword_t>> references;
  set<uword_t> relatives;
  
  for (auto& file : files) {
    // assemble global symbol table
    for (auto& sym : file.exported) {
      symbols[sym.first] = sym.second + file.offset;
    }
    
    // assemble global reference table
    for (auto& sym : file.imported) {
      set<uword_t>& refs = references[sym.first];
      for (auto addr : sym.second) {
        refs.emplace(addr + file.offset);
      }
    }
    
    // assemble global relative address table
    for (auto addr : file.relative) {
      relatives.emplace(addr + file.offset);
      file.mem[addr] += file.offset; // relocating
    }
    
    // copy object code
    memcpy(&mem[mem_size], file.mem, file.mem_size*sizeof(uword_t));
    mem_size += file.mem_size;
  }
  
  // solve references
  for (auto ref = references.begin(); ref != references.end();) {
    auto sym = symbols.find(ref->first);
    if (sym == symbols.end()) {
      ref++;
    }
    else {
      for (auto addr : ref->second) {
        mem[addr] = sym->second;
      }
      references.erase(ref++);
    }
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
  for (uword_t i = 0; i < mem_size; ++i) {
    sprintf(buf, "%08x", i);
    f << buf;
    sprintf(buf, "%08x", mem[i]);
    f << " : " << buf << ";\n";
  }
  f << "\n";
  f << "END;\n";
  f.close();
  
  delete[] mem;
  
  return 0;
}
