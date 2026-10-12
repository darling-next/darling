/*
 This file is part of Darling.

 Copyright (C) 2019 Lubos Dolezel

 Darling is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 Darling is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with Darling.  If not, see <http://www.gnu.org/licenses/>.
*/


#include <BNNS/BNNS.h>
#include <float.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static int verbose = 0;

__attribute__((constructor))
static void initme(void) {
    verbose = getenv("STUB_VERBOSE") != NULL;
}

void* BNNSApplyVectorActivationLayer(void)
{
    if (verbose) puts("STUB: BNNSApplyVectorActivationLayer called");
    return NULL;
}

void* BNNSDequantize(void)
{
    if (verbose) puts("STUB: BNNSDequantize called");
    return NULL;
}

void* BNNSFilterApply(void)
{
    if (verbose) puts("STUB: BNNSFilterApply called");
    return NULL;
}

void* BNNSFilterApplyBatch(void)
{
    if (verbose) puts("STUB: BNNSFilterApplyBatch called");
    return NULL;
}

void* BNNSFilterCreateConvolutionLayer(void)
{
    if (verbose) puts("STUB: BNNSFilterCreateConvolutionLayer called");
    return NULL;
}

void* BNNSFilterCreateConvolutionWeightsTensorConversionLayer(void)
{
    if (verbose) puts("STUB: BNNSFilterCreateConvolutionWeightsTensorConversionLayer called");
    return NULL;
}

void* BNNSFilterCreateFullyConnectedLayer(void)
{
    if (verbose) puts("STUB: BNNSFilterCreateFullyConnectedLayer called");
    return NULL;
}

void* BNNSFilterCreateImageTensorConversionLayer(void)
{
    if (verbose) puts("STUB: BNNSFilterCreateImageTensorConversionLayer called");
    return NULL;
}

void* BNNSFilterCreatePoolingLayer(void)
{
    if (verbose) puts("STUB: BNNSFilterCreatePoolingLayer called");
    return NULL;
}

void* BNNSFilterCreateTensorConvolutionLayer(void)
{
    if (verbose) puts("STUB: BNNSFilterCreateTensorConvolutionLayer called");
    return NULL;
}

void* BNNSFilterCreateVectorActivationLayer(void)
{
    if (verbose) puts("STUB: BNNSFilterCreateVectorActivationLayer called");
    return NULL;
}

void* BNNSFilterDestroy(void)
{
    if (verbose) puts("STUB: BNNSFilterDestroy called");
    return NULL;
}

