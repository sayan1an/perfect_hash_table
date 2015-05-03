#include <stdlib.h>
#include <stdio.h>
#include "interface.h"

static uint128_t *loaded_hashes_128;
static uint192_t *loaded_hashes_192;
static unsigned int num_loaded_hashes = 0;

static OFFSET_TABLE_WORD *offset_table = NULL;
static unsigned int offset_table_size = 0;
static unsigned int hash_table_size = 0;

static unsigned int total_memory_in_bytes = 0;

static void load_hashes_128(char *filename)
{
	FILE *fp;
	char string_a[9], string_b[9], string_c[9], string_d[9];
	unsigned int iter, shift64;

	fprintf(stdout, "Loading Hashes...");

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

	}

	fprintf(stdout, "Done.\n");

	fprintf(stdout, "Number of loaded hashes(in millions):%Lf\n", (long double)num_loaded_hashes/ ((long double)1000.00 * 1000.00));
	fprintf(stdout, "Size of Loaded Hashes(in GBs):%Lf\n\n", ((long double)total_memory_in_bytes) / ((long double) 1024 * 1024 * 1024));

	fclose(fp);
}

static void load_hashes_160(char *filename)
{
	FILE *fp;
	char string_a[9], string_b[9], string_c[9], string_d[9], string_e[9];
	unsigned int iter, shift64;

	fprintf(stdout, "Loading Hashes...");

	fp = fopen(filename, "r");
	if (fp == NULL) {
		fprintf(stderr, "Error reading file.\n");
		exit(0);
	}
	while (fscanf(fp, "%08s%08s%08s%08s%08s\n", string_a, string_b, string_c, string_d, string_e) == 5) num_loaded_hashes++;
	fclose(fp);

	fp = fopen(filename, "r");
	if (fp == NULL) {
		fprintf(stderr, "Error reading file.\n");
		exit(0);
	}

	loaded_hashes_192 = (uint192_t *) calloc(num_loaded_hashes, sizeof(uint192_t));
	total_memory_in_bytes += (unsigned long long)num_loaded_hashes * sizeof(uint192_t);

	iter = 0;
	shift64 = (((1ULL << 63) % num_loaded_hashes) * 2) % num_loaded_hashes;

	while(fscanf(fp, "%08s%08s%08s%08s%08s\n", string_a, string_b, string_c, string_d, string_e) == 5) {

		uint192_t input_hash_192;
		unsigned int a, b, c, d, e;

		string_a[8] = string_b[8] = string_c[8] = string_d[8] = string_e[8] = 0;

		a = (unsigned int) strtol(string_a, NULL, 16);
		b = (unsigned int) strtol(string_b, NULL, 16);
		c = (unsigned int) strtol(string_c, NULL, 16);
		d = (unsigned int) strtol(string_d, NULL, 16);
		e = (unsigned int) strtol(string_e, NULL, 16);

		input_hash_192.HI = (uint64_t)e;
		input_hash_192.MI = ((uint64_t)d << 32) | (uint64_t)c;
		input_hash_192.LO = ((uint64_t)b << 32) | (uint64_t)a;

		loaded_hashes_192[iter++] = input_hash_192;
	}

	fprintf(stdout, "Done.\n");

	fprintf(stdout, "Number of loaded hashes(in millions):%Lf\n", (long double)num_loaded_hashes/ ((long double)1000.00 * 1000.00));
	fprintf(stdout, "Size of Loaded Hashes(in GBs):%Lf\n\n", ((long double)total_memory_in_bytes) / ((long double) 1024 * 1024 * 1024));

	fclose(fp);
}

static unsigned int modulo192_31b(uint192_t a, unsigned int N)
{
	uint64_t p, shift128, shift64, temp;
	unsigned int temp2;

	shift64 = (((1ULL << 63) % N) * 2) % N;
	shift128 = (shift64 * shift64) % N;

	p = (a.HI % N) * shift128;
	p += (a.MI % N) * shift64;
	p += a.LO % N;
	p %= N;
	return (unsigned int)p;
}

static uint192_t add192(uint192_t a, unsigned int b)
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

int main(int argc, char *argv[])
{
	if ( argc != 2 ) {
		fprintf(stderr, "Please Enter a File Name");
	}

	unsigned int offset_table_index, hash_table_index, lookup;
	uint192_t temp;

	// Building 128 bit tables.
	/*load_hashes_128(argv[1]);

	create_perfect_hash_table(128, (void *)loaded_hashes_128,
			       num_loaded_hashes,
			       &offset_table,
			       &offset_table_size,
			       &hash_table_size);*/

	// Building 192 bit tables
	/*
	 * Load 160bit hashes into the array 'loaded_hashes_192'
	 */
	load_hashes_160(argv[1]);

	/*
	 * Build the tables.
	 */
	if (create_perfect_hash_table(192, (void *)loaded_hashes_192,
			       num_loaded_hashes,
			       &offset_table,
			       &offset_table_size,
			       &hash_table_size)) {

		/*
		 * Demo use of tables.
		 */
		lookup = 3;

		/*
		 * Perform a Lookup into hash table and compute location
		 * in hash table corresponding to item at location '3' in
		 * hash array.
		 */
		offset_table_index = modulo192_31b(loaded_hashes_192[lookup], offset_table_size);
		temp = add192(loaded_hashes_192[lookup], (unsigned int)offset_table[offset_table_index]);
		hash_table_index = modulo192_31b(temp, hash_table_size);

		if (hash_table_192[hash_table_index].LO == loaded_hashes_192[lookup].LO &&
		    hash_table_192[hash_table_index].MI == loaded_hashes_192[lookup].MI &&
		    hash_table_192[hash_table_index].HI == loaded_hashes_192[lookup].HI)
			fprintf(stdout, "Lookup successful.\n");
		else
			fprintf(stderr, "Lookup failed.\n");
	}
	else {
		free(hash_table_192);
		free(offset_table);
	}

	free(loaded_hashes_128);
	free(loaded_hashes_192);

	return 0;
}