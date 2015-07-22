/*
 * This software is Copyright (c) 2015 Sayantan Datta <std2048 at gmail dot com>
 * and it is hereby released to the general public under the following terms:
 * Redistribution and use in source and binary forms, with or without modification, are permitted.
 */

#include <stdlib.h>
#include <stdio.h>
#include "bt_hash_types.h"

uint192_t *loaded_hashes_192 = NULL;
unsigned int *hash_table_192 = NULL;

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
	if (result.HI < a.HI)
		bt_error("192 bit add overflow.");

	return result;
}

void allocate_ht_192(unsigned int num_loaded_hashes, unsigned int verbosity)
{
	unsigned int i;

	if (bt_memalign_alloc((void **)&hash_table_192, 32, 6 * hash_table_size * sizeof(unsigned int)))
		bt_error("Couldn't allocate hash_table_192.");

	for (i = 0; i < hash_table_size; i++)
		hash_table_192[i] = hash_table_192[i + hash_table_size] = hash_table_192[i + 2 * hash_table_size] =
		hash_table_192[i + 3 * hash_table_size] = hash_table_192[i + 4 * hash_table_size] =
		hash_table_192[i + 5 * hash_table_size] = 0;

	total_memory_in_bytes += 6 * hash_table_size * sizeof(unsigned int);

	if (verbosity > 2) {
		fprintf(stdout, "Hash Table Size %Lf %% of Number of Loaded Hashes.\n", ((long double)hash_table_size / (long double)num_loaded_hashes) * 100.00);
		fprintf(stdout, "Hash Table Size(in GBs):%Lf\n", ((long double)6 * hash_table_size * sizeof(unsigned int)) / ((long double)1024 * 1024 * 1024));
	}
}

inline unsigned int calc_ht_idx_192(unsigned int hash_location, unsigned int offset)
{
	return  modulo192_31b(add192(loaded_hashes_192[hash_location], offset), hash_table_size, shift64_ht_sz, shift128_ht_sz);
}

inline unsigned int zero_check_ht_192(unsigned int hash_table_idx)
{
	return (hash_table_192[hash_table_idx] || hash_table_192[hash_table_idx + hash_table_size] ||
		hash_table_192[hash_table_idx + 2 * hash_table_size] || hash_table_192[hash_table_idx + 3 * hash_table_size] ||
		hash_table_192[hash_table_idx + 4 * hash_table_size] || hash_table_192[hash_table_idx + 5 * hash_table_size]);
}

inline void assign_ht_192(unsigned int hash_table_idx, unsigned int hash_location)
{
	uint192_t hash = loaded_hashes_192[hash_location];
	hash_table_192[hash_table_idx] = (unsigned int)(hash.LO & 0xffffffff);
	hash_table_192[hash_table_idx + hash_table_size] = (unsigned int)(hash.LO >> 32);
	hash_table_192[hash_table_idx + 2 * hash_table_size] = (unsigned int)(hash.MI & 0xffffffff);
	hash_table_192[hash_table_idx + 3 * hash_table_size] = (unsigned int)(hash.MI >> 32);
	hash_table_192[hash_table_idx + 4 * hash_table_size] = (unsigned int)(hash.HI & 0xffffffff);
	hash_table_192[hash_table_idx + 5 * hash_table_size] = (unsigned int)(hash.HI >> 32);
}

inline void assign0_ht_192(unsigned int hash_table_idx)
{
	hash_table_192[hash_table_idx] = hash_table_192[hash_table_idx + hash_table_size] = hash_table_192[hash_table_idx + 2 * hash_table_size] =
		hash_table_192[hash_table_idx + 3 * hash_table_size] = hash_table_192[hash_table_idx + 4 * hash_table_size] =
		hash_table_192[hash_table_idx + 5 * hash_table_size] = 0;
}

unsigned int get_offset_192(unsigned int hash_table_idx, unsigned int hash_location)
{
	unsigned int z = modulo192_31b(loaded_hashes_192[hash_location], hash_table_size, shift64_ht_sz, shift128_ht_sz);
	return (hash_table_size - z + hash_table_idx);
}

