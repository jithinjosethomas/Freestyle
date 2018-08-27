/*
 * Copyright (c) 2017  P. Arun Babu and Jithin Jose Thomas 
 * arun DOT hbni AT gmail DOT com, jithinjosethomas AT gmail DOT com
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
Some code is taken from D. J. Bernstein's
chacha-merged.c version 20080118
Public domain.
*/

#include "freestyle.h"

static void freestyle_column_round (u32 x[16])
{
	QR (x[0], x[4], x[ 8], x[12])
	QR (x[1], x[5], x[ 9], x[13])
	QR (x[2], x[6], x[10], x[14])
	QR (x[3], x[7], x[11], x[15])
}

static void freestyle_diagonal_round (u32 x[16])
{
	QR (x[0], x[5], x[10], x[15])
	QR (x[1], x[6], x[11], x[12])
	QR (x[2], x[7], x[ 8], x[13])
	QR (x[3], x[4], x[ 9], x[14])
}

static void freestyle_precompute_rounds (freestyle_ctx *x)
{
	u8 r;
	for (r = 1; r <= x->num_precomputed_rounds; ++r)
	{
		if (r & 1)
			freestyle_column_round   (x->input);
		else
			freestyle_diagonal_round (x->input);
	}
}

void freestyle_set_counter (freestyle_ctx *x, u32 counter)
{
	x->input[COUNTER] = PLUS(x->initial_counter, counter);
}

static void freestyle_keysetup (
		freestyle_ctx 	*x,
	const 	u8 		*key,
	const 	u16 		key_length_bits)
{
	const char *constants;

	x->input[KEY0] = U8TO32_LITTLE(key +  0);
	x->input[KEY1] = U8TO32_LITTLE(key +  4);
	x->input[KEY2] = U8TO32_LITTLE(key +  8);
	x->input[KEY3] = U8TO32_LITTLE(key + 12);

	if (key_length_bits == 256) /* recommended */
	{ 
		key += 16;
		constants = sigma;
	}
	else {
		constants = tau;
	}

	x->input[KEY4] = U8TO32_LITTLE(key +  0);
	x->input[KEY5] = U8TO32_LITTLE(key +  4);
	x->input[KEY6] = U8TO32_LITTLE(key +  8);
	x->input[KEY7] = U8TO32_LITTLE(key + 12);

	x->input[CONSTANT0] = U8TO32_LITTLE(constants +  0);
	x->input[CONSTANT1] = U8TO32_LITTLE(constants +  4);
	x->input[CONSTANT2] = U8TO32_LITTLE(constants +  8);
	x->input[CONSTANT3] = U8TO32_LITTLE(constants + 12);
}

static void freestyle_ivsetup (
		freestyle_ctx 	*x,
	const 	u8 		*iv,
	const	u32 		counter)
{
	x->input[COUNTER] = counter;

	x->input[IV0] = U8TO32_LITTLE(iv + 0);
	x->input[IV1] = U8TO32_LITTLE(iv + 4);
	x->input[IV2] = U8TO32_LITTLE(iv + 8);
}

static void freestyle_roundsetup (
		freestyle_ctx 	*x,
	const 	u32 		min_rounds,
	const 	u32 		max_rounds,
	const	u8		num_precomputed_rounds,
	const	u8 		pepper_bits,
	const	u8 		num_init_hashes)
{
	x->min_rounds 			= min_rounds;
	x->max_rounds 			= max_rounds;
	x->num_precomputed_rounds 	= num_precomputed_rounds;
	x->pepper_bits 			= pepper_bits;
	x->num_init_hashes 		= num_init_hashes;

	x->cipher_parameter[0] 	= ((x->min_rounds    	& 0xFFFF) << 16) // 16 bits 
				| ((x->max_rounds    	& 0xFFFF));	 // 16 bits

	x->cipher_parameter[1] 	= ((x->hash_interval   	& 0xFFFF) << 16) // 16 bits 
				| ((x->pepper_bits     	& 0x003F) << 10) //  6 bits
				| ((x->num_init_hashes 	& 0x003F) <<  4) //  6 bits
				| ((x->num_precomputed_rounds & 0xF));	 //  4 bits
	x->rand[0] = 0; 
	x->rand[1] = 0; 
	x->rand[2] = 0; 
	x->rand[3] = 0; 
	x->rand[4] = 0; 
	x->rand[5] = 0; 
	x->rand[6] = 0; 
	x->rand[7] = 0; 

	/* modify constant[0] and constant[1] */
	x->input[CONSTANT0] ^= x->cipher_parameter[0];
	x->input[CONSTANT1] ^= x->cipher_parameter[1];

	/* the number of ways a block of message can be encrypted */
	x->num_rounds_possible = 1 + (x->max_rounds - x->min_rounds)/x->hash_interval;
}