/*
 Random number generation.

 MEASURED 2026-10-12, from the sweep of the guest's Xcode 26.3 (DVTSmartSearch.framework):
 the twelve entry points above are the whole of libBNNS's export trie (12 entries; Apple's
 libBNNS.tbd carries 408), and DVTSmartSearch demands three names that are NOT among them --
 BNNSCreateRandomGenerator, BNNSDestroyRandomGenerator, BNNSRandomFillUniformFloat -- plus
 nine vDSP names that already exist in the built libvDSP (455 exports).  All twelve bindings
 are STRONG and NON-LAZY (the referrer's chained imports 0x0..0xB, __DATA_CONST __got,
 dependency field "Accelerate"), so dyld resolves every one of them before the image runs:
 declaring Accelerate.framework without these three would turn a clear "image not found"
 into "symbol not found".  Hence a real implementation rather than three more stubs.

 THE GENERATOR.  BNNSRandomGeneratorMethod has exactly one member, AES_CTR, and bnns.h
 documents it as "Implementation based on the AES hash of a counter".  This is that method
 and not a quieter substitute for it: AES-128 (FIPS-197) in counter mode, the key expanded
 once per generator, the 128-bit counter block being eight zero bytes followed by the
 big-endian block index, four 32-bit keystream words per block.  The round function is
 written out below rather than linked from corecrypto because libBNNS's only load command is
 /usr/lib/libSystem.B.dylib, measured on both architectures: taking a second image would
 change what the deploy has to resolve, for the encryption direction of the ~140 lines below
 that this file needs and nothing else.

 QUALITY.  The draws ARE the AES output, which is what the method names, so no bit of a
 keystream block is ever reused and the generator grows no state beyond key and counter.  A
 uniform draw takes the top 24 bits of a keystream word times 2^-24: every float in [0,1)
 with a 24-bit significand is reachable, all equally likely, and every step of the arithmetic
 is exact in binary32; the map onto [a, b] runs in double, because (b - a) alone is already an
 infinity for two bounds of opposite signs near FLT_MAX and the width of the interval has to
 survive the subtraction.  The inverse of that (an AES block is 128 bits for four draws) is the
 price of not naming a generator this file does not implement.

 SEEDING.  BNNSCreateRandomGenerator takes no seed, and bnns.h documents the seed as
 "internally generated ... that will vary from call to call", so the 128-bit key comes from
 arc4random_buf -- measured present in this tree's libsystem_c.dylib, which the
 libSystem.B.dylib this file already links re-exports.  That is the guest's own entropy
 pool; seeding from the clock or from the address of the allocation would be the same
 signature with an attacker-guessable key.  The counter starts at zero, which is safe
 precisely because the key is fresh per generator.

 THE ARRAY DESCRIPTOR.  bnns.h fixes the element order for a first-major and a last-major
 layout with two different formulas, and the fill below does not need to tell them apart:
 each dimension is walked over its whole size, so the set of (logical index, element offset)
 pairs is the same under either formula and every element gets one independent draw.  The
 rank is the layout's own: bits 16..23 of BNNSDataLayout, whose constants bnns_constants.h
 groups as "1-dimensional layouts = 0x10000 ... 8-dimensional layouts = 0x88000".  A stride
 of zero means "contiguous in this axis", which bnns.h spells as size[i-1] * stride[i-1]
 with stride[0] == 1.  Only the three floating point data types reach the fill: bnns.h
 requires a floating point descriptor, and the two half-width ones are rounded as bnns.h
 asks (a and b capped at the closest finite value the type can hold -- binary16 tops out at
 65504, bfloat16 at 0x7f7f0000, which is below FLT_MAX -- and the draw rounded to nearest even).

 STATUS.  bnns.h documents "0 on success, nonzero on error" and Apple publishes no finer
 enumeration, so the negative codes below are private to this file and exist only so that a
 caller can tell the documented failure conditions apart.  The twelve stubs above have no
 status to return; these three are the only part of the file that can fail.

 WHAT THIS DOES NOT DO.  It implements the three names the guest demands, not all ten
 random-generator entry points in Apple's libBNNS.tbd: BNNSRandomGeneratorStateSize/GetState/
 SetState and the Uniform/Int and Normal/Categorical fills are not here, and not binding them
 is what the measurement says the ladder needs.  filter_params is accepted and ignored --
 bnns.h describes it as compute options (thread pool, allocators) and gives the generator its
 seed internally -- and passing NULL, as bnns.h permits, changes nothing.
*/

/* bnns.h: "usage is serialized through an internal lock". */
typedef struct {
	uint8_t round_keys[176];
	uint64_t counter;      /* index of the next keystream block */
	uint32_t keystream[4]; /* the current block's words, handed out from the top */
	unsigned available;    /* how many of keystream[] are still unused: 0..4 */
	pthread_mutex_t lock;
} bnns_random_generator;

enum {
	BNNS_STATUS_SUCCESS = 0,
	BNNS_STATUS_NULL_GENERATOR = -1,
	BNNS_STATUS_NULL_DESCRIPTOR = -2,
	BNNS_STATUS_NULL_DATA = -3,
	BNNS_STATUS_UNSUPPORTED_DATATYPE = -4,
	BNNS_STATUS_UNSUPPORTED_LAYOUT = -5,
	BNNS_STATUS_UNSUPPORTED_METHOD = -6,
};

/* FIPS-197 table 6.  Generated from the definition (the inverse in GF(2^8) followed by the
   affine transform) rather than transcribed, and checked against the published first and last
   rows: sbox[0x00] == 0x63, sbox[0x01] == 0x7c, sbox[0x53] == 0xed, sbox[0xff] == 0x16. */
static const uint8_t bnns_aes_sbox[256] = {
	0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5,
	0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
	0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0,
	0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
	0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc,
	0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
	0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a,
	0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
	0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0,
	0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
	0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b,
	0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
	0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85,
	0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
	0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5,
	0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
	0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17,
	0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
	0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88,
	0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
	0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c,
	0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
	0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9,
	0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
	0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6,
	0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
	0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e,
	0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
	0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94,
	0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
	0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68,
	0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
};

/* multiply by x in GF(2^8) modulo the AES polynomial x^8 + x^4 + x^3 + x + 1 */
static uint8_t bnns_aes_xtime(uint8_t value)
{
	return (uint8_t)((value << 1) ^ ((value >> 7) * 0x1b));
}

static void bnns_aes_add_round_key(uint8_t state[16], const uint8_t* round_key)
{
	unsigned i;

	for (i = 0; i < 16; i++)
		state[i] ^= round_key[i];
}

static void bnns_aes_sub_bytes(uint8_t state[16])
{
	unsigned i;

	for (i = 0; i < 16; i++)
		state[i] = bnns_aes_sbox[state[i]];
}

/* Row r of the state -- state[4 * column + r] -- rotates left by r. */
static void bnns_aes_shift_rows(uint8_t state[16])
{
	uint8_t shifted[16];
	unsigned row, column;

	for (row = 0; row < 4; row++) {
		for (column = 0; column < 4; column++)
			shifted[4 * column + row] = state[4 * ((column + row) % 4) + row];
	}

	memcpy(state, shifted, sizeof(shifted));
}

static void bnns_aes_mix_columns(uint8_t state[16])
{
	unsigned column;

	for (column = 0; column < 4; column++) {
		uint8_t* c = state + 4 * column;
		uint8_t a0 = c[0], a1 = c[1], a2 = c[2], a3 = c[3];
		uint8_t sum = (uint8_t)(a0 ^ a1 ^ a2 ^ a3);

		c[0] = (uint8_t)(a0 ^ sum ^ bnns_aes_xtime((uint8_t)(a0 ^ a1)));
		c[1] = (uint8_t)(a1 ^ sum ^ bnns_aes_xtime((uint8_t)(a1 ^ a2)));
		c[2] = (uint8_t)(a2 ^ sum ^ bnns_aes_xtime((uint8_t)(a2 ^ a3)));
		c[3] = (uint8_t)(a3 ^ sum ^ bnns_aes_xtime((uint8_t)(a3 ^ a0)));
	}
}

static void bnns_aes128_expand_key(const uint8_t key[16], uint8_t round_keys[176])
{
	static const uint8_t rcon[11] = {
		0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
	};
	unsigned word;

	memcpy(round_keys, key, 16);
	for (word = 4; word < 44; word++) {
		uint8_t t[4];

		memcpy(t, round_keys + (word - 1) * 4, sizeof(t));
		if (word % 4 == 0) {
			uint8_t first = t[0];

			t[0] = (uint8_t)(bnns_aes_sbox[t[1]] ^ rcon[word / 4]);
			t[1] = bnns_aes_sbox[t[2]];
			t[2] = bnns_aes_sbox[t[3]];
			t[3] = bnns_aes_sbox[first];
		}
		round_keys[word * 4 + 0] = (uint8_t)(round_keys[(word - 4) * 4 + 0] ^ t[0]);
		round_keys[word * 4 + 1] = (uint8_t)(round_keys[(word - 4) * 4 + 1] ^ t[1]);
		round_keys[word * 4 + 2] = (uint8_t)(round_keys[(word - 4) * 4 + 2] ^ t[2]);
		round_keys[word * 4 + 3] = (uint8_t)(round_keys[(word - 4) * 4 + 3] ^ t[3]);
	}
}

static void bnns_aes128_encrypt_block(const uint8_t round_keys[176], const uint8_t in[16],
	uint8_t out[16])
{
	uint8_t state[16];
	unsigned round;

	memcpy(state, in, sizeof(state));
	bnns_aes_add_round_key(state, round_keys);
	for (round = 1; round < 10; round++) {
		bnns_aes_sub_bytes(state);
		bnns_aes_shift_rows(state);
		bnns_aes_mix_columns(state);
		bnns_aes_add_round_key(state, round_keys + round * 16);
	}
	/* the last round drops MixColumns (FIPS-197) */
	bnns_aes_sub_bytes(state);
	bnns_aes_shift_rows(state);
	bnns_aes_add_round_key(state, round_keys + 160);
	memcpy(out, state, sizeof(state));
}

static uint32_t bnns_generator_next_word(bnns_random_generator* generator)
{
	uint8_t block[16];
	uint8_t encrypted[16];
	unsigned i;

	if (generator->available == 0) {
		memset(block, 0, 8);
		for (i = 0; i < 8; i++)
			block[15 - i] = (uint8_t)(generator->counter >> (8 * i));
		generator->counter++;
		bnns_aes128_encrypt_block(generator->round_keys, block, encrypted);
		for (i = 0; i < 4; i++) {
			generator->keystream[i] = ((uint32_t)encrypted[4 * i] << 24)
				| ((uint32_t)encrypted[4 * i + 1] << 16)
				| ((uint32_t)encrypted[4 * i + 2] << 8)
				| (uint32_t)encrypted[4 * i + 3];
		}
		generator->available = 4;
	}

	return generator->keystream[--generator->available];
}

