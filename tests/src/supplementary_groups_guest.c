#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int failures;

static void check(int condition, const char *operation)
{
    if (!condition) {
        fprintf(stderr, "FAIL supplementary groups: %s (errno=%d)\n", operation, errno);
        ++failures;
    }
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    alarm(20);
    puts("SUPPLEMENTARY_GROUPS_BEGIN");

    int count = getgroups(0, NULL);
    check(count >= 0 && count <= NGROUPS_MAX, "query the group count");
    if (count < 0 || count > NGROUPS_MAX)
        return 1;

    struct {
        gid_t before;
        gid_t groups[NGROUPS_MAX];
        gid_t after;
    } result;
    memset(&result, 0xa5, sizeof(result));
    const gid_t sentinel = result.before;
    int copied = getgroups(NGROUPS_MAX, result.groups);
    check(copied == count, "count and list queries agree");
    check(result.before == sentinel && result.after == sentinel,
        "group query preserves adjacent memory");
    for (int i = count; i < NGROUPS_MAX; ++i)
        check(result.groups[i] == sentinel, "group query preserves the unused buffer");

    gid_t second[NGROUPS_MAX];
    memset(second, 0, sizeof(second));
    copied = getgroups(NGROUPS_MAX, second);
    check(copied == count && memcmp(result.groups, second, count * sizeof(gid_t)) == 0,
        "repeated queries preserve exact group identities");

    errno = 0;
    int invalid_size = -1;
    check(getgroups(invalid_size, second) == -1 && errno == EINVAL,
        "negative capacity reports EINVAL");
    if (count > 1) {
        errno = 0;
        check(getgroups(count - 1, second) == -1 && errno == EINVAL,
            "undersized capacity reports EINVAL");
    }

    printf("SUPPLEMENTARY_GROUPS failures=%d count=%d\n", failures, count);
    if (failures != 0)
        return 1;
    puts("SUPPLEMENTARY_GROUPS_GUEST_OK");
    return 0;
}