static u32 freestyle_random_round_number (const freestyle_ctx *x)
{
	u32 R;

	/* Generate a random number */
	R = x->min_rounds + arc4random_uniform (x->max_rounds - x->min_rounds + x->hash_interval);

	/* Make it a multiple of hash_interval */
	R = x->hash_interval * (u32)(R/x->hash_interval);

	assert (R >= x->min_rounds);
	assert (R <= x->max_rounds);

	return R;
}

static void freestyle_hashsetup (
		freestyle_ctx 	*x,
	const 	u32 		hash_interval)
{
	x->hash_interval = hash_interval;
}

static u16 freestyle_hash (
		freestyle_ctx	*x,
	const	u32 		output[16],
	const 	u16 		previous_hash,
	const	u32 		rounds)
{
	u32 temp1 = rounds;
	u32 temp2 = previous_hash;

	AXR (temp1, output[ 3], temp2, 16);
	AXR (temp2, output[ 6], temp1, 12);
	AXR (temp1, output[ 9], temp2,  8);
	AXR (temp2, output[12], temp1,  7);

	return (u16) XOR(temp1 & 0xFFFF, temp1 >> 16);
}

static u32 freestyle_process_block (
		freestyle_ctx	*x,	
	const 	u8 		*plaintext,
		u8 		*ciphertext,
		u8 		bytes,
		u16 		*expected_hash,
	const 	bool		do_encryption)
{
	u32 	i, r;

	u16 	hash = 0;

	u32 	output32[16];

	bool init = (plaintext == NULL) || (ciphertext == NULL);

	u32 rounds = do_encryption ? freestyle_random_round_number (x) : x->max_rounds;

	bool do_decryption = ! do_encryption;

	bool hash_collided [MAX_HASH_VALUE];

	memset (hash_collided, false, sizeof(hash_collided));

	for (i = 0; i < 16; ++i) {
		output32 [i] = x->input [i];
	}

	/* modify counter[0] */
	output32[COUNTER] ^= x->rand[0];

	for (r = x->num_precomputed_rounds + 1; r <= rounds; ++r)
	{
		if (r & 1)
			freestyle_column_round   (output32);
		else
			freestyle_diagonal_round (output32);

		if (r >= x->min_rounds && r % x->hash_interval == 0)
		{
			hash = freestyle_hash (x,output32,hash,r);

			while (hash_collided [hash]) {
				++hash;
			}

			hash_collided [hash] = true;

			if (do_decryption && hash == *expected_hash) {
				break;
			}
		}
	}

	if (do_encryption)
		*expected_hash = hash;
	else
		if (r > x->max_rounds)
			return 0;

	if (! init)
	{
		u8 output8 [64];

		for (i = 0; i < 16; ++i)
		{
			output32 [i] = PLUS(output32[i], x->input[i]);
	     		U32TO8_LITTLE (output8 + 4 * i, output32[i]);
		}

		for (i = 0; i < bytes; ++i) {
			ciphertext [i] = plaintext[i] ^ output8[i];
		}
        }

	return do_encryption ? rounds : r;
}

static void freestyle_increment_counter (freestyle_ctx *x)
{   
	x->input [COUNTER] = PLUSONE (x->input[COUNTER]);
}

