/*
 * This software is Copyright (c) 2015 Sayantan Datta <std2048 at gmail dot com>
 * and it is hereby released to the general public under the following terms:
 * Redistribution and use in source and binary forms, with or without modification, are permitted.
 */

#include <stdlib.h>
#include <stdio.h>
#include "hash_types.h"

uint192_t *loaded_hashes_192 = NULL;
uint192_t *hash_table_192 = NULL;

/* Assuming N < 0x7fffffff */
inline unsigned int modulo192_31b(uint192_t a, unsigned int N, uint64_t shift64, uint64_t shift128)
{
	uint64_t p;
	p = (a.HI % N) * shift128;
	p += (a.MI % N) * shift64;
	p += a.LO % N;
	p %= N;
	return (unsigned int)p;
}

inline uint192_t add192(uint192_t a, unsigned int b)
{
	uint192_t result;
	result.LO = a.LO + b;
	result.MI = a.MI + (result.LO < a.LO);
	result.HI = a.HI + (result.MI < a.MI);
	if (result.HI < a.HI) {
		fprintf(stderr, "192 bit add overflow!!\n");
		exit(0);
	}

	return result;
}

void allocate_ht_192(unsigned int num_loaded_hashes)
{
	int i;

	if (posix_memalign((void **)&hash_table_192, 32, hash_table_size * sizeof(uint192_t))) {
		fprintf(stderr, "Couldn't allocate memory!!\n");
		exit(0);
	}

	for (i = 0; i < hash_table_size; i++)
		hash_table_192[i].HI = hash_table_192[i].MI = hash_table_192[i].LO = 0;

	total_memory_in_bytes += hash_table_size * sizeof(uint192_t);

	fprintf(stdout, "Hash Table Size %Lf %% of Number of Loaded Hashes.\n", ((long double)hash_table_size / (long double)num_loaded_hashes) * 100.00);
	fprintf(stdout, "Hash Table Size(in GBs):%Lf\n", ((long double)hash_table_size * sizeof(uint192_t)) / ((long double)1024 * 1024 * 1024));
}

inline unsigned int calc_ht_idx_192(unsigned int hash_location, unsigned int offset)
{
	return  modulo192_31b(add192(loaded_hashes_192[hash_location], offset), hash_table_size, shift64_ht_sz, shift128_ht_sz);
}

inline unsigned int zero_check_ht_192(unsigned int hash_table_idx)
{
	return ((hash_table_192[hash_table_idx].HI || hash_table_192[hash_table_idx].MI || hash_table_192[hash_table_idx].LO));
}

inline void assign_ht_192(unsigned int hash_table_idx, unsigned int hash_location)
{
	hash_table_192[hash_table_idx] = loaded_hashes_192[hash_location];
}

inline void assign0_ht_192(unsigned int hash_table_idx)
{
	hash_table_192[hash_table_idx].HI = hash_table_192[hash_table_idx].MI = hash_table_192[hash_table_idx].LO = 0;
}

unsigned int get_offset_192(unsigned int hash_table_idx, unsigned int hash_location)
{
	unsigned int z = modulo192_31b(loaded_hashes_192[hash_location], hash_table_size, shift64_ht_sz, shift128_ht_sz);
	return (hash_table_size - z + hash_table_idx);
}

void test_tables_192(unsigned int num_loaded_hashes, OFFSET_TABLE_WORD *offset_table, unsigned int offset_table_size, unsigned int shift64_ot_sz, unsigned int shift128_ot_sz)
{
	unsigned char *hash_table_collisions;
	unsigned int i, hash_table_idx, error = 1, count = 0;

	hash_table_collisions = (unsigned char *) calloc(hash_table_size, sizeof(unsigned char));

#pragma omp parallel private(i, hash_table_idx)
	{
#pragma omp for
		for (i = 0; i < num_loaded_hashes; i++) {
			hash_table_idx =
				calc_ht_idx_192(i,
					(unsigned int)offset_table[
					modulo192_31b(loaded_hashes_192[i],
					offset_table_size, shift64_ot_sz, shift128_ot_sz)]);
#pragma omp atomic
			hash_table_collisions[hash_table_idx]++;

			if (error && (hash_table_192[hash_table_idx].HI != loaded_hashes_192[i].HI ||
			    hash_table_192[hash_table_idx].MI != loaded_hashes_192[i].MI ||
			    hash_table_192[hash_table_idx].LO != loaded_hashes_192[i].LO ||
			    hash_table_collisions[hash_table_idx] > 1)) {
				fprintf(stderr, "Error building tables: Loaded hash Idx:%u, No. of Collosions:%u\n", i, hash_table_collisions[hash_table_idx]);
				error = 0;
			}

		}
#pragma omp single
		for (hash_table_idx = 0; hash_table_idx < hash_table_size; hash_table_idx++)
			if (zero_check_ht_192(hash_table_idx))
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