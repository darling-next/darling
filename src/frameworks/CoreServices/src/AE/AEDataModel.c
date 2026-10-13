/*
This file is part of Darling.

Copyright (C) 2026 Darling Team

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

/*
 * The AE descriptor data model: AECreateDesc/AEDisposeDesc (Apple's AEDataModel.h), AEGetDescData
 * (the same header), CreateObjSpecifier (AEPackObject.h) and AESendMessage (AEMach.h). The AE
 * sub-framework is where a client's CoreServices dependency field points for all five, which the
 * CoreServices umbrella re-exports.
 *
 * WHY THE TWO STUBS IN stub.c HAD TO BECOME REAL. AECreateDesc answered noErr and left `result`
 * alone, and AEDisposeDesc answered noErr and freed nothing. AEGetDescData and CreateObjSpecifier
 * read the descriptors those calls produce, so with the stubs in place the honest implementation of
 * either would have copied whatever was in the caller's stack: a plausible wrong value. The
 * descriptor model is a fact this guest CAN have -- it is a type, a length and a byte buffer, with
 * no Apple Event Manager behind it -- so all five names are answered from the bytes the caller
 * handed over, and the one name that needs a transport (AESendMessage) fails observably instead.
 *
 * HOW THE DATA IS KEPT. This build resolves OPAQUE_TOOLBOX_STRUCTS to 1 (basic-headers/
 * ConditionalMacros.h: it is 1 whenever TARGET_API_MAC_CARBON is set), so AEDataStorage is the
 * incomplete `struct OpaqueAEDataStorageType *` and the bytes live behind a block this file defines:
 * a data pointer and a length. The data pointer is the FIRST member, which is also the layout a
 * build with OPAQUE_TOOLBOX_STRUCTS 0 needs (there AEDataStorage is `Ptr *` and a client reads the
 * bytes with `*desc->dataHandle`).
 *
 * A RECORD IS AN ARRAY OF AEKeyDesc, as AEDataModel.h declares it: `struct AEKeyDesc { AEKeyword
 * descKey; AEDesc descContent; }`. CreateObjSpecifier writes exactly the four fields
 * aetypes.ObjectSpecifier documents for an 'obj ' record (src/external/python/2.7, whose packer
 * built these records against the real manager on both big- and little-endian Macs):
 *
 *   'want'  a 'type' descriptor holding the desired class's four characters
 *   'form'  an 'enum' descriptor holding the key form's four characters
 *   'seld'  a copy of the key data descriptor, carrying the key data's own type
 *   'from'  a copy of the container descriptor ('null' when there is no container)
 *
 * The four characters are stored in the order they are written ('word' -> 77 6f 72 64), never as the
 * little-endian integer the C constant spells, because that is the byte order every AE client that
 * hands a FourCC string to AECreateDesc produces (aetypes.Type.__aepack__ -> AECreateDesc('type',
 * "word"), read back by aepack.unpack -> mktype(desc.data)).
 *
 * THE ONE NAME THAT CANNOT BE SERVED. AESendMessage needs an Apple Event transport: an address
 * descriptor resolved to a process, and a manager to deliver the event. This guest has neither --
 * nothing in the tree implements an AE port, no address type resolves, and AEInstallEventHandler
 * (stub.c) registers into no table -- so the function answers errAEUnknownAddressType, the family's
 * own "the target address type is not known" (CarbonCore's MacErrors.h:674). It is not noErr,
 * because a noErr would tell the caller an event was delivered when none was, and the two measured
 * DevToolsCore callers absorb a nonzero status exactly as failure: -send returns it, and
 * -sendAppleEventAndWaitForReplyForTicks: answers nil.
 */

#include <AE/AE.h>

#include <CarbonCore/MacErrors.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* One descriptor's storage, behind the header's opaque AEDataStorage. */
typedef struct {
	Ptr data;
	Size size;
} AEDescStorage;