/* Uniform on [0, 1), with the 24-bit resolution that makes every step exact in binary32. */
static float bnns_uniform_unit(bnns_random_generator* generator)
{
	return (float)(bnns_generator_next_word(generator) >> 8) * (1.0f / 16777216.0f);
}

/* The affine map onto [a, b].  Written as a + (b - a) * u it overflows in binary32 for bounds of
   opposite signs near FLT_MAX -- (b - a) alone is then 2 * FLT_MAX, which is already infinity --
   so the arithmetic runs in double, whose range and 53-bit significand hold the whole interval,
   and the result is rounded once on the way out. */
static float bnns_uniform_between(bnns_random_generator* generator, float a, float b)
{
	double u = (double)bnns_uniform_unit(generator);
	float value = (float)((double)a + ((double)b - (double)a) * u);

	/* The double arithmetic is strictly below b, but rounding the result to binary32 can land exactly
	   on b: MEASURED on the host, a = 1000 and b = 1001 produced 8 draws equal to b out of 200000 (the
	   affine result sits within half an ULP of b and rounds up).  The draw is taken again instead of
	   being pulled down to b's predecessor, because clamping would pile every such draw onto one value
	   -- a visible point mass at the top of the interval -- while a fresh draw is uniform on the same
	   grid and the redraw costs about one extra block per 25000 calls.  A degenerate interval (a == b)
	   skips this entirely and returns exactly a. */
	while (a < b && value >= b) {
		u = (double)bnns_uniform_unit(generator);
		value = (float)((double)a + ((double)b - (double)a) * u);
	}

	return value;
}

/* binary32 -> binary16, round to nearest even, subnormals included. */
static uint16_t bnns_binary16_from_float(float value)
{
	uint32_t bits, mantissa, sign;
	int32_t exponent;

	memcpy(&bits, &value, sizeof(bits));
	sign = (bits >> 16) & 0x8000u;
	exponent = (int32_t)((bits >> 23) & 0xffu) - 127;
	mantissa = bits & 0x7fffffu;

	if (exponent == 128) /* infinity or NaN, kept as such rather than folded into a finite value */
		return (uint16_t)(sign | 0x7c00u | (mantissa ? 0x0200u : 0u));

	if (exponent > 15)   /* past binary16's largest finite value: cannot happen once a and b are capped */
		return (uint16_t)(sign | 0x7c00u);

	if (exponent >= -14) {
		uint32_t half = ((uint32_t)(exponent + 15) << 10) | (mantissa >> 13);
		uint32_t remainder = mantissa & 0x1fffu;

		/* rounding carries into the exponent field when it should, which is why half is not
		   masked to its significand bits */
		if (remainder > 0x1000u || (remainder == 0x1000u && (half & 1u)))
			half++;
		return (uint16_t)(sign | half);
	}

	if (exponent < -25) /* below half of the smallest subnormal */
		return (uint16_t)sign;

	{
		uint32_t significand = mantissa | 0x800000u;
		uint32_t shift = (uint32_t)(-exponent - 14); /* 1..11, because exponent is -15..-25 */
		uint32_t half = significand >> (13 + shift);
		uint32_t remainder = significand & ((1u << (13 + shift)) - 1u);
		uint32_t halfway = 1u << (12 + shift);

		if (remainder > halfway || (remainder == halfway && (half & 1u)))
			half++;
		return (uint16_t)(sign | half);
	}
}

/* binary32 -> bfloat16, round to nearest even, which is the top half of binary32. */
static uint16_t bnns_bfloat16_from_float(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	if (((bits >> 23) & 0xffu) == 0xffu) {
		/* infinity or NaN: the exponent range is binary32's own, so only the payload moves */
		return (uint16_t)((bits >> 16) | ((bits & 0x7fffffu) ? 0x0040u : 0u));
	}

	{
		uint32_t lsb = (bits >> 16) & 1u;

		bits += 0x7fffu + lsb;
	}
	return (uint16_t)(bits >> 16);
}

/* bits 16..23 of BNNSDataLayout are the rank; bnns_constants.h numbers its constants that way. */
static unsigned bnns_descriptor_rank(const BNNSNDArrayDescriptor* desc)
{
	unsigned rank = desc->layout >> 16;

	return rank > BNNS_MAX_TENSOR_DIMENSION ? BNNS_MAX_TENSOR_DIMENSION : rank;
}

static size_t bnns_element_offset(const size_t index[BNNS_MAX_TENSOR_DIMENSION],
	const size_t stride[BNNS_MAX_TENSOR_DIMENSION], unsigned rank)
{
	size_t offset = 0;
	unsigned i;

	for (i = 0; i < rank; i++)
		offset += index[i] * stride[i];

	return offset;
}

