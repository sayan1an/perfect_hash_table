#include <stdlib.h>
#include <stdio.h>
#include "hash_types.h"

uint128_t *loaded_hashes_128 = NULL;
uint128_t *hash_table_128 = NULL;

/* Assuming N < 0x7fffffff */
inline unsigned int modulo128_31b(uint128_t a, unsigned int N, uint64_t shift64)
{
	uint64_t p;
	p = (a.HI64 % N) * shift64;
	p += (a.LO64 % N);
	p %= N;
	return (unsigned int)p;
}

inline uint128_t add128(uint128_t a, unsigned int b)
{
	uint128_t result;
	result.LO64 = a.LO64 + b;
	result.HI64 = a.HI64 + (result.LO64 < a.LO64);
	if (result.HI64 < a.HI64) {
		fprintf(stderr, "128 bit add overflow!!\n");
		exit(0);
	}

	return result;
}

void allocate_ht_128(unsigned int num_loaded_hashes)
{
	int i;

	if (posix_memalign((void **)&hash_table_128, 16, hash_table_size * sizeof(uint128_t))) {
		fprintf(stderr, "Couldn't allocate memory!!\n");
		exit(0);
	}

	for (i = 0; i < hash_table_size; i++)
		hash_table_128[i].HI64 = hash_table_128[i].LO64 = 0;

	total_memory_in_bytes += hash_table_size * sizeof(uint128_t);

	fprintf(stdout, "Hash Table Size %Lf %% of Number of Loaded Hashes.\n", ((long double)hash_table_size / (long double)num_loaded_hashes) * 100.00);
	fprintf(stdout, "Hash Table Size(in GBs):%Lf\n", ((long double)hash_table_size * sizeof(uint128_t)) / ((long double)1024 * 1024 * 1024));
}

inline unsigned int calc_ht_idx_128(unsigned int hash_location, unsigned int offset)
{
	return  modulo128_31b(add128(loaded_hashes_128[hash_location], offset), hash_table_size, shift64_ht_sz);
}

inline unsigned int zero_check_ht_128(unsigned int hash_table_idx)
{
	return ((hash_table_128[hash_table_idx].HI64 || hash_table_128[hash_table_idx].LO64));
}

inline void assign_ht_128(unsigned int hash_table_idx, unsigned int hash_location)
{
	hash_table_128[hash_table_idx] = loaded_hashes_128[hash_location];
}

inline void assign0_ht_128(unsigned int hash_table_idx)
{
	hash_table_128[hash_table_idx].HI64 = hash_table_128[hash_table_idx].LO64 = 0;
}

unsigned int get_offset_128(unsigned int hash_table_idx, unsigned int hash_location)
{
	unsigned int z = modulo128_31b(loaded_hashes_128[hash_location], hash_table_size, shift64_ht_sz);
	return (hash_table_size - z + hash_table_idx);
}

void test_tables_128(unsigned int num_loaded_hashes, OFFSET_TABLE_WORD *offset_table, unsigned int offset_table_size, unsigned int shift64_ot_sz, unsigned int shift128_ot_sz)
{
	unsigned char *hash_table_collisions;
	unsigned int i, hash_table_idx, error = 1, count = 0;

	hash_table_collisions = (unsigned char *) calloc(hash_table_size, sizeof(unsigned char));

#pragma omp parallel private(i, hash_table_idx)
	{
#pragma omp for
		for (i = 0; i < num_loaded_hashes; i++) {
			hash_table_idx =
				calc_ht_idx_128(i,
					(unsigned int)offset_table[
					modulo128_31b(loaded_hashes_128[i],
					offset_table_size, shift64_ot_sz)]);
#pragma omp atomic
			hash_table_collisions[hash_table_idx]++;

			if (error && (hash_table_128[hash_table_idx].HI64 != loaded_hashes_128[i].HI64 ||
			    hash_table_128[hash_table_idx].LO64 != loaded_hashes_128[i].LO64 ||
			    hash_table_collisions[hash_table_idx] > 1)) {
				fprintf(stderr, "Error building tables: Loaded hash Idx:%u, No. of Collosions:%u\n", i, hash_table_collisions[hash_table_idx]);
				error = 0;
			}

		}
#pragma omp single
		for (hash_table_idx = 0; hash_table_idx < hash_table_size; hash_table_idx++)
			if (zero_check_ht_128(hash_table_idx))
				count++;
#pragma omp barrier
	}

	if (count != num_loaded_hashes) {
		error = 0;
		fprintf(stderr, "Error!! Tables contains extra or less entries.\n");
	}

	free(hash_table_collisions);

	if (error)
		fprintf(stderr, "Tables TESTED OK\n");
}
