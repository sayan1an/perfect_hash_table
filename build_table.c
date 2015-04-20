/*
 * This software is Copyright (c) 2015 Sayantan Datta <std2048 at gmail dot com>
 * and it is hereby released to the general public under the following terms:
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted.
 * Based on paper 'Perfect Spatial Hashing' by Lefebvre & Hoppe
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <sys/time.h>
#include <signal.h>
#include <unistd.h>
#include "mt.h"

#define BITMAP_TEST_OFF
#define DEBUG
#define OFFSET_TABLE_WORD unsigned int
#define HASH_TABLE_WORD uint128_t

typedef struct {
	uint64_t LO64;
	uint64_t HI64;
} uint128_t;

typedef struct {
	/* List of indexes linked to offset_data_idx */
	unsigned int *hash_location_list;
	unsigned short collisions;
	unsigned short iter;
	unsigned int offset_table_idx;

} auxilliary_offset_data;

/* Interface pointers */
unsigned int (*zero_check_ht)(unsigned int);
void (*assign_ht)(unsigned int, unsigned int);
void (*assign0_ht)(unsigned int);
unsigned int (*calc_ht_idx)(unsigned int, unsigned int);
unsigned int (*get_offset)(unsigned int, unsigned int);
void (*allocate_ht)(void);
void (*load_hashes)(void);
void (*test_tables)(void);
void *loaded_hashes;
void *hash_table;
unsigned int hash_type = 0;
unsigned int binary_size_actual = 0;

static uint128_t *loaded_hashes_128 = NULL;
static unsigned int num_loaded_hashes = 0;

static uint128_t *hash_table_128 = NULL;
static unsigned int hash_table_size, shift64_ht_sz;

static OFFSET_TABLE_WORD *offset_table = NULL;
static unsigned int offset_table_size, shift64_ot_sz;
static auxilliary_offset_data *offset_data = NULL;

static unsigned long long total_memory_in_bytes = 0;

static unsigned int signal_stop = 0, current_iter = 1, alarm_repeat = 1, alarm_start = 1;

void alarm_handler(int sig)
{
	if (sig == SIGALRM) {
		static int last_iter = 0;
		if (current_iter <= last_iter) {
			fprintf(stderr, "\nProgress is too slow!! trying next table size.\n");
			signal_stop = 1;
		}

		last_iter = current_iter;

		alarm(alarm_repeat);
	}
}

int qsort_compare(const void *p1, const void *p2)
{
	auxilliary_offset_data *a = (auxilliary_offset_data *)p1;
	auxilliary_offset_data *b = (auxilliary_offset_data *)p2;

	if (a[0].collisions > b[0].collisions) return -1;
	if (a[0].collisions == b[0].collisions) return 0;
	return 1;
}

unsigned int coprime_check(unsigned int m,unsigned int n)
{
	unsigned int rem;
	while (n != 0) {
		rem = m % n;
		m = n;
		n = rem;
	}
	return m;
}

void release_all_lists()
{
	unsigned int i;
	for (i = 0; i < offset_table_size; i++)
		free(offset_data[i].hash_location_list);
}

/* Assuming N < 0x7fffffff */
inline unsigned int modulo128_64b(uint128_t a, unsigned int N, uint64_t shift64)
{
	uint64_t p;
	p = (a.HI64 % N) * shift64;
	p += (a.LO64 % N);
	p %= N;
	return (unsigned int)p;
}

