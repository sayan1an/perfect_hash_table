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

extern uint128_t *hash_table_128;
extern uint192_t *hash_table_192;

extern void create_perfect_hash_table(int htype, void *loaded_hashes_ptr,
			       unsigned int num_ld_hashes,
			       OFFSET_TABLE_WORD *offset_table_ptr,
			       unsigned int *offset_table_sz_ptr,
			       unsigned int *hash_table_sz_ptr);