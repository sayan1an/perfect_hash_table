#include <inttypes.h>

#define OFFSET_TABLE_WORD unsigned int

typedef struct {
	uint64_t LO64;
	uint64_t HI64;
} uint128_t;

typedef struct {
	uint64_t LO;
	uint64_t MI;
	uint64_t HI;
} uint192_t;

extern unsigned int num_loaded_hashes;
extern unsigned int hash_table_size;
extern unsigned int shift64_ht_sz, shift128_ht_sz;
extern unsigned long long total_memory_in_bytes;

extern uint128_t *loaded_hashes_128;
extern uint128_t *hash_table_128;

extern unsigned int modulo128_31b(uint128_t, unsigned int, uint64_t);
extern void allocate_ht_128(void);
extern unsigned int calc_ht_idx_128(unsigned int, unsigned int);
extern unsigned int zero_check_ht_128(unsigned int);
extern void assign_ht_128(unsigned int, unsigned int);
extern void assign0_ht_128(unsigned int);
extern unsigned int get_offset_128(unsigned int, unsigned int);
extern void test_tables_128(OFFSET_TABLE_WORD *, unsigned int, unsigned int, unsigned int);
extern void load_hashes_128(void);

extern uint192_t *loaded_hashes_192;
extern uint192_t *hash_table_192;

extern unsigned int modulo192_31b(uint192_t, unsigned int, uint64_t, uint64_t);
extern void allocate_ht_192(void);
extern unsigned int calc_ht_idx_192(unsigned int, unsigned int);
extern unsigned int zero_check_ht_192(unsigned int);
extern void assign_ht_192(unsigned int, unsigned int);
extern void assign0_ht_192(unsigned int);
extern unsigned int get_offset_192(unsigned int, unsigned int);
extern void test_tables_192(OFFSET_TABLE_WORD *, unsigned int, unsigned int, unsigned int);
extern void load_hashes_160(void);