static void freestyle_randomsetup_encrypt (freestyle_ctx *x)
{
	u32 	i;

	u32 	R [MAX_INIT_HASHES]; /* actual random rounds */
	u32 	CR[MAX_INIT_HASHES]; /* collided random rounds */

	u32	temp1;
	u32	temp2;

	const u32 saved_min_rounds		= x->min_rounds;
	const u32 saved_max_rounds		= x->max_rounds;
	const u32 saved_hash_interval   	= x->hash_interval;
	const u8  saved_num_precomputed_rounds 	= x->num_precomputed_rounds;

	u32 p;

	if (! x->is_pepper_set)
	{
		x->pepper = arc4random_uniform (
			x->pepper_bits == 32 ?  -1 : (1 << x->pepper_bits)
		);
	}

	/* set sane values for initalization */
	x->min_rounds 			= 12;
	x->max_rounds 			= 36;
	x->hash_interval 		= 1;
	x->num_precomputed_rounds 	= 4;

	for (i = 0; i < MAX_INIT_HASHES; ++i) {
		R [i] = CR[i] = 0;
	}

	/* initial pre-computed rounds */
	freestyle_precompute_rounds (x);

	/* save the counter after pre-computed rounds */
	x->initial_counter = x->input[COUNTER];

	/* add a random/user-set pepper to constant[3] */
	x->input[CONSTANT3] = PLUS(x->input[CONSTANT3], x->pepper); 

	for (i = 0; i < x->num_init_hashes; ++i)
	{
		R[i] = freestyle_encrypt_block (
			x,
			NULL,
			NULL,
			0,
			&x->init_hash [i]
		);

		freestyle_increment_counter (x);
	}

	if (! x->is_pepper_set)
	{
		/* set it back to its previous value */
		x->input[CONSTANT3] = MINUS(x->input[CONSTANT3], x->pepper); 

		/* check for any collisions between 0 and pepper */
		for (p = 0; p < x->pepper; ++p)
		{
			x->input[COUNTER] = x->initial_counter;

			for (i = 0; i < x->num_init_hashes; ++i)
			{
				CR[i] = freestyle_decrypt_block (
					x,
					NULL,
					NULL,
					0,
					&x->init_hash [i]
				);

				if (CR[i] == 0) {
					goto continue_loop_encrypt;	
				}

				freestyle_increment_counter (x);
			}

			/* found a collision; use the collided rounds */ 
			memcpy(R, CR, sizeof(R));
			break;

	continue_loop_encrypt:
			x->input[CONSTANT3] = PLUSONE(x->input[CONSTANT3]);
		}
	}

	for (i = 0; i < 8; ++i)
	{
		temp1 = 0;
		temp2 = 0;

		AXR (temp1, R[7*i + 0], temp2, 16);
		AXR (temp2, R[7*i + 1], temp1, 12);
		AXR (temp1, R[7*i + 2], temp2,  8);
		AXR (temp2, R[7*i + 3], temp1,  7);

		AXR (temp1, R[7*i + 4], temp2, 16);
		AXR (temp2, R[7*i + 5], temp1, 12);
		AXR (temp1, R[7*i + 6], temp2,  8);
		AXR (temp2, R[7*i + 0], temp1,  7);

		x->rand[i] = temp1; 
	}

	/* set user parameters back */
	x->min_rounds 			= saved_min_rounds;
	x->max_rounds 			= saved_max_rounds;
	x->hash_interval 		= saved_hash_interval; 
	x->num_precomputed_rounds 	= saved_num_precomputed_rounds;

	/* set counter to the value that was after pre-computed rounds */
	x->input[COUNTER] = x->initial_counter;

	/* modify nonce[0], nonce[1], and nonce[2] */
	x->input[IV0] ^= x->rand[1]; 
	x->input[IV1] ^= x->rand[2]; 
	x->input[IV2] ^= x->rand[3]; 

	/* modify constant[0], constant[1], constant[2], and constant[3] */
	x->input[CONSTANT0] ^= x->rand[4]; 
	x->input[CONSTANT1] ^= x->rand[5]; 
	x->input[CONSTANT2] ^= x->rand[6]; 
	x->input[CONSTANT3] ^= x->rand[7]; 

	/* Do pre-computation as specified by the user */
	freestyle_precompute_rounds (x);
}