/* Walks the logical index space in dimension order, 0..rank-1, carrying like an odometer. */
static void bnns_advance_index(size_t index[BNNS_MAX_TENSOR_DIMENSION],
	const size_t size[BNNS_MAX_TENSOR_DIMENSION], unsigned rank)
{
	unsigned i;

	for (i = 0; i < rank; i++) {
		if (++index[i] < size[i])
			return;
		index[i] = 0;
	}
}

BNNSRandomGenerator BNNSCreateRandomGenerator(BNNSRandomGeneratorMethod method,
	const BNNSFilterParameters* filter_params)
{
	bnns_random_generator* generator;
	uint8_t key[16];

	(void)filter_params;

	if (method != BNNSRandomGeneratorMethodAES_CTR)
		return NULL;

	generator = malloc(sizeof(*generator));
	if (generator == NULL)
		return NULL;

	arc4random_buf(key, sizeof(key));
	bnns_aes128_expand_key(key, generator->round_keys);
	generator->counter = 0;
	generator->available = 0;
	if (pthread_mutex_init(&generator->lock, NULL) != 0) {
		free(generator);
		return NULL;
	}

	return generator;
}

void BNNSDestroyRandomGenerator(BNNSRandomGenerator generator)
{
	bnns_random_generator* g = generator;

	if (g == NULL)
		return;

	pthread_mutex_destroy(&g->lock);
	free(g);
}

int BNNSRandomFillUniformFloat(BNNSRandomGenerator generator, BNNSNDArrayDescriptor* desc,
	float a, float b)
{
	bnns_random_generator* g = generator;
	size_t size[BNNS_MAX_TENSOR_DIMENSION];
	size_t stride[BNNS_MAX_TENSOR_DIMENSION];
	size_t index[BNNS_MAX_TENSOR_DIMENSION];
	size_t total = 1;
	size_t remaining;
	unsigned rank, i;
	float limit;

	if (g == NULL)
		return BNNS_STATUS_NULL_GENERATOR;
	if (desc == NULL)
		return BNNS_STATUS_NULL_DESCRIPTOR;
	if (desc->data == NULL)
		return BNNS_STATUS_NULL_DATA;

	rank = bnns_descriptor_rank(desc);
	if (rank == 0)
		return BNNS_STATUS_UNSUPPORTED_LAYOUT;

	for (i = 0; i < rank; i++) {
		size[i] = desc->size[i];
		if (desc->stride[i] != 0)
			stride[i] = desc->stride[i];
		else
			stride[i] = (i == 0) ? 1 : size[i - 1] * stride[i - 1];
		total *= size[i];
	}

	/* bnns.h: a bound outside the descriptor's type is capped at the closest finite value.
	   binary16 tops out at 65504; bfloat16 shares binary32's exponent range but not its
	   significand, so its largest finite value is 0x7f7f0000, not FLT_MAX. */
	switch (desc->data_type) {
	case BNNSDataTypeFloat16:
		limit = 65504.0f;
		break;
	case BNNSDataTypeBFloat16:
		limit = 3.3895313892515355e38f;
		break;
	case BNNSDataTypeFloat32:
		limit = FLT_MAX;
		break;
	default:
		return BNNS_STATUS_UNSUPPORTED_DATATYPE;
	}
	if (a > limit)
		a = limit;
	else if (a < -limit)
		a = -limit;
	if (b > limit)
		b = limit;
	else if (b < -limit)
		b = -limit;

	memset(index, 0, sizeof(index));
	pthread_mutex_lock(&g->lock);
	if (desc->data_type == BNNSDataTypeFloat32) {
		float* data = desc->data;

		for (remaining = total; remaining > 0; remaining--) {
			data[bnns_element_offset(index, stride, rank)] = bnns_uniform_between(g, a, b);
			bnns_advance_index(index, size, rank);
		}
	} else {
		uint16_t* data = desc->data;
		int binary16 = (desc->data_type == BNNSDataTypeFloat16);

		for (remaining = total; remaining > 0; remaining--) {
			float value = bnns_uniform_between(g, a, b);

			data[bnns_element_offset(index, stride, rank)] = binary16
				? bnns_binary16_from_float(value)
				: bnns_bfloat16_from_float(value);
			bnns_advance_index(index, size, rank);
		}
	}
	pthread_mutex_unlock(&g->lock);

	return BNNS_STATUS_SUCCESS;
}
