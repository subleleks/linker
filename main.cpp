/*
 * main.cpp
 *
 *  Created on: Apr 12, 2014
 *      Author: Pimenta
 */

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <list>
#include <map>
#include <string>
#include <utility>

using namespace std;

typedef int64_t  word_t;
typedef uint64_t uword_t;
typedef int32_t  address_t;

enum field_t {
  A, B, J
};

#ifndef MEM_WORDS
#define MEM_WORDS 8192
#endif

#ifndef WORD_WIDTH
#define WORD_WIDTH 64
#endif

const address_t ADDRESS_MASK  = MEM_WORDS - 1;
const uword_t   ADDRESS_WIDTH = log2(MEM_WORDS);
const uword_t   A_MASK        = ~(((uword_t)(MEM_WORDS - 1)) << 2*ADDRESS_WIDTH);
const uword_t   B_MASK        = ~(((uword_t)(MEM_WORDS - 1)) << 1*ADDRESS_WIDTH);
const uword_t   J_MASK        = ~(((uword_t)(MEM_WORDS - 1)) << 0*ADDRESS_WIDTH);

struct ObjectFile {
    address_t offset;
    map<string, address_t> exported;
    map<string, list<pair<address_t, field_t>>> imported;
    address_t mem_size;
    uword_t* mem;
    
    ~ObjectFile() {
      delete[] mem;
    }
    
    void initStart() {
      offset = 0;
      imported["start"].push_back(pair<address_t, field_t>(0, J));
      mem_size = 2;
      mem = new uword_t[2];
      mem[0] = 0x0000000004002000;
      mem[1] = 0x0000000000000000;
    }
    
    void read(const char* fn, address_t offs) {
      offset = offs;
      
      ifstream f(fn);
      
      uint32_t tmp1, tmp2;
      
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
        for (uint32_t i = 0; i < tmp2; ++i) {
          address_t addr;
          field_t field;
          
          // address
          f.read((char*)&addr, sizeof(uint32_t));
          
          // field
          f.read((char*)&field, sizeof(uint32_t));
          
          imported[sym].push_back(pair<address_t, field_t>(addr, field));
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

int main(int argc, char* argv[]) {
  if (argc < 3) {
    fprintf(stderr, "Usage mode: subleq-ld <subleq_object_files...> <mem_init_file>\n");
    return 0;
  }
  
  // read object files
  ObjectFile files[argc - 1];
  files[0].initStart();
  for (int i = 1; i < argc - 1; ++i)
    files[i].read(argv[i], files[i - 1].offset + files[i - 1].mem_size);
  
  // assemble global symbol table
  map<string, address_t> symbols;
  for (auto& file : files) {
    for (auto& sym : file.exported)
      symbols[sym.first] = sym.second + file.offset;
  }
  
  // link
  address_t ip = 0;
  uword_t mem[MEM_WORDS];
  for (auto& file : files) {
    // solve pendencies for this file
    for (auto& sym : file.imported) {
      for (auto& ref : sym.second) {
        switch (ref.second) {
          case A:
            file.mem[ref.first] &= A_MASK;
            file.mem[ref.first] |= ((uword_t)((symbols[sym.first] - (ref.first + file.offset)) & ADDRESS_MASK)) << 2*ADDRESS_WIDTH;
            break;
          case B:
            file.mem[ref.first] &= B_MASK;
            file.mem[ref.first] |= ((uword_t)((symbols[sym.first] - (ref.first + file.offset)) & ADDRESS_MASK)) << 1*ADDRESS_WIDTH;
            break;
          case J:
            file.mem[ref.first] &= J_MASK;
            file.mem[ref.first] |= ((uword_t)((symbols[sym.first] - (ref.first + file.offset)) & ADDRESS_MASK)) << 0*ADDRESS_WIDTH;
            break;
        }
      }
    }
    
    // copy mem
    for (address_t i = 0; i < file.mem_size; ++i)
      mem[ip++] = file.mem[i];
  }
  
  // output mif
  fstream f(argv[argc - 1], fstream::out | fstream::binary);
  char buf[20];
  f << "DEPTH = " << MEM_WORDS << ";\n";
  f << "WIDTH = " << WORD_WIDTH << ";\n";
  f << "ADDRESS_RADIX = HEX;\n";
  f << "DATA_RADIX = HEX;\n";
  f << "CONTENT\n";
  f << "BEGIN\n";
  f << "\n";
  for (address_t i = 0; i < ip; ++i) {
    sprintf(buf, "%08x", i);
    f << buf;
    sprintf(buf, "%016llx", mem[i]);
    f << " : " << buf << ";\n";
  }
  f << "\n";
  f << "END;\n";
  f.close();
  
  return 0;
}