static void freestyle_randomsetup_decrypt (freestyle_ctx *x)
{
	u32 	i;

	u32 	R [MAX_INIT_HASHES]; /* random rounds */

	u32	temp1;
	u32	temp2;

	const u32 saved_min_rounds		= x->min_rounds;
	const u32 saved_max_rounds		= x->max_rounds;
	const u32 saved_hash_interval   	= x->hash_interval;
	const u8  saved_num_precomputed_rounds 	= x->num_precomputed_rounds;

	u32 pepper;
	u32 max_pepper = (u32)(((u64)1 << x->pepper_bits) - 1); 

	/* set sane values for initalization */
	x->min_rounds 			= 12;
	x->max_rounds 			= 36;
	x->hash_interval 		= 1;
	x->num_precomputed_rounds 	= 4;

	for (i = 0; i < MAX_INIT_HASHES; ++i) {
		R[i] = 0;
	}

	/* initial pre-computed rounds */
	freestyle_precompute_rounds (x);

	/* save the counter after pre-computed rounds */
	x->initial_counter = x->input[COUNTER];

	/* if initial pepper is set, then add it to constant[3] */
	x->input [CONSTANT3] = PLUS(x->input[CONSTANT3], x->pepper);

	for (pepper = x->pepper; pepper <= max_pepper; ++pepper)
	{
		x->input[COUNTER] = x->initial_counter;

		for (i = 0; i < x->num_init_hashes; ++i)
		{
			R[i] = freestyle_decrypt_block (
				x,
				NULL,
				NULL,
				0,
				&x->init_hash [i]
			);

			if (R[i] == 0) {
				goto continue_loop_decrypt;
			}
			
			freestyle_increment_counter (x);
		}

		/* found all valid R[i]s */
		break;

continue_loop_decrypt:
		x->input[CONSTANT3] = PLUSONE(x->input[CONSTANT3]);
	}

	for (i = 0; i < 8; ++i)
	{
		temp1 = 0;
		temp2 = 0;

		AXR (temp1, R[7*i + 0], temp2, 16);
		AXR (temp2, R[7*i + 1], temp1, 12);
		AXR (temp1, R[7*i + 2], temp2,  8);
		AXR (temp2, R[7*i + 3], temp1,  7);

		AXR (temp1, R[7*i + 4], temp2, 16);
		AXR (temp2, R[7*i + 5], temp1, 12);
		AXR (temp1, R[7*i + 6], temp2,  8);
		AXR (temp2, R[7*i + 0], temp1,  7);

		x->rand[i] = temp1; 
	}

	/* set user parameters back */
	x->min_rounds 			= saved_min_rounds;
	x->max_rounds 			= saved_max_rounds;
	x->hash_interval 		= saved_hash_interval; 
	x->num_precomputed_rounds 	= saved_num_precomputed_rounds;

	/* set counter to the value that was after pre-computed rounds */
	x->input[COUNTER] = x->initial_counter;

	/* modify nonce[0], nonce[1], and nonce[2] */
	x->input[IV0] ^= x->rand[1]; 
	x->input[IV1] ^= x->rand[2]; 
	x->input[IV2] ^= x->rand[3]; 

	/* modify constant[0], constant[1], constant[2], and constant[3] */
	x->input[CONSTANT0] ^= x->rand[4]; 
	x->input[CONSTANT1] ^= x->rand[5]; 
	x->input[CONSTANT2] ^= x->rand[6]; 
	x->input[CONSTANT3] ^= x->rand[7]; 

	/* Do pre-computation as specified by the user */
	freestyle_precompute_rounds (x);
}

static void freestyle_init_common (
		freestyle_ctx 	*x,
	const 	u8 		*key,
	const 	u16		key_length_bits,
	const 	u8 		*iv,
	const 	u32 		min_rounds,
	const	u32		max_rounds,
	const	u8		num_precomputed_rounds,
	const	u32 		hash_interval,
	const	u8 		pepper_bits,
	const	u8 		num_init_hashes)
{	
	assert (min_rounds >= 1);
	assert (max_rounds <= 65536);

	assert (min_rounds <= max_rounds);

	assert (min_rounds % hash_interval == 0);
	assert (max_rounds % hash_interval == 0);

	assert (num_precomputed_rounds >= 1);
	assert (num_precomputed_rounds <= 16);
	assert (num_precomputed_rounds <= (min_rounds - 4));

	assert (pepper_bits >= 8);
	assert (pepper_bits <= 32);

	assert (num_init_hashes >= 7);
	assert (num_init_hashes <= 56);

	freestyle_keysetup 	(x, key, key_length_bits);
	freestyle_ivsetup 	(x, iv, 0);
	freestyle_hashsetup 	(x, hash_interval);
	freestyle_roundsetup 	(x, min_rounds, max_rounds, num_precomputed_rounds, pepper_bits, num_init_hashes);
}

