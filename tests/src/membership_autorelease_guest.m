#import <Foundation/Foundation.h>
#include <membership.h>
#include <pwd.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#define REQUIRE(condition, label) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL membership autorelease: %s\n", label); \
        return 1; \
    } \
} while (0)

int main(void)
{
    const uid_t expected = getuid();
    struct passwd *user = getpwuid(expected);
    REQUIRE(user != NULL, "current user record");
    char username[1024];
    size_t username_length = strlen(user->pw_name);
    REQUIRE(username_length < sizeof(username), "current user name size");
    memcpy(username, user->pw_name, username_length + 1);
    uuid_t uuid;

    @autoreleasepool {
        @autoreleasepool {
            REQUIRE(mbr_identifier_to_uuid(ID_TYPE_USERNAME, username, username_length, uuid) == 0,
                    "user name to UUID");
        }
        uid_t actual = (uid_t)-1;
        int type = -1;
        @autoreleasepool {
            REQUIRE(mbr_uuid_to_id(uuid, &actual, &type) == 0, "UUID to ID");
            REQUIRE(type == ID_TYPE_UID && actual == expected, "UID roundtrip");
        }
        @autoreleasepool {
            uuid_t again;
            REQUIRE(mbr_identifier_to_uuid(ID_TYPE_USERNAME, username, username_length, again) == 0,
                    "subsequent user lookup");
            REQUIRE(uuid_compare(uuid, again) == 0, "stable UUID after pool drain");
        }
    }
    puts("MEMBERSHIP_AUTORELEASE_GUEST_OK");
    return 0;
}