unsigned int modulo_op(void * hash, unsigned int N, uint64_t shift64)
{
	if (hash_type == 128)
		return  modulo128_64b(*(uint128_t *)hash, N, shift64);
	else
		fprintf(stderr, "modulo op error\n");
	return 0;
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

/* Exploits the fact that sorting with a bucket is not essential. */
void in_place_bucket_sort(unsigned int num_buckets)
{
	unsigned int *histogram = (unsigned int*) malloc((num_buckets + 1) * sizeof(unsigned int));
	unsigned int *histogram_empty = (unsigned int*) malloc((num_buckets + 1) * sizeof(unsigned int));
	unsigned int *prefix_sum = (unsigned int*) malloc((num_buckets + 10) * sizeof(unsigned int));
	unsigned int i;

	memset(histogram, 0, (num_buckets + 1) * sizeof(unsigned int));
	memset(histogram_empty, 0, (num_buckets + 1) * sizeof(unsigned int));
	memset(prefix_sum, 0, (num_buckets + 1) * sizeof(unsigned int));

	i = 0;
	while (i < offset_table_size)
		histogram[num_buckets - offset_data[i++].collisions]++;

	for (i = 1; i <= num_buckets; i++)
		prefix_sum[i] = prefix_sum[i - 1] + histogram[i - 1];

	i = 0;
	while (i < prefix_sum[num_buckets]) {
		unsigned int histogram_index = num_buckets - offset_data[i].collisions;
		if (i >= prefix_sum[histogram_index] &&
		    histogram_index < num_buckets &&
		    i < prefix_sum[histogram_index + 1]) {
			histogram_empty[histogram_index]++;
			i++;
		}
		else {
			auxilliary_offset_data tmp;
			unsigned int swap_index = prefix_sum[histogram_index] + histogram_empty[histogram_index];
			histogram_empty[histogram_index]++;
			tmp = offset_data[i];
			offset_data[i] = offset_data[swap_index];
			offset_data[swap_index] = tmp;
		}
	}

	free(histogram);
	free(histogram_empty);
	free(prefix_sum);
}

void allocate_ht_128()
{
	int i;

	posix_memalign((void **)&hash_table_128, 16, hash_table_size * sizeof(uint128_t));

	for (i = 0; i < hash_table_size; i++)
		hash_table_128[i].HI64 = hash_table_128[i].LO64 = 0;

	total_memory_in_bytes += hash_table_size * sizeof(uint128_t);

	fprintf(stdout, "Hash Table Size %Lf %% of Number of Loaded Hashes.\n", ((long double)hash_table_size / (long double)num_loaded_hashes) * 100.00);
	fprintf(stdout, "Hash Table Size(in GBs):%Lf\n", ((long double)hash_table_size * sizeof(uint128_t)) / ((long double)1024 * 1024 * 1024));
}

void init_tables(unsigned int approx_offset_table_sz, unsigned int approx_hash_table_sz)
{
	unsigned int i, max_collisions, offset_data_idx;

	fprintf(stderr, "Initialing Tables...");

	total_memory_in_bytes = 0;

	approx_hash_table_sz |= 1;
	/* Repeat until two sizes are coprimes */
	while (coprime_check(approx_offset_table_sz, approx_hash_table_sz) != 1)
		approx_offset_table_sz++;

	offset_table_size = approx_offset_table_sz;
	hash_table_size = approx_hash_table_sz;

	shift64_ht_sz = (((1ULL << 63) % hash_table_size) * 2) % hash_table_size;
	shift64_ot_sz = (((1ULL << 63) % offset_table_size) * 2) % offset_table_size;

	offset_table = (OFFSET_TABLE_WORD *) malloc(offset_table_size * sizeof(OFFSET_TABLE_WORD));
	total_memory_in_bytes += offset_table_size * sizeof(OFFSET_TABLE_WORD);

	offset_data = (auxilliary_offset_data *) malloc(offset_table_size * sizeof(auxilliary_offset_data));
	total_memory_in_bytes += offset_table_size * sizeof(auxilliary_offset_data);

	max_collisions = 0;

#pragma omp parallel private(i, offset_data_idx)
{
#pragma omp for
	for (i = 0; i < offset_table_size; i++) {
		//memset(&offset_data[i], 0, sizeof(auxilliary_offset_data));
		offset_data[i].offset_table_idx = 0;
		offset_data[i].collisions = 0;
		offset_data[i].hash_location_list = NULL;
		offset_data[i].iter = 0;
		offset_table[i] = 0;
	}
#pragma omp barrier
	/* Build Auxilliary data structure for offset_table. */
#pragma omp for
	for (i = 0; i < num_loaded_hashes; i++) {
		offset_data_idx = modulo_op(loaded_hashes + i * binary_size_actual, offset_table_size, shift64_ot_sz);
#pragma omp atomic
		offset_data[offset_data_idx].collisions++;
	}
#pragma omp barrier
#pragma omp single
	for (i = 0; i < offset_table_size; i++)
	      if (offset_data[i].collisions) {
			offset_data[i].hash_location_list = (unsigned int*) malloc(offset_data[i].collisions * sizeof(unsigned int));
			if (offset_data[i].collisions > max_collisions)
				max_collisions = offset_data[i].collisions;
	      }
#pragma omp barrier
#pragma omp for
	for (i = 0; i < num_loaded_hashes; i++) {
		unsigned int iter;
		offset_data_idx = modulo_op(loaded_hashes + i * binary_size_actual, offset_table_size, shift64_ot_sz);
#pragma omp atomic write
		offset_data[offset_data_idx].offset_table_idx = offset_data_idx;
#pragma omp atomic capture
		iter = offset_data[offset_data_idx].iter++;
		offset_data[offset_data_idx].hash_location_list[iter] = i;
	}
#pragma omp barrier
}
	total_memory_in_bytes += num_loaded_hashes * sizeof(unsigned int);

	//qsort((void *)offset_data, offset_table_size, sizeof(auxilliary_offset_data), qsort_compare);
	in_place_bucket_sort(max_collisions);

	allocate_ht();

	fprintf(stdout, "Offset Table Size %Lf %% of Number of Loaded Hashes.\n", ((long double)offset_table_size / (long double)num_loaded_hashes) * 100.00);
	fprintf(stdout, "Offset Table Size(in GBs):%Lf\n", ((long double)offset_table_size * sizeof(OFFSET_TABLE_WORD)) / ((long double)1024 * 1024 * 1024));
	fprintf(stdout, "Offset Table Aux Data Size(in GBs):%Lf\n", ((long double)offset_table_size * sizeof(auxilliary_offset_data)) / ((long double)1024 * 1024 * 1024));
	fprintf(stdout, "Offset Table Aux List Size(in GBs):%Lf\n", ((long double)num_loaded_hashes * sizeof(unsigned int)) / ((long double)1024 * 1024 * 1024));

	for (i = 0; i < offset_table_size && offset_data[i].collisions; i++);
	fprintf (stdout, "Unused Slots in Offset Table:%Lf %%\n", 100.00 * (long double)(offset_table_size - i) / (long double)(offset_table_size));

	fprintf(stdout, "Total Memory Use(in GBs):%Lf\n\n", ((long double)total_memory_in_bytes) / ((long double) 1024 * 1024 * 1024));

	alarm(alarm_start);
}

inline unsigned int calc_ht_idx_128(unsigned int hash_location, unsigned int offset)
{
	return  modulo128_64b(add128(loaded_hashes_128[hash_location], offset), hash_table_size, shift64_ht_sz);
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

unsigned int check_n_insert_into_hash_table(unsigned int offset, auxilliary_offset_data * ptr, unsigned int *hash_table_idxs, unsigned int *store_hash_modulo_table_sz)
{
	unsigned int i;

	i = 0;
	while (i < ptr -> collisions) {
		hash_table_idxs[i] = store_hash_modulo_table_sz[i] + offset;
		if (hash_table_idxs[i] >= hash_table_size)
			hash_table_idxs[i] -= hash_table_size;
		if (zero_check_ht(hash_table_idxs[i++]))
			return 0;
	}

	i = 0;
	while (i < ptr -> collisions) {
		if (zero_check_ht(hash_table_idxs[i])) {
			unsigned int j = 0;
			while (j < i)
				assign0_ht(hash_table_idxs[j++]);
			return 0;
		}
		assign_ht(hash_table_idxs[i], ptr -> hash_location_list[i]);
		i++;
	}
	return 1;
}

unsigned int get_offset_128(unsigned int hash_table_idx, unsigned int hash_location)
{
	unsigned int z = modulo128_64b(loaded_hashes_128[hash_location], hash_table_size, shift64_ht_sz);
	return (hash_table_size - z + hash_table_idx);
}

void calc_hash_mdoulo_table_size(unsigned int *store, auxilliary_offset_data * ptr) {
	unsigned int i = 0;
	while (i < ptr -> collisions) {
		store[i] =  modulo_op(loaded_hashes + (ptr -> hash_location_list[i]) * binary_size_actual, hash_table_size, shift64_ht_sz);
		i++;
	}
}
unsigned int create_tables()
{
 	unsigned int i, backtracking;

	unsigned int bitmap = ((1ULL << (sizeof(OFFSET_TABLE_WORD) * 8)) - 1) & 0xFFFFFFFF;
	unsigned int limit = bitmap % hash_table_size + 1;

	unsigned int hash_table_idx, *store_hash_modulo_table_sz = (unsigned int *) malloc(offset_data[0].collisions * sizeof(unsigned int));
	OFFSET_TABLE_WORD last_offset;
	unsigned int *hash_table_idxs = (unsigned int*) malloc(offset_data[0].collisions * sizeof(unsigned int));

	unsigned int trigger;
	long double done = 0;
	struct timeval t;

	gettimeofday(&t, NULL);

	seedMT((unsigned int) t.tv_sec);

	i = 0;
	backtracking = 0;
	trigger = 0;
	while (offset_data[i].collisions > 1) {
		OFFSET_TABLE_WORD offset;
		unsigned int num_iter;

		done += offset_data[i].collisions;

		current_iter = i;

		calc_hash_mdoulo_table_size(store_hash_modulo_table_sz, &offset_data[i]);

		offset = (OFFSET_TABLE_WORD)(randomMT() & bitmap) % hash_table_size;

		if (backtracking) {
			offset = (last_offset + 1);
			backtracking = 0;
		}

		num_iter = 0;
		while (!check_n_insert_into_hash_table((unsigned int)offset, &offset_data[i], hash_table_idxs, store_hash_modulo_table_sz) && num_iter < limit) {
			offset++;
			if (offset >= hash_table_size) offset = 0;
			num_iter++;
		}
		//fprintf(stderr, "Backtracking...\n");
		offset_table[offset_data[i].offset_table_idx] = offset;

		if (num_iter == limit) {
			unsigned int j, backtrack_steps, iter;

			done -= offset_data[i].collisions;
			offset_table[offset_data[i].offset_table_idx] = 0;

			backtrack_steps = 1;
			j = 1;
			while (j <= backtrack_steps) {
				last_offset = offset_table[offset_data[i - j].offset_table_idx];
				iter = 0;
				while (iter < offset_data[i - j].collisions) {
					hash_table_idx = calc_ht_idx(offset_data[i - j].hash_location_list[iter], last_offset);
					assign0_ht(hash_table_idx);
					iter++;
				}
				offset_table[offset_data[i - j].offset_table_idx] = 0;
				done -= offset_data[i - j].collisions;
				j++;
			}
			i -= backtrack_steps;

			backtracking = 1;

			i--;
		}

		if ((trigger & 0xffff) == 0) {
			trigger = 0;
			fprintf(stdout, "\rProgress:%Lf %%, Number of collisions:%u", done / (long double)num_loaded_hashes * 100.00, offset_data[i].collisions);
			fflush(stdout);
		}

		if (signal_stop) {
			signal_stop = 0;
			current_iter = 1;
			alarm(0);
			return 0;
		}

		trigger++;
		i++;
	}

	alarm(0);

	hash_table_idx = 0;
	while (offset_data[i].collisions > 0) {
		done++;

		while (hash_table_idx < hash_table_size) {
			if (!zero_check_ht(hash_table_idx)) {
				assign_ht(hash_table_idx, offset_data[i].hash_location_list[0]);
				break;
			}
			hash_table_idx++;
		}
		offset_table[offset_data[i].offset_table_idx] = get_offset(hash_table_idx, offset_data[i].hash_location_list[0]);
		if ((trigger & 0xffff) == 0) {
			trigger = 0;
			fprintf(stdout, "\rProgress:%Lf %%, Number of collisions:%u", done / (long double)num_loaded_hashes * 100.00, offset_data[i].collisions);
			fflush(stdout);
		}
		trigger++;
		i++;
	}

	fprintf(stdout, "\n");
	free(hash_table_idxs);

	return 1;
}

void test_tables_128()
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
					modulo128_64b(loaded_hashes_128[i],
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

void load_hashes_128()
{
#define NO_MODULO_TEST
	FILE *fp;
	char filename[200] = "100M";
	char string_a[9], string_b[9], string_c[9], string_d[9];
	unsigned int iter, shift64;

	fprintf(stderr, "Loading Hashes...");

	fp = fopen(filename, "r");
	if (fp == NULL) {
		fprintf(stderr, "Error reading file.\n");
		exit(0);
	}
	while (fscanf(fp, "%08s%08s%08s%08s\n", string_a, string_b, string_c, string_d) == 4) num_loaded_hashes++;
	fclose(fp);

	fp = fopen(filename, "r");
	if (fp == NULL) {
		fprintf(stderr, "Error reading file.\n");
		exit(0);
	}

	loaded_hashes_128 = (uint128_t *) calloc(num_loaded_hashes, sizeof(uint128_t));
	total_memory_in_bytes += (unsigned long long)num_loaded_hashes * sizeof(uint128_t);

	iter = 0;
	shift64 = (((1ULL << 63) % num_loaded_hashes) * 2) % num_loaded_hashes;

	while(fscanf(fp, "%08s%08s%08s%08s\n", string_a, string_b, string_c, string_d) == 4) {
#ifndef NO_MODULO_TEST
		unsigned int p ,q;
		unsigned __int128 test;
#endif
		uint128_t input_hash_128;
		unsigned int a, b, c, d;

		string_a[8] = string_b[8] = string_c[8] = string_d[8] = 0;

		a = (unsigned int) strtol(string_a, NULL, 16);
		b = (unsigned int) strtol(string_b, NULL, 16);
		c = (unsigned int) strtol(string_c, NULL, 16);
		d = (unsigned int) strtol(string_d, NULL, 16);

		input_hash_128.HI64 = ((uint64_t)d << 32) | (uint64_t)c;
		input_hash_128.LO64 = ((uint64_t)b << 32) | (uint64_t)a;

		loaded_hashes_128[iter++] = input_hash_128;

#ifndef NO_MODULO_TEST
		test = ((unsigned __int128)d << 96) + ((unsigned __int128)c << 64) + ((unsigned __int128)b << 32) + ((unsigned __int128)a);
		p = test % num_loaded_hashes;
		q = modulo128_64b(input_hash_128, num_loaded_hashes, shift64);
		if (p != q) {
			fprintf(stderr, "Error with modulo operation!!\n");
			exit(0);
		}
#endif
	}

	fprintf(stderr, "Done.\n");

	fprintf(stdout, "Number of loaded hashes(in millions):%Lf\n", (long double)num_loaded_hashes/ ((long double)1000.00 * 1000.00));
	fprintf(stdout, "Size of Loaded Hashes(in GBs):%Lf\n\n", ((long double)total_memory_in_bytes) / ((long double) 1024 * 1024 * 1024));

	fclose(fp);
}

void bitmap_test()
{
	unsigned int num_bitmap_loads_4G, num_bitmap_loads_1G, num_bitmap_loads_128M, num_bitmap_loads_16M, num_bitmap_loads_1M, num_bitmap_loads_128k, num_bitmap_loads_16k, num_bitmap_loads_1k;
	long double load4G, load1G, load128M, load16M, load1M, load128k, load16k, load1k;
	unsigned int bitmap_idx, iter;
	unsigned int *bitmap_1k, *bitmap_16k, *bitmap_128k, *bitmap_1M, *bitmap_16M, *bitmap_128M, *bitmap_1G, *bitmap_4G;
	unsigned int input_hash_LO32, total_bitmap_size;

	total_memory_in_bytes = 0;

	load_hashes_128();

	bitmap_1k = (unsigned int *) calloc(32, 4);
	bitmap_16k = (unsigned int *) calloc(32 * 16, 4);
	bitmap_128k = (unsigned int *) calloc(32 * 16 * 8, 4);
	bitmap_1M = (unsigned int *) calloc(32 * 16 * 8 * 8, 4);
	bitmap_16M = (unsigned int *) calloc(32 * 16 * 8 * 8 * 16, 4);
	bitmap_128M = (unsigned int *) calloc(32 * 16 * 8 * 8 * 16 * 8, 4);
	bitmap_1G = (unsigned int *) calloc(32 * 16 * 8 * 8 * 16 * 8 * 8, 4);
	bitmap_4G = (unsigned int *) calloc(32 * 16 * 8 * 8 * 16 * 8 * 8 * 4, 4);

	total_bitmap_size =
		((unsigned long long)32 + 32 * 16 + 32 * 16 * 8 + 32 * 16 * 8 * 8 +
		32 * 16 * 8 * 8 * 16 + 32 * 16 * 8 * 8 * 16 * 8 + 32 * 16 * 8 * 8 * 16 * 8 * 8 +
		32 * 16 * 8 * 8 * 16 * 8 * 8 * 4) * 4;

	num_bitmap_loads_4G = num_bitmap_loads_1G = num_bitmap_loads_128M = num_bitmap_loads_16M = num_bitmap_loads_1M = num_bitmap_loads_128k = num_bitmap_loads_16k = num_bitmap_loads_1k = 0;

	iter = 0;
	while (iter < num_loaded_hashes) {
		input_hash_LO32 = loaded_hashes_128[iter].LO64 & 0xFFFFFFFF;

		bitmap_idx = input_hash_LO32 & ((unsigned int)4 * 1024 * 1024 * 1024 - 1);
		if (!(bitmap_4G[bitmap_idx >> 5] & (1U << (bitmap_idx & 31)))) {
			bitmap_4G[bitmap_idx >> 5] |= (1U << (bitmap_idx & 31));
			num_bitmap_loads_4G++;
		}

		bitmap_idx = input_hash_LO32 & ((unsigned int)1024 * 1024 * 1024 - 1);
		if (!(bitmap_1G[bitmap_idx >> 5] & (1U << (bitmap_idx & 31)))) {
			bitmap_1G[bitmap_idx >> 5] |= (1U << (bitmap_idx & 31));
			num_bitmap_loads_1G++;
		}

		bitmap_idx = input_hash_LO32 & ((unsigned int)128 * 1024 * 1024 - 1);
		if (!(bitmap_128M[bitmap_idx >> 5] & (1U << (bitmap_idx & 31)))) {
			bitmap_128M[bitmap_idx >> 5] |= (1U << (bitmap_idx & 31));
			num_bitmap_loads_128M++;
		}

		bitmap_idx = input_hash_LO32 & ((unsigned int)16 * 1024 * 1024 - 1);
		if (!(bitmap_16M[bitmap_idx >> 5] & (1U << (bitmap_idx & 31)))) {
			bitmap_16M[bitmap_idx >> 5] |= (1U << (bitmap_idx & 31));
			num_bitmap_loads_16M++;
		}

		bitmap_idx = input_hash_LO32 & ((unsigned int)1024 * 1024 - 1);
		if (!(bitmap_1M[bitmap_idx >> 5] & (1U << (bitmap_idx & 31)))) {
			bitmap_1M[bitmap_idx >> 5] |= (1U << (bitmap_idx & 31));
			num_bitmap_loads_1M++;
		}

		bitmap_idx = input_hash_LO32 & ((unsigned int)128 * 1024 - 1);
		if (!(bitmap_128k[bitmap_idx >> 5] & (1U << (bitmap_idx & 31)))) {
			bitmap_128k[bitmap_idx >> 5] |= (1U << (bitmap_idx & 31));
			num_bitmap_loads_128k++;
		}

		bitmap_idx = input_hash_LO32 & ((unsigned int)16 * 1024 - 1);
		if (!(bitmap_16k[bitmap_idx >> 5] & (1U << (bitmap_idx & 31)))) {
			bitmap_16k[bitmap_idx >> 5] |= (1U << (bitmap_idx & 31));
			num_bitmap_loads_16k++;
		}

		bitmap_idx = input_hash_LO32 & ((unsigned int)1024 - 1);
		if (!(bitmap_1k[bitmap_idx >> 5] & (1U << (bitmap_idx & 31)))) {
			bitmap_1k[bitmap_idx >> 5] |= (1U << (bitmap_idx & 31));
			num_bitmap_loads_1k++;
		}

		iter++;
	}
	fprintf(stdout, "Toatal Size of Bitmaps(in MBs):%Lf\n", ((long double)total_bitmap_size) / ((long double)1024 * 1024));

	load4G = ((long double)num_bitmap_loads_4G / ((long double)4 * 1024 * 1024 * 1024));
	load1G = ((long double)num_bitmap_loads_1G / ((long double)1024 * 1024 * 1024));
	load128M = ((long double)num_bitmap_loads_128M / ((long double)128 * 1024 * 1024));
	load16M = ((long double)num_bitmap_loads_16M / ((long double)16 * 1024 * 1024));
	load1M = ((long double)num_bitmap_loads_1M / ((long double)1024 * 1024));
	load128k = ((long double)num_bitmap_loads_128k / ((long double)128 * 1024));
	load16k = ((long double)num_bitmap_loads_16k / ((long double)16 * 1024));
	load1k = ((long double)num_bitmap_loads_1k / ((long double)1024));

	fprintf(stdout, "4Gbit bitmap: Number of bits set:%u, Fraction positive:%Lf\n", num_bitmap_loads_4G, load4G);
	fprintf(stdout, "1Gbit bitmap: Number of bits set:%u, Fraction positive:%Lf\n", num_bitmap_loads_1G, load1G);
	fprintf(stdout, "128Mbit bitmap: Number of bits set:%u, Fraction positive:%Lf\n", num_bitmap_loads_128M, load128M);
	fprintf(stdout, "16Mbit bitmap: Number of bits set:%u, Fraction positive:%Lf\n", num_bitmap_loads_16M, load16M);
	fprintf(stdout, "1Mbit bitmap: Number of bits set:%u, Fraction positive:%Lf\n", num_bitmap_loads_1M, load1M);
	fprintf(stdout, "128Kbit bitmap: Number of bits set:%u, Fraction positive:%Lf\n", num_bitmap_loads_128k, load128k);
	fprintf(stdout, "16Kbit bitmap: Number of bits set:%u, Fraction positive:%Lf\n", num_bitmap_loads_16k, load16k);
	fprintf(stdout, "1Kbit bitmap: Number of bits set:%u, Fraction positive:%Lf\n", num_bitmap_loads_1k, load1k);

	free(bitmap_1k);
	free(bitmap_16k);
	free(bitmap_128k);
	free(bitmap_1M);
	free(bitmap_16M);
	free(bitmap_128M);
	free(bitmap_1G);
	free(bitmap_4G);

	free(loaded_hashes_128);
}

unsigned int next_prime(unsigned int num)
{
	if (num == 1)
		return 2;
	else if (num == 2)
		return 3;
	else if (num == 3 || num == 4)
		return 5;
	else if (num == 5 || num == 6)
		return 7;
	else if (num >= 7 && num <= 9)
		return 1;
/*	else if (num == 11 || num == 12)
		return 13;
	else if (num >= 13 && num < 17)
		return 17;
	else if (num == 17 || num == 18)
		return 19;
	else if (num >= 19 && num < 23)
		return 23;
	else if (num >= 23 && num < 29)
		return 29;
	else if (num == 29 || num == 30 )
		return 31;
	else if (num >= 31 && num < 37)
		return 37;
	else if (num >= 37 && num < 41)
		return 41;
	else if (num == 41 || num == 42 )
		return 43;
	else if (num >= 43 && num < 47)
		return 47;
	else if (num >= 47 && num < 53)
		return 53;
	else if (num >= 53 && num < 59)
		return 59;
	else if (num == 59 || num == 60)
		return 61;
	else if (num >= 61 && num < 67)
		return 67;
	else if (num >= 67 && num < 71)
		return 71;
	else if (num == 71 || num == 72)
		return 73;
	else if (num >= 73 && num < 79)
		return 79;
	else if (num >= 79 && num < 83)
		return 83;
	else if (num >= 83 && num < 89)
		return 89;
	else if (num >= 89 && num < 97)
		return 97;
	else
		return 1;*/
}
void create_perfect_hash_table()
{
	long double multiplier_ht, multiplier_ot;
	unsigned int approx_hash_table_sz, approx_offset_table_sz, i;

	total_memory_in_bytes = 0;

	load_hashes = load_hashes_128;

	load_hashes();

	hash_type = 128;
	binary_size_actual = 16;
	zero_check_ht = zero_check_ht_128;
	assign_ht = assign_ht_128;
	assign0_ht = assign0_ht_128;
	calc_ht_idx = calc_ht_idx_128;
	get_offset = get_offset_128;
	allocate_ht = allocate_ht_128;
	loaded_hashes = loaded_hashes_128;
	hash_table = hash_table_128;
	test_tables = test_tables_128;

	signal(SIGALRM, alarm_handler);

	if (num_loaded_hashes <= 1000)
		multiplier_ot = 1.101375173;
	else if (num_loaded_hashes <= 10000)
		multiplier_ot = 1.151375173;
	else if (num_loaded_hashes <= 100000)
		multiplier_ot = 1.20375173;
	else if (num_loaded_hashes <= 1000000)
		multiplier_ot = 1.25375173;
	else if (num_loaded_hashes <= 10000000)
		multiplier_ot = 1.31375173;
	else if (num_loaded_hashes <= 110000000) {
		multiplier_ot = 1.41375173;
		alarm_start = 3;
		alarm_repeat = 7;
	}
	else if (num_loaded_hashes <= 200000000) {
		multiplier_ot = 1.61375173;
		alarm_start = 4;
		alarm_repeat = 10;
	}
	else {
		fprintf(stderr, "This many number of hashes have never been tested before and might not succeed!!\n");
		multiplier_ot = 3.01375173;
		alarm_start = 5;
		alarm_repeat = 15;
	}


	multiplier_ht = 1.001097317;

	approx_offset_table_sz = (((long double)num_loaded_hashes / 4.0) * multiplier_ot);
	approx_hash_table_sz = ((long double)num_loaded_hashes * multiplier_ht);

	i = 0;
	do {
		unsigned int temp;

		init_tables(approx_offset_table_sz, approx_hash_table_sz);

		if (create_tables())
			break;

		release_all_lists();
		free(offset_data);
		free(offset_table);
		free(hash_table);

		temp = next_prime(approx_offset_table_sz % 10);
		approx_offset_table_sz /= 10;
		approx_offset_table_sz *= 10;
		approx_offset_table_sz += temp;

		i++;

		if (!(i % 5)) {
			multiplier_ot += 0.05;
			multiplier_ht += 0.005;
			approx_offset_table_sz = (((long double)num_loaded_hashes / 4.0) * multiplier_ot);
			approx_hash_table_sz = ((long double)num_loaded_hashes * multiplier_ht);
		}

	} while(1);

	test_tables();

	release_all_lists();
	free(offset_data);
	free(loaded_hashes);
}

int main ()
{
	//bitmap_test();
	create_perfect_hash_table();




	return 0;
}