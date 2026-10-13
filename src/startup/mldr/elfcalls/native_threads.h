#ifndef _ELFLOADER_NATIVE_THREADS_H_
#define _ELFLOADER_NATIVE_THREADS_H_

/*
 * Guest identity for host threads created by NATIVE libraries inside a guest process.
 * See native_threads.c for the measured defect this interposition exists for.
 */

/* mldr's own guest-thread creation (threads.c) installs the new thread's real guest TSD base itself, so that
 * path must not be wrapped by the interposer: the wrapper's synthetic array would only be thrown away. */
void __darling_native_thread_create_begin(void);
void __darling_native_thread_create_end(void);

/* Install THIS thread's own guest TSD base. Called by the interposer's thread entry before the thread's own
 * start routine runs; exposed for a caller that creates a host thread outside pthread_create. */
void __darling_native_thread_install_tsd(void);

#endif // _ELFLOADER_NATIVE_THREADS_H_
