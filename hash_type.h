#include <inttypes.h>

#define OFFSET_TABLE_WORD unsigned int

typedef struct {
	uint64_t LO64;
	uint64_t HI64;
} uint128_t;

extern uint128_t *loaded_hashes_128;
extern unsigned int num_loaded_hashes;
extern uint128_t *hash_table_128;
extern unsigned int hash_table_size;
extern unsigned int shift64_ht_sz;
extern unsigned long long total_memory_in_bytes;

extern unsigned int modulo128_64b(uint128_t, unsigned int, uint64_t);
extern void allocate_ht_128(void);
extern unsigned int calc_ht_idx_128(unsigned int, unsigned int);
extern unsigned int zero_check_ht_128(unsigned int);
extern void assign_ht_128(unsigned int, unsigned int);
extern void assign0_ht_128(unsigned int);
extern unsigned int get_offset_128(unsigned int, unsigned int);
extern void test_tables_128(OFFSET_TABLE_WORD *, unsigned int, unsigned int);
extern void load_hashes_128(void);
