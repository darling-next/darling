#ifndef __AE_H__
#define __AE_H__

#include <AE/AEDataModel.h>
#include <AE/AppleEvents.h>

/*
 * The AE surface this sub-framework implements, with the Apple header and line each declaration and
 * constant was taken from (Xcode 26.3's MacOSX SDK: CoreServices.framework/Frameworks/AE.framework/
 * Headers). Each descriptor type and object-specifier key below is a value, so a client compiles
 * against exactly what the manager published, not against a re-spelling.
 */

/* AEDataModel.h:  the descriptor value types a type/enum descriptor carries, and the empty one. */
enum {
	typeNull = 'null',        /* AEDataModel.h:145 */
	typeType = 'type',        /* AEDataModel.h:131 */
	typeEnumerated = 'enum',  /* AEDataModel.h:130 (AERegistry.h:527 spells it typeEnumeration) */
};

/* AEObjects.h:  the type and the four keys of an object specifier record. */
enum {
	typeObjectSpecifier = 'obj ',  /* AEObjects.h:96 */
	keyAEDesiredClass = 'want',    /* AEObjects.h:62 */
	keyAEContainer = 'from',       /* AEObjects.h:63 */
	keyAEKeyForm = 'form',         /* AEObjects.h:64 */
	keyAEKeyData = 'seld',         /* AEObjects.h:65 */
};

OSErr AECreateDesc(DescType typeCode, const void *dataPtr, Size dataSize, AEDesc *result);
OSErr AEDisposeDesc(AEDesc *theAEDesc);

/* AEDataModel.h:2517 */
OSErr AEGetDescData(const AEDesc *theAEDesc, void *dataPtr, Size maximumSize);

/* AEPackObject.h:105 */
OSErr CreateObjSpecifier(
	DescType desiredClass,
	AEDesc *theContainer,
	DescType keyForm,
	AEDesc *keyData,
	Boolean disposeInputs,
	AEDesc *objSpecifier);

/* AEMach.h:212 */
OSErr AESendMessage(
	const AppleEvent *event,
	AppleEvent *reply,
	AESendMode sendMode,
	long timeOutInTicks);

#endif