static AEDescStorage* ae_storage(AEDataStorage handle)
{
	return (AEDescStorage*) handle;
}

/* The bytes of a descriptor's data, or NULL when it has none. */
static Ptr ae_desc_data(const AEDesc* theAEDesc)
{
	return theAEDesc->dataHandle == NULL ? NULL : ae_storage(theAEDesc->dataHandle)->data;
}

static Size ae_desc_data_size(const AEDesc* theAEDesc)
{
	return theAEDesc->dataHandle == NULL ? 0 : ae_storage(theAEDesc->dataHandle)->size;
}

/* A descriptor's bytes as one allocation: the bytes themselves, nothing in front of them. */
static Ptr ae_bytes_create(const void* bytes, Size size)
{
	Ptr copy;

	if (size <= 0)
		return NULL;
	if ((uint64_t) size > 0xFFFFFFFFull)
		return NULL;

	copy = (Ptr) malloc((size_t) size);
	if (copy == NULL)
		return NULL;

	if (bytes != NULL)
		memcpy(copy, bytes, (size_t) size);
	else
		memset(copy, 0, (size_t) size);

	return copy;
}

static void ae_desc_dispose(AEDesc* theAEDesc);

/* A descriptor built from bytes. Answers false when there is no memory, and leaves `result` empty
 * in every failing case. */
static Boolean ae_desc_create(DescType typeCode, const void* bytes, Size size, AEDesc* result)
{
	AEDescStorage* storage;
	Ptr copy;

	result->descriptorType = typeNull;
	result->dataHandle = NULL;

	if (size < 0 || (size > 0 && bytes == NULL))
		return false;

	copy = ae_bytes_create(bytes, size);
	if (size > 0 && copy == NULL)
		return false;

	storage = (AEDescStorage*) malloc(sizeof(AEDescStorage));
	if (storage == NULL)
	{
		if (copy != NULL)
			free(copy);
		return false;
	}

	storage->data = copy;
	storage->size = size;

	result->descriptorType = typeCode;
	result->dataHandle = (AEDataStorage) storage;
	return true;
}

/* A FourCC value as the four characters a type/enum descriptor carries. */
static void ae_fourcc_bytes(DescType value, unsigned char out[4])
{
	out[0] = (unsigned char) ((value >> 24) & 0xFF);
	out[1] = (unsigned char) ((value >> 16) & 0xFF);
	out[2] = (unsigned char) ((value >> 8) & 0xFF);
	out[3] = (unsigned char) (value & 0xFF);
}

/* The descriptor's own storage, deep when it is a record this file built: an 'obj ' record owns the
 * descriptors in its keyed list, so freeing the record frees them too (AEDisposeDesc's contract for
 * a record). Any other type is a leaf whose bytes are freed as one block. */
static void ae_desc_dispose(AEDesc* theAEDesc)
{
	if (theAEDesc == NULL || theAEDesc->dataHandle == NULL)
	{
		if (theAEDesc != NULL)
		{
			theAEDesc->descriptorType = typeNull;
			theAEDesc->dataHandle = NULL;
		}
		return;
	}

	if (theAEDesc->descriptorType == typeObjectSpecifier)
	{
		AEKeyDesc* keys = (AEKeyDesc*) ae_desc_data(theAEDesc);
		Size count = ae_desc_data_size(theAEDesc) / (Size) sizeof(AEKeyDesc);
		Size i;

		for (i = 0; i < count; i++)
			ae_desc_dispose(&keys[i].descContent);
	}

	if (ae_storage(theAEDesc->dataHandle)->data != NULL)
		free(ae_storage(theAEDesc->dataHandle)->data);
	free(ae_storage(theAEDesc->dataHandle));

	theAEDesc->descriptorType = typeNull;
	theAEDesc->dataHandle = NULL;
}