int test_tables_192(unsigned int num_loaded_hashes, OFFSET_TABLE_WORD *offset_table, unsigned int offset_table_size, unsigned int shift64_ot_sz, unsigned int shift128_ot_sz, unsigned int verbosity)
{
	unsigned char *hash_table_collisions;
	unsigned int i, hash_table_idx, error = 1, count = 0;
	uint192_t hash;

	if (verbosity > 1)
		fprintf(stdout, "\nTesting Tables...");

	if (bt_calloc((void **)&hash_table_collisions, hash_table_size, sizeof(unsigned char)))
		bt_error("Failed to allocate memory: hash_table_collisions.");

#if _OPENMP
#pragma omp parallel private(i, hash_table_idx, hash)
#endif
	{
#if _OPENMP
#pragma omp for
#endif
		for (i = 0; i < num_loaded_hashes; i++) {
			hash = loaded_hashes_192[i];
			hash_table_idx =
				calc_ht_idx_192(i,
					(unsigned int)offset_table[
					modulo192_31b(hash,
					offset_table_size, shift64_ot_sz, shift128_ot_sz)]);
#if _OPENMP
#pragma omp atomic
#endif
			hash_table_collisions[hash_table_idx]++;

			if (error && (hash_table_192[hash_table_idx] != (unsigned int)(hash.LO & 0xffffffff) ||
				hash_table_192[hash_table_idx + hash_table_size] != (unsigned int)(hash.LO >> 32) ||
				hash_table_192[hash_table_idx + 2 * hash_table_size] != (unsigned int)(hash.MI & 0xffffffff) ||
				hash_table_192[hash_table_idx + 3 * hash_table_size] != (unsigned int)(hash.MI >> 32) ||
				hash_table_192[hash_table_idx + 4 * hash_table_size] != (unsigned int)(hash.HI & 0xffffffff) ||
				hash_table_192[hash_table_idx + 5 * hash_table_size] != (unsigned int)(hash.HI >> 32) ||
				hash_table_collisions[hash_table_idx] > 1)) {
				fprintf(stderr, "Error building tables: Loaded hash Idx:%u, No. of Collosions:%u\n", i, hash_table_collisions[hash_table_idx]);
				error = 0;
			}

		}
#if _OPENMP
#pragma omp single
#endif
		for (hash_table_idx = 0; hash_table_idx < hash_table_size; hash_table_idx++)
			if (zero_check_ht_192(hash_table_idx))
				count++;
#if _OPENMP
#pragma omp barrier
#endif
	}

	if (count != num_loaded_hashes) {
		error = 0;
		fprintf(stderr, "Error!! Tables contains extra or less entries.\n");
		return 0;
	}

	bt_free((void **)&hash_table_collisions);

	if (error && verbosity > 1)
		fprintf(stdout, "OK\n");

	return 1;
}

#define check_equal(p, q) \
	(loaded_hashes_192[p].LO == loaded_hashes_192[q].LO &&	\
	 loaded_hashes_192[p].MI == loaded_hashes_192[q].MI &&	\
	 loaded_hashes_192[p].HI == loaded_hashes_192[q].HI)

#define check_non_zero(p) \
	(loaded_hashes_192[p].LO || loaded_hashes_192[p].MI || loaded_hashes_192[p].HI)

#define check_zero(p) \
	(loaded_hashes_192[p].LO == 0 && loaded_hashes_192[p].MI == 0 && loaded_hashes_192[p].HI == 0)

#define set_zero(p) \
	loaded_hashes_192[p].LO = loaded_hashes_192[p].MI = loaded_hashes_192[p].HI = 0

