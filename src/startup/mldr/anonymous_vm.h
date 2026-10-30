#ifndef MLDR_ANONYMOUS_VM_H
#define MLDR_ANONYMOUS_VM_H
#include <darlingserver/anonymous-vm.h>
int mldr_vm_operation(const struct darling_vm_request*, struct darling_vm_result*);
/* Called with asynchronous signals blocked by the loader's existing fork scope.
 * Lock order: descriptor registry, then VM ledger. VM operations never take the
 * descriptor lock or call guest/RPC/libc allocation entrypoints. */
void mldr_vm_prefork(void);
void mldr_vm_postfork_parent(void);
void mldr_vm_postfork_child(void);
#endif