OSErr AECreateDesc(DescType typeCode, const void* dataPtr, Size dataSize, AEDesc* result)
{
	if (result == NULL)
		return paramErr;
	if (dataSize < 0 || (dataSize > 0 && dataPtr == NULL))
		return paramErr;

	if (!ae_desc_create(typeCode, dataPtr, dataSize, result))
		return memFullErr;

	return noErr;
}

OSErr AEDisposeDesc(AEDesc* theAEDesc)
{
	if (theAEDesc == NULL)
		return paramErr;

	ae_desc_dispose(theAEDesc);
	return noErr;
}

OSErr AEGetDescData(const AEDesc* theAEDesc, void* dataPtr, Size maximumSize)
{
	Size size;

	if (theAEDesc == NULL || dataPtr == NULL)
		return paramErr;

	size = ae_desc_data_size(theAEDesc);

	/* The documented limit: "The AEGetDescData function will not return more data than you specify
	 * in this parameter" (Apple's AEDataModel.h). A buffer that cannot hold the descriptor's whole
	 * data is the family's own errAEBufferTooSmall rather than a silent truncation. */
	if (size > maximumSize)
		return errAEBufferTooSmall;

	if (size > 0)
		memcpy(dataPtr, ae_desc_data(theAEDesc), (size_t) size);

	return noErr;
}

OSErr CreateObjSpecifier(
		DescType desiredClass,
		AEDesc* theContainer,
		DescType keyForm,
		AEDesc* keyData,
		Boolean disposeInputs,
		AEDesc* objSpecifier)
{
	unsigned char classBytes[4];
	unsigned char formBytes[4];
	AEKeyDesc keys[4];
	AEDesc constructed;
	Boolean built;

	if (objSpecifier == NULL || keyData == NULL)
		return paramErr;

	memset(keys, 0, sizeof(keys));
	ae_fourcc_bytes(desiredClass, classBytes);
	ae_fourcc_bytes(keyForm, formBytes);

	keys[0].descKey = keyAEDesiredClass;
	keys[1].descKey = keyAEKeyForm;
	keys[2].descKey = keyAEKeyData;
	keys[3].descKey = keyAEContainer;

	built = ae_desc_create(typeType, classBytes, sizeof(classBytes), &keys[0].descContent) &&
		ae_desc_create(typeEnumerated, formBytes, sizeof(formBytes), &keys[1].descContent) &&
		ae_desc_create(keyData->descriptorType, ae_desc_data(keyData),
			ae_desc_data_size(keyData), &keys[2].descContent);

	/* The container: a copy of the caller's descriptor, or the empty 'null' descriptor that means
	 * "look for it in the application" ("'from' object the object in which it is contained, or
	 * null" -- aetypes.ObjectSpecifier). */
	if (built && theContainer != NULL && theContainer->dataHandle != NULL)
		built = ae_desc_create(theContainer->descriptorType, ae_desc_data(theContainer),
			ae_desc_data_size(theContainer), &keys[3].descContent);
	else if (built)
	{
		keys[3].descContent.descriptorType = typeNull;
		keys[3].descContent.dataHandle = NULL;
	}

	built = built && ae_desc_create(typeObjectSpecifier, keys, sizeof(keys), &constructed);

	if (built)
	{
		*objSpecifier = constructed;

		if (disposeInputs)
		{
			AEDisposeDesc(theContainer);
			AEDisposeDesc(keyData);
		}
	}
	else
	{
		ae_desc_dispose(&keys[0].descContent);
		ae_desc_dispose(&keys[1].descContent);
		ae_desc_dispose(&keys[2].descContent);
		ae_desc_dispose(&keys[3].descContent);
	}

	return built ? noErr : memFullErr;
}

OSErr AESendMessage(
		const AppleEvent* event,
		AppleEvent* reply,
		AESendMode sendMode,
		long timeOutInTicks)
{
	/* See the file comment: this guest has no Apple Event transport, so no address descriptor
	 * resolves to a process and there is nothing to deliver the event to or wait on a reply from. */
	return errAEUnknownAddressType;
}