static void remove_duplicates_final(unsigned int num_loaded_hashes, unsigned int hash_table_size, unsigned int *rehash_list)
{
	unsigned int i, **hash_location_list, counter;
#define COLLISION_DTYPE unsigned int
	COLLISION_DTYPE *collisions;
	typedef struct {
		unsigned int store_loc1;
		unsigned int store_loc2;
		unsigned int idx_hash_loc_list;
		COLLISION_DTYPE  collisions;
		COLLISION_DTYPE iter;
	} hash_table_data;

	hash_table_data *hash_table = NULL;

	if (bt_malloc((void **)&hash_table, hash_table_size * sizeof(hash_table_data)))
		bt_error("Failed to allocate memory: hash_table.");
	if (bt_calloc((void **)&collisions, hash_table_size, sizeof(COLLISION_DTYPE)))
		bt_error("Failed to allocate memory: collisions.");

	for (i = 0; i < num_loaded_hashes; i++) {
		unsigned int idx = loaded_hashes_192[rehash_list[i]].LO % hash_table_size;
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

	if (bt_malloc((void **)&hash_location_list, (counter + 1) * sizeof(unsigned int *)))
		bt_error("Failed to allocate memory: hash_location_list.");

	counter = 0;
	for (i = 0; i < hash_table_size; i++)
	      if (collisions[i] > 3) {
			if (bt_malloc((void **)&hash_location_list[counter], (collisions[i] - 1) * sizeof(unsigned int)))
				bt_error("Failed to allocate memory: hash_location_list[counter].");
			counter++;
	      }

	for (i = 0; i < num_loaded_hashes; i++) {
		unsigned int k = rehash_list[i];
		unsigned int idx = loaded_hashes_192[k].LO % hash_table_size ;

		if (collisions[idx] == 2) {
			if (!hash_table[idx].iter) {
				hash_table[idx].iter++;
				hash_table[idx].store_loc1 = k;
			}
			else if (check_equal(hash_table[idx].store_loc1, k))
				set_zero(k);
		}

		if (collisions[idx] == 3) {
			if (!hash_table[idx].iter) {
				hash_table[idx].iter++;
				hash_table[idx].store_loc1 = k;
			}
			else if (hash_table[idx].iter == 1) {
				if (check_equal(hash_table[idx].store_loc1, k))
					set_zero(k);
				else
					hash_table[idx].store_loc2 = k;
			}
			else if (check_equal(hash_table[idx].store_loc1, k) ||
				 check_equal(hash_table[idx].store_loc2, k))
				set_zero(k);
		}

		else if (collisions[idx] > 3) {
			unsigned int iter = hash_table[idx].iter;
			if (!iter)
				hash_location_list[hash_table[idx].idx_hash_loc_list][iter++] = k;
			else {
				unsigned int j;
				for (j = 0; j < iter; j++)
					if (check_equal(hash_location_list[hash_table[idx].idx_hash_loc_list][j], k)) {
						set_zero(k);
						break;
					}
				if (j == iter && iter < (unsigned int)hash_table[idx].collisions - 1)
					hash_location_list[hash_table[idx].idx_hash_loc_list][iter++] = k;
			}
			hash_table[idx].iter = iter;
		}
	}

#undef COLLISION_DTYPE
	for (i = 0; i < counter; i++)
		bt_free((void **)&hash_location_list[i]);
	bt_free((void **)&hash_location_list);
	bt_free((void **)&hash_table);
	bt_free((void **)&collisions);
}

unsigned int remove_duplicates_192(unsigned int num_loaded_hashes, unsigned int hash_table_size, unsigned int verbosity)
{
	unsigned int i, num_unique_hashes, *rehash_list, counter;
#define COLLISION_DTYPE unsigned int
	COLLISION_DTYPE *collisions;
	typedef struct {
		unsigned int store_loc1;
		unsigned int store_loc2;
		unsigned int store_loc3;
		COLLISION_DTYPE iter;
	} hash_table_data;

	hash_table_data *hash_table = NULL;

	if (verbosity > 1)
		fprintf(stdout, "Removing duplicate hashes...");

	if (hash_table_size & (hash_table_size - 1)) {
		fprintf(stderr, "Duplicate removal hash table size must power of 2.\n");
		return 0;
	}

	if (bt_malloc((void **)&hash_table, hash_table_size * sizeof(hash_table_data)))
		bt_error("Failed to allocate memory: hash_table.");
	if (bt_calloc((void **)&collisions, hash_table_size, sizeof(COLLISION_DTYPE)))
		bt_error("Failed to allocate memory: collisions.");

#if _OPENMP
#pragma omp parallel private(i)
#endif
{
#if _OPENMP
#pragma omp for
#endif
	for (i = 0; i < num_loaded_hashes; i++) {
		unsigned int idx = loaded_hashes_192[i].LO & (hash_table_size - 1);
#if _OPENMP
#pragma omp atomic
#endif
		collisions[idx]++;
	}

	counter = 0;
#if _OPENMP
#pragma omp barrier

#pragma omp for
#endif
	for (i = 0; i < hash_table_size; i++) {
		  hash_table[i].iter = 0;
		 if (collisions[i] > 4)
#if _OPENMP
#pragma omp atomic
#endif
			 counter += (collisions[i] - 3);
	}
#if _OPENMP
#pragma omp barrier

#pragma omp sections
#endif
{
#if _OPENMP
#pragma omp section
#endif
{
	for (i = 0; i < num_loaded_hashes; i++) {
		unsigned int idx = loaded_hashes_192[i].LO & (hash_table_size - 1);

		if (collisions[idx] == 2) {
			if (!hash_table[idx].iter) {
				hash_table[idx].iter++;
				hash_table[idx].store_loc1 = i;
			}
			else if (check_equal(hash_table[idx].store_loc1, i))
				set_zero(i);
		}
	}
}

#if _OPENMP
#pragma omp section
#endif
{
	if (bt_malloc((void **)&rehash_list, counter * sizeof(unsigned int)))
		bt_error("Failed to allocate memory: rehash_list.");
	counter = 0;
	for (i = 0; i < num_loaded_hashes; i++) {
		unsigned int idx = loaded_hashes_192[i].LO & (hash_table_size - 1);

		if (collisions[idx] == 3) {
			if (!hash_table[idx].iter) {
				hash_table[idx].iter++;
				hash_table[idx].store_loc1 = i;
			}
			else if (hash_table[idx].iter == 1) {
				if (check_equal(hash_table[idx].store_loc1, i))
					set_zero(i);
				else {
					hash_table[idx].iter++;
					hash_table[idx].store_loc2 = i;
				}
			}
			else if (check_equal(hash_table[idx].store_loc1, i) ||
				 check_equal(hash_table[idx].store_loc2, i))
				set_zero(i);
		}

		else if (collisions[idx] >= 4) {
			if (!hash_table[idx].iter) {
				hash_table[idx].iter++;
				hash_table[idx].store_loc1 = i;
			}
			else if (hash_table[idx].iter == 1) {
				if (check_equal(hash_table[idx].store_loc1, i))
					set_zero(i);
				else {
					hash_table[idx].iter++;
					hash_table[idx].store_loc2 = i;
				}

			}
			else if (hash_table[idx].iter == 2) {
				if (check_equal(hash_table[idx].store_loc1, i) ||
				    check_equal(hash_table[idx].store_loc2, i))
					set_zero(i);
				else {
					hash_table[idx].iter++;
					hash_table[idx].store_loc3 = i;
				}
			}
			else if (hash_table[idx].iter >= 3) {
				if (check_equal(hash_table[idx].store_loc1, i) ||
				    check_equal(hash_table[idx].store_loc2, i) ||
				    check_equal(hash_table[idx].store_loc3, i))
					set_zero(i);
				else {
					if (collisions[idx] > 4)
						rehash_list[counter++] = i;
				}
			}
		}
	}

	if (counter)
		remove_duplicates_final(counter, counter + (counter >> 1), rehash_list);
	bt_free((void **)&rehash_list);
}
}
}

#if 0
	{	unsigned int col1 = 0, col2 = 0, col3 = 0, col4 = 0, col5a = 0;
		for (i = 0; i < hash_table_size; i++) {
			if (collisions[i] == 1)
				col1++;
			else if (collisions[i] == 2)
				col2++;
			else if (collisions[i] == 3)
				col3++;
			else if (collisions[i] == 4)
				col4++;
			else if (collisions[i] > 4)
				col5a += collisions[i];
		}
		col2 *= 2;
		col3 *= 3;
		col4 *= 4;
		fprintf(stderr, "Statistics:%Lf %Lf %Lf %Lf %Lf\n", (long double)col1 / (long double)num_loaded_hashes,
		  (long double)col2 / (long double)num_loaded_hashes, (long double)col3 / (long double)num_loaded_hashes,
			(long double)col4 / (long double)num_loaded_hashes, (long double)col5a / (long double)num_loaded_hashes);

	}
#endif
	num_unique_hashes = 0;
	for (i = num_loaded_hashes - 1; (int)i >= 0; i--)
		if (check_non_zero(i)) {
			num_unique_hashes = i;
			break;
		}

	for (i = 0; i <= num_unique_hashes; i++)
		if (check_zero(i)) {
			unsigned int j;
			loaded_hashes_192[i] = loaded_hashes_192[num_unique_hashes];
			set_zero(num_unique_hashes);
			num_unique_hashes--;
			for (j = num_unique_hashes; (int)j >= 0; j--)
				if (check_non_zero(j)) {
					num_unique_hashes = j;
					break;
				}
		}
#undef COLLISION_DTYPE
	bt_free((void **)&collisions);
	bt_free((void **)&hash_table);

	if (verbosity > 1)
		fprintf(stdout, "Done\n");

	return (num_unique_hashes + 1);
}
