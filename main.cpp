/*
 * main.cpp
 *
 *  Created on: Apr 12, 2014
 *      Author: Pimenta
 */

int linker32(int argc, char* argv[]);
int linker64(int argc, char* argv[]);

int main(int argc, char* argv[]) {
  return linker32(argc, argv);
}
