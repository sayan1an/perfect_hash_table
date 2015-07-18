##**Brief description:**
This projects builds a perfect hash table that requires exactly two memory access to perform a lookup.

##**Limitations:**
1. Designed to load only upto 0x7fffffff number of distinct hashes. Duplicates are removed during build process.
2. Currently supported hash types are 32bit to 192bit. Althogh hash types can be easily etendend.

##**How to use:**
###1. Perform a lookup:   
offset_table_idx = search_hash % offset_table_size;   
offset = offset_table[offset_table_idx]; // first memory access.   
hash_table_idx = hash + offset;   
found_hash = hash_table[hash_table_idx]; // second memory access.

Look demo.c for more details.

###2. Loading the hases:
For 64bit or lower hashes should be loaded into an array of uint64_t.  
For 128bit or lower hashes should be loaded into an array of struct uint128_t(defined in interface.h).  
For 160bit/192bit hashes should be loaded into an array of struct uint192_t(defined in interface.h).

###3. Linking and using the repo:
include file 'interface.h' into your programs.   
See 'demo.c' for more details.

###4. Building and using demo.c:
gcc -g -O -c build_table.c twister.c hash_type_128.c hash_type_192.c -fopenmp   
gcc twister.o hash_type_192.o hash_type_128.o build_table.o  demo.c -o demo.out  -fopenmp   
./demo.out hash_list_file 128 // for loading 128 bit hashes or lower.   
./demo.out hash_list_file 192 // for loading 160bit or 192bit hashes.   

Building with Address sanitizer* for detecting memory issues:   
gcc -g -O -c build_table.c twister.c hash_type_64.c hash_type_128.c hash_type_192.c -fsanitize=address -fno-omit-frame-pointer -fopenmp   
gcc twister.o hash_type_192.o hash_type_128.o hash_type_64.o build_table.o  demo.c -o demo.out -fsanitize=address -fno-omit-frame-pointer -fopenmp   
./demo.out hash_list_file 128 // for loading 128 bit hashes or lower.   
./demo.out hash_list_file 192 // for loading 160bit or 192bit hashes.   

*Address sanitizer is available for gcc 4.8.0 or later




