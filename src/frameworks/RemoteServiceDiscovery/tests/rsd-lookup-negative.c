/* The measured broker-negative path: no com.apple.remoted broker in this guest must produce the
 * native client's XPC-error answer (NULL, errno 35), not a fabricated absence. */
#include <errno.h>
#include <stdio.h>

#include <xpc/xpc.h>

typedef const void *remote_device_t;
extern remote_device_t remote_device_copy_device_with_name(const char *name);

int main(void)
{
	errno = 0;
	remote_device_t device = remote_device_copy_device_with_name("no-such-device");
	int error = errno;
	printf("RSD_LOOKUP device=%p errno=%d\n", device, error);
	int ok = (device == NULL && error == 35);
	printf("RSD_RC=%d\n", ok ? 0 : 1);
	return ok ? 0 : 1;
}
