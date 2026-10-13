#include <AE/AE.h>

/*
 * AECreateDesc/AEDisposeDesc and the rest of the descriptor model live in AEDataModel.c, because
 * the AE names a load-time client demands (AEGetDescData, CreateObjSpecifier) read the descriptors
 * those calls produce -- a stub that answered noErr without creating one would have made them copy
 * the caller's uninitialised stack.
 *
 * AEInstallEventHandler stays here: it registers a handler with an Apple Event Manager, and this
 * guest has none (see AESendMessage in AEDataModel.c). Nothing dispatches through it.
 */

static int verbose = 0;
__attribute__((constructor)) static void initme(void) {
    verbose = getenv("STUB_VERBOSE") != NULL;
}

OSErr AEInstallEventHandler(
  AEEventClass theAEEventClass,
  AEEventID theAEEventID,
  AEEventHandlerUPP handler,
  SRefCon handlerRefcon,
  Boolean isSysHandler
) {
    if (verbose) puts("STUB: AEInstallEventHandler called");
    return noErr;
}
