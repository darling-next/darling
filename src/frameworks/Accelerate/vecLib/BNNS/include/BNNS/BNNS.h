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


#ifndef _BNNS_H_
#define _BNNS_H_

#include <stddef.h>
#include <stdint.h>

void* BNNSApplyVectorActivationLayer(void);
void* BNNSDequantize(void);
void* BNNSFilterApply(void);
void* BNNSFilterApplyBatch(void);
void* BNNSFilterCreateConvolutionLayer(void);
void* BNNSFilterCreateConvolutionWeightsTensorConversionLayer(void);
void* BNNSFilterCreateFullyConnectedLayer(void);
void* BNNSFilterCreateImageTensorConversionLayer(void);
void* BNNSFilterCreatePoolingLayer(void);
void* BNNSFilterCreateTensorConvolutionLayer(void);
void* BNNSFilterCreateVectorActivationLayer(void);
void* BNNSFilterDestroy(void);

/*
 The random number generator interface, transcribed from Apple's BNNS/bnns.h
 (macOS 12.0+, iOS 15.0+).  The declarations above carry their historical no-prototype
 form; these three cannot, because their implementations read the array descriptor.
 Only the shapes and constants these three entry points touch are declared here.

 Apple's header is the ABI reference: BNNSNDArrayDescriptor is passed by pointer and read
 field by field (flags/layout/size/stride/data/data_type/table_data/table_data_type/
 data_scale/data_bias, in this order, `void *`-sized pointers before the two floats), and
 `BNNSRandomGenerator` is an opaque pointer, so a client built against Apple's header and a
 client built against this one exchange identical values.
*/

#define BNNS_MAX_TENSOR_DIMENSION 8

typedef void* BNNSRandomGenerator;

typedef uint32_t BNNSNDArrayFlags;
typedef uint32_t BNNSDataLayout;
typedef uint32_t BNNSDataType;

typedef struct {
	BNNSNDArrayFlags flags;
	BNNSDataLayout layout;

	size_t size[BNNS_MAX_TENSOR_DIMENSION];
	size_t stride[BNNS_MAX_TENSOR_DIMENSION];

	void* data;
	BNNSDataType data_type;

	void* table_data;
	BNNSDataType table_data_type;

	float data_scale;
	float data_bias;
} BNNSNDArrayDescriptor;

typedef uint32_t BNNSRandomGeneratorMethod;

/* The only method Apple defines (bnns_constants.h); it is passed through and not stored. */
enum {
	BNNSRandomGeneratorMethodAES_CTR = 0,
};

/* The floating point members of BNNSDataType (bnns_constants.h). */
enum {
	BNNSDataTypeFloatBit = 0x10000,
	BNNSDataTypeFloat16 = 0x10000 | 16,
	BNNSDataTypeFloat32 = 0x10000 | 32,
	BNNSDataTypeBFloat16 = 0x10000 | 0x8000 | 16,
};

/* Opaque here, and only ever carried through: it is a bag of compute options (thread count,
   allocators) and Apple's own documentation gives the generator its seed internally. */
typedef struct BNNSFilterParameters BNNSFilterParameters;

BNNSRandomGenerator BNNSCreateRandomGenerator(BNNSRandomGeneratorMethod method,
	const BNNSFilterParameters* filter_params);
void BNNSDestroyRandomGenerator(BNNSRandomGenerator generator);
int BNNSRandomFillUniformFloat(BNNSRandomGenerator generator, BNNSNDArrayDescriptor* desc,
	float a, float b);

#endif
