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

static void load_hashes_128()
{
#define NO_MODULO_TEST
	FILE *fp;
	char filename[200] = "35M";
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
		q = modulo128_31b(input_hash_128, num_loaded_hashes, shift64);
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

static void load_hashes_160()
{
	FILE *fp;
	char filename[200] = "30M_sha";
	char string_a[9], string_b[9], string_c[9], string_d[9], string_e[9];
	unsigned int iter, shift64;

	fprintf(stderr, "Loading Hashes...");

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

	fprintf(stderr, "Done.\n");

	fprintf(stdout, "Number of loaded hashes(in millions):%Lf\n", (long double)num_loaded_hashes/ ((long double)1000.00 * 1000.00));
	fprintf(stdout, "Size of Loaded Hashes(in GBs):%Lf\n\n", ((long double)total_memory_in_bytes) / ((long double) 1024 * 1024 * 1024));

	fclose(fp);
}

int main()
{
	/*load_hashes_128();

	create_perfect_hash_table(128, (void *)loaded_hashes_128,
			       num_loaded_hashes,
			       offset_table,
			       &offset_table_size,
			       &hash_table_size);*/

	load_hashes_160();

	create_perfect_hash_table(192, (void *)loaded_hashes_192,
			       num_loaded_hashes,
			       offset_table,
			       &offset_table_size,
			       &hash_table_size);

	free(loaded_hashes_128);
	free(loaded_hashes_192);

	return 0;
}

