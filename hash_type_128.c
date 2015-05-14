/*
 * This software is Copyright (c) 2015 Sayantan Datta <std2048 at gmail dot com>
 * and it is hereby released to the general public under the following terms:
 * Redistribution and use in source and binary forms, with or without modification, are permitted.
 */

#include <stdlib.h>
#include <stdio.h>
#include "hash_types.h"

uint128_t *loaded_hashes_128 = NULL;
unsigned int *hash_table_128 = NULL;

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

	if (posix_memalign((void **)&hash_table_128, 16, 4 * hash_table_size * sizeof(unsigned int))) {
		fprintf(stderr, "Couldn't allocate memory!!\n");
		exit(0);
	}

	for (i = 0; i < hash_table_size; i++)
		hash_table_128[i] = hash_table_128[i + hash_table_size]
			= hash_table_128[i + 2 * hash_table_size]
			= hash_table_128[i + 3 * hash_table_size] = 0;

	total_memory_in_bytes += 4 * hash_table_size * sizeof(unsigned int);

	fprintf(stdout, "Hash Table Size %Lf %% of Number of Loaded Hashes.\n", ((long double)hash_table_size / (long double)num_loaded_hashes) * 100.00);
	fprintf(stdout, "Hash Table Size(in GBs):%Lf\n", ((long double)4.0 * hash_table_size * sizeof(unsigned int)) / ((long double)1024 * 1024 * 1024));
}

inline unsigned int calc_ht_idx_128(unsigned int hash_location, unsigned int offset)
{
	return  modulo128_31b(add128(loaded_hashes_128[hash_location], offset), hash_table_size, shift64_ht_sz);
}

inline unsigned int zero_check_ht_128(unsigned int hash_table_idx)
{
	return ((hash_table_128[hash_table_idx] || hash_table_128[hash_table_idx + hash_table_size] ||
		hash_table_128[hash_table_idx + 2 * hash_table_size] ||
		hash_table_128[hash_table_idx + 3 * hash_table_size]));
}

inline void assign_ht_128(unsigned int hash_table_idx, unsigned int hash_location)
{
	uint128_t hash = loaded_hashes_128[hash_location];
	hash_table_128[hash_table_idx] = (unsigned int)(hash.LO64 & 0xffffffff);
	hash_table_128[hash_table_idx + hash_table_size] = (unsigned int)(hash.LO64 >> 32);
	hash_table_128[hash_table_idx + 2 * hash_table_size] = (unsigned int)(hash.HI64 & 0xffffffff);
	hash_table_128[hash_table_idx + 3 * hash_table_size] = (unsigned int)(hash.HI64 >> 32);
}

inline void assign0_ht_128(unsigned int hash_table_idx)
{
	hash_table_128[hash_table_idx] = hash_table_128[hash_table_idx + hash_table_size]
			= hash_table_128[hash_table_idx + 2 * hash_table_size]
			= hash_table_128[hash_table_idx + 3 * hash_table_size] = 0;
}

unsigned int get_offset_128(unsigned int hash_table_idx, unsigned int hash_location)
{
	unsigned int z = modulo128_31b(loaded_hashes_128[hash_location], hash_table_size, shift64_ht_sz);
	return (hash_table_size - z + hash_table_idx);
}