void freestyle_init_encrypt (
		freestyle_ctx 	*x,
	const 	u8 		*key,
	const 	u16		key_length_bits,
	const 	u8 		*iv,
	const 	u32 		min_rounds,
	const	u32		max_rounds,
	const	u8		num_precomputed_rounds,
	const	u32 		hash_interval,
	const	u8 		pepper_bits,
	const	u8 		num_init_hashes)
{	
	freestyle_init_common (x, key, key_length_bits, iv, min_rounds, max_rounds, num_precomputed_rounds, hash_interval, pepper_bits, num_init_hashes);

	x->pepper		= 0;
	x->is_pepper_set 	= false;
	
	freestyle_randomsetup_encrypt(x);
}

void freestyle_init_encrypt_with_pepper (
		freestyle_ctx 	*x,
	const 	u8 		*key,
	const 	u16		key_length_bits,
	const 	u8 		*iv,
	const 	u32 		min_rounds,
	const	u32		max_rounds,
	const	u8		num_precomputed_rounds,
	const	u32 		hash_interval,
	const	u8 		pepper_bits,
	const	u8 		num_init_hashes,
	const	u32 		pepper)
{	
	freestyle_init_common (x, key, key_length_bits, iv, min_rounds, max_rounds, num_precomputed_rounds, hash_interval, pepper_bits, num_init_hashes);

	x->pepper 		= pepper;
	x->is_pepper_set 	= true;

	freestyle_randomsetup_encrypt(x);
}

void freestyle_init_decrypt (
		freestyle_ctx 	*x,
	const 	u8 		*key,
	const 	u16		key_length_bits,
	const 	u8 		*iv,
	const 	u32 		min_rounds,
	const	u32		max_rounds,
	const	u8		num_precomputed_rounds,
	const	u32 		hash_interval,
	const	u8 		pepper_bits,
	const	u8 		num_init_hashes,
	const	u16 		*init_hash)
{	
	freestyle_init_common (x, key, key_length_bits, iv, min_rounds, max_rounds, num_precomputed_rounds, hash_interval, pepper_bits, num_init_hashes);

	x->pepper		= 0;
	x->is_pepper_set 	= false;

	memcpy ( x->init_hash,
		 init_hash,
		 sizeof(x->init_hash)
	);

	freestyle_randomsetup_decrypt(x);
}

void freestyle_init_decrypt_with_pepper (
		freestyle_ctx 	*x,
	const 	u8 		*key,
	const 	u16		key_length_bits,
	const 	u8 		*iv,
	const 	u32 		min_rounds,
	const	u32		max_rounds,
	const	u8		num_precomputed_rounds,
	const	u32 		hash_interval,
	const	u8 		pepper_bits,
	const	u8 		num_init_hashes,
	const	u32 		pepper,
	const	u16 		*init_hash)
{	
	freestyle_init_common (x, key, key_length_bits, iv, min_rounds, max_rounds, num_precomputed_rounds, hash_interval, pepper_bits, num_init_hashes);

	x->pepper 		= pepper;
	x->is_pepper_set 	= true;
	
	memcpy ( x->init_hash,
		 init_hash,
		 sizeof(x->init_hash)
	);

	freestyle_randomsetup_decrypt(x);
}
	
int freestyle_process (
		freestyle_ctx 	*x,
	const 	u8 		*plaintext,
		u8 		*ciphertext,
		u32 		bytes,
		u16 		*hash,
	const 	bool 		do_encryption)
{
	u32 	i	= 0;
	u32 	block 	= 0;

	while (bytes > 0)
	{
	    u8 bytes_to_process = bytes >= 64 ? 64 : bytes;

	    u32 num_rounds = freestyle_process_block (
		x,
		plaintext  + i,
		ciphertext + i,
		bytes_to_process,
		&hash [block],
		do_encryption
	    );

	    if (num_rounds < x->min_rounds) {
		return -1;
	    }

	    i 	  += bytes_to_process;
	    bytes -= bytes_to_process;
	
            ++block;

	    freestyle_increment_counter(x);
	}

	return 0;
}