int test_tables_128(unsigned int num_loaded_hashes, OFFSET_TABLE_WORD *offset_table, unsigned int offset_table_size, unsigned int shift64_ot_sz, unsigned int shift128_ot_sz)
{
	unsigned char *hash_table_collisions;
	unsigned int i, hash_table_idx, error = 1, count = 0;
	uint128_t hash;

	hash_table_collisions = (unsigned char *) calloc(hash_table_size, sizeof(unsigned char));

#pragma omp parallel private(i, hash_table_idx, hash)
	{
#pragma omp for
		for (i = 0; i < num_loaded_hashes; i++) {
			hash = loaded_hashes_128[i];
			hash_table_idx =
				calc_ht_idx_128(i,
					(unsigned int)offset_table[
					modulo128_31b(hash,
					offset_table_size, shift64_ot_sz)]);
#pragma omp atomic
			hash_table_collisions[hash_table_idx]++;

			if (error && (hash_table_128[hash_table_idx] != (unsigned int)(hash.LO64 & 0xffffffff)  ||
			    hash_table_128[hash_table_idx + hash_table_size] != (unsigned int)(hash.LO64 >> 32) ||
			    hash_table_128[hash_table_idx + 2 * hash_table_size] != (unsigned int)(hash.HI64 & 0xffffffff) ||
			    hash_table_128[hash_table_idx + 3 * hash_table_size] != (unsigned int)(hash.HI64 >> 32) ||
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
		return 0;
	}

	free(hash_table_collisions);

	if (error)
		fprintf(stdout, "Tables TESTED OK\n");

	return 1;
}

unsigned int remove_duplicates_128(unsigned int num_loaded_hashes, unsigned int hash_table_size)
{
	unsigned int i, num_unique_hashes, **hash_location_list, counter;
#define COLLISION_DTYPE unsigned short
	COLLISION_DTYPE *collisions;
	typedef struct {
		unsigned int store_loc1;
		unsigned int store_loc2;
		COLLISION_DTYPE  collisions;
		COLLISION_DTYPE iter;
		unsigned int idx_hash_loc_list;
	} hash_table_data;

	if (hash_table_size & (hash_table_size - 1)) {
		fprintf(stderr, "Duplicate removal hash table size must power of 2.\n");
		return 0;
	}

	hash_table_data *hash_table = (hash_table_data *) malloc(hash_table_size * sizeof(hash_table_data));
	collisions = (COLLISION_DTYPE *) calloc(hash_table_size, sizeof(COLLISION_DTYPE));

	for (i = 0; i < num_loaded_hashes; i++) {
		unsigned int idx = loaded_hashes_128[i].LO64 & (hash_table_size - 1);
		collisions[idx]++;
	}

	counter = 0;
	for (i = 0; i < hash_table_size; i++) {
		 hash_table[i].collisions = collisions[i];
		 hash_table[i].iter = 0;
		 hash_table[i].store_loc1 = hash_table[i].store_loc2 =
			hash_table[i].idx_hash_loc_list = 0xffffffff;
		if (hash_table[i].collisions > 3)
			hash_table[i].idx_hash_loc_list = counter++;
	}

	hash_location_list = (unsigned int **) malloc(counter * sizeof(unsigned int *));

	counter = 0;
	for (i = 0; i < hash_table_size; i++)
	      if (collisions[i] > 3)
			hash_location_list[counter++] = (unsigned int *) malloc((collisions[i] - 1) * sizeof(unsigned int));

	for (i = 0; i < num_loaded_hashes; i++) {
		unsigned int idx = loaded_hashes_128[i].LO64 & (hash_table_size - 1);

		if (collisions[idx] > 1) {
			if (hash_table[idx].collisions == 2) {
				if (!hash_table[idx].iter) {
					hash_table[idx].iter++;
					hash_table[idx].store_loc1 = i;
				}
				else if (loaded_hashes_128[hash_table[idx].store_loc1].LO64 == loaded_hashes_128[i].LO64 &&
					loaded_hashes_128[hash_table[idx].store_loc1].HI64 == loaded_hashes_128[i].HI64)
					loaded_hashes_128[i].LO64 = loaded_hashes_128[i].HI64 = 0;
			}
			else if (hash_table[idx].collisions == 3) {
				if (!hash_table[idx].iter) {
					hash_table[idx].iter++;
					hash_table[idx].store_loc1 = i;
				}
				else if (hash_table[idx].iter == 1) {
					if (loaded_hashes_128[hash_table[idx].store_loc1].LO64 == loaded_hashes_128[i].LO64 &&
					    loaded_hashes_128[hash_table[idx].store_loc1].HI64 == loaded_hashes_128[i].HI64)
						loaded_hashes_128[i].LO64 = loaded_hashes_128[i].HI64 = 0;
					else
						hash_table[idx].store_loc2 = i;
				}
				else if ((loaded_hashes_128[hash_table[idx].store_loc1].LO64 == loaded_hashes_128[i].LO64 &&
					  loaded_hashes_128[hash_table[idx].store_loc1].HI64 == loaded_hashes_128[i].HI64) ||
					  (loaded_hashes_128[hash_table[idx].store_loc2].LO64 == loaded_hashes_128[i].LO64 &&
					   loaded_hashes_128[hash_table[idx].store_loc2].HI64 == loaded_hashes_128[i].HI64))
						loaded_hashes_128[i].LO64 = loaded_hashes_128[i].HI64 = 0;
			}
			else {
				unsigned int iter = hash_table[idx].iter;

				if (!iter)
					hash_location_list[hash_table[idx].idx_hash_loc_list][iter++] = i;

				else {
					unsigned int j;
					for (j = 0; j < iter; j++)
						if (loaded_hashes_128[hash_location_list[hash_table[idx].idx_hash_loc_list][j]].LO64 == loaded_hashes_128[i].LO64 &&
						    loaded_hashes_128[hash_location_list[hash_table[idx].idx_hash_loc_list][j]].HI64 == loaded_hashes_128[i].HI64) {
							loaded_hashes_128[i].LO64 = loaded_hashes_128[i].HI64 = 0;
							break;
						}
					if (j == iter && iter < hash_table[idx].collisions - 1)
						hash_location_list[hash_table[idx].idx_hash_loc_list][iter++] = i;
				}
				hash_table[idx].iter = iter;
			}

		}
	}

	for (i = num_loaded_hashes - 1; i >= 0; i--)
		if (loaded_hashes_128[i].LO64 || loaded_hashes_128[i].HI64) {
			num_unique_hashes = i;
			break;
		}

	for (i = 0; i <= num_unique_hashes; i++)
		if (loaded_hashes_128[i].LO64 == 0 && loaded_hashes_128[i].HI64 == 0) {
			unsigned int j;
			loaded_hashes_128[i] = loaded_hashes_128[num_unique_hashes];
			loaded_hashes_128[num_unique_hashes].LO64 = loaded_hashes_128[num_unique_hashes].HI64 = 0;
			num_unique_hashes--;
			for (j = num_unique_hashes; j >= 0; j--)
				if (loaded_hashes_128[j].LO64 || loaded_hashes_128[j].HI64) {
					num_unique_hashes = j;
					break;
				}
		}
#undef COLLISION_DTYPE
	for (i = 0; i < counter; i++)
		free(hash_location_list[i]);
	free(hash_location_list);
	free(hash_table);
	free(collisions);
	fprintf(stderr, "NUM UNIQUE HASHES:%u\n", num_unique_hashes + 1);
	return (num_unique_hashes + 1);
}