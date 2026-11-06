/* Copyright (C) 2026 Darling contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <cryptex.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* These APIs select and retain files, not authenticate or mount their contents. */
static int failures;
static char root[] = "/private/var/tmp/cryptex-bundle.XXXXXX";
static char restore[1024];
#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "cryptex bundle: line %d: %s (errno=%d)\n", __LINE__, #x, errno); \
    ++failures; } } while (0)

static void put(const char *name, const char *bytes)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", restore, name);
    int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0640);
    CHECK(fd >= 0);
    if (fd >= 0) {
        size_t size = strlen(bytes);
        CHECK(write(fd, bytes, size) == (ssize_t)size);
        CHECK(fchmod(fd, 0640) == 0);
        CHECK(close(fd) == 0);
    }
}

static FILE *begin_manifest(void)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/BuildManifest.plist", restore);
    FILE *file = fopen(path, "w");
    CHECK(file != NULL);
    if (file) fputs("<?xml version=\"1.0\"?><plist version=\"1.0\"><dict>"
        "<key>BuildIdentities</key><array>", file);
    return file;
}

static void identity(FILE *file, const char *variant, const char *ticket,
                     int board, int partial)
{
    fputs("<dict><key>Info</key><dict>", file);
    if (variant) fprintf(file, "<key>Variant</key><string>%s</string>", variant);
    fputs("</dict>", file);
    if (board >= 0) {
        fprintf(file, "<key>ApBoardID</key><string>0x%x</string>", board);
        if (!partial) fputs("<key>ApChipID</key><string>0x44</string>"
            "<key>ApSecurityDomain</key><string>1</string>", file);
    }
    fputs("<key>Signatures</key><array/><key>Manifest</key><dict>"
        "<key>CryptexDMG</key><dict><key>Info</key><dict>"
        "<key>Path</key><string>payload.dmg</string></dict></dict>", file);
    if (ticket) fprintf(file, "<key>Image4Manifest</key><dict><key>Info</key>"
        "<dict><key>Path</key><string>%s</string></dict></dict>", ticket);
    fputs("</dict></dict>", file);
}

static void end_manifest(FILE *file)
{
    if (file) {
        fputs("</array></dict></plist>", file);
        CHECK(fclose(file) == 0);
    }
}

static cryptex_bundle_t bundle(void)
{
    int fd = open(root, O_RDONLY);
    CHECK(fd >= 0);
    cryptex_bundle_t result = cryptex_bundle_create_with_directory(&fd, 0);
    CHECK(result != NULL && fd == -1);
    if (fd >= 0) close(fd);
    return result;
}

static img4_chip_instance_t chip(unsigned board)
{
    img4_chip_instance_t value = {0};
    value.chid_version = IMG4_CHIP_INSTANCE_STRUCT_VERSION;
    value.chid_bord = board;
    value.chid_chip = 0x44;
    value.chid_sdom = 1;
    value.chid_omit = IMG4_CHIP_INSTANCE_OMIT_FCHP |
        IMG4_CHIP_INSTANCE_OMIT_TYPE | IMG4_CHIP_INSTANCE_OMIT_CLAS;
    return value;
}

static void copied_manifest(cryptex_t object, const char *expected)
{
    char path[1024], bytes[128] = {0};
    snprintf(path, sizeof(path), "%s/output.im4m", root);
    int fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0600);
    CHECK(fd >= 0);
    if (fd < 0) return;
    CHECK(cryptex_fcopy_personalized_manifest(object, fd) == 0);
    CHECK(fcntl(fd, F_GETFD) >= 0);
    CHECK(lseek(fd, 0, SEEK_SET) == 0);
    ssize_t count = read(fd, bytes, sizeof(bytes));
    CHECK(count == (ssize_t)strlen(expected));
    CHECK(count >= 0 && memcmp(bytes, expected, strlen(expected)) == 0);
    struct stat status = {0};
    CHECK(fstat(fd, &status) == 0);
    CHECK((status.st_mode & 0777) == 0640);
    CHECK(close(fd) == 0);
    CHECK(unlink(path) == 0);
}

static void selection_and_ownership(void)
{
    FILE *file = begin_manifest();
    if (!file) return;
    identity(file, NULL, "default.im4m", 0x22, 0);
    identity(file, "chosen", "chosen.im4m", 0x22, 0);
    end_manifest(file);
    cryptex_bundle_t b = bundle();
    img4_chip_instance_t request = chip(0x22);
    cryptex_t chosen = NULL, fallback = NULL;
    CHECK(cryptex_bundle_copy_cryptex_for_chip(b, &request, "chosen", &chosen) == 0);
    CHECK(cryptex_bundle_copy_cryptex_for_chip(b, &request, "absent", &fallback) == 0);
    os_release(b);
    if (chosen) { copied_manifest(chosen, "chosen manifest\n"); os_release(chosen); }
    if (fallback) { copied_manifest(fallback, "default manifest\n"); os_release(fallback); }
}

static void wildcard_fallback(void)
{
    FILE *file = begin_manifest();
    if (!file) return;
    identity(file, "chosen", "default.im4m", -1, 0);
    identity(file, "chosen", "chosen.im4m", 0x22, 0);
    end_manifest(file);
    cryptex_bundle_t b = bundle();
    img4_chip_instance_t request = chip(0x99);
    cryptex_t result = NULL;
    CHECK(cryptex_bundle_copy_cryptex_for_chip(b, &request, "chosen", &result) == 0);
    os_release(b);
    if (result) { copied_manifest(result, "default manifest\n"); os_release(result); }
}

static void descriptor_backed_attachment(void)
{
    FILE *file = begin_manifest();
    if (!file) return;
    identity(file, "chosen", NULL, 0x22, 0);
    end_manifest(file);
    cryptex_bundle_t b = bundle();
    img4_chip_instance_t request = chip(0x22);
    cryptex_t result = NULL;
    CHECK(cryptex_bundle_copy_cryptex_for_chip(b, &request, "chosen", &result) == 0);
    os_release(b);
    if (!result) return;
    CHECK(cryptex_fcopy_personalized_manifest(result, -1) == ENOENT);
    CHECK(cryptex_attach_personalized_manifest(result, -1) == EBADF);
    char path[1024];
    snprintf(path, sizeof(path), "%s/chosen.im4m", restore);
    int fd = open(path, O_RDONLY);
    CHECK(fd >= 0);
    CHECK(cryptex_attach_personalized_manifest(result, fd) == 0);
    CHECK(fcntl(fd, F_GETFD) >= 0);
    CHECK(cryptex_attach_personalized_manifest(result, fd) == EEXIST);
    CHECK(close(fd) == 0);
    copied_manifest(result, "chosen manifest\n");
    os_release(result);
}
static void cryptex1_identity(FILE *file, unsigned product_class,
                             uint64_t nonce, const char *ticket)
{
    fprintf(file, "<dict><key>Info</key><dict><key>Variant</key>"
        "<string>cryptex1</string></dict>"
        "<key>ApBoardID</key><string>0x22</string>"
        "<key>ApChipID</key><string>0x44</string>"
        "<key>ApSecurityDomain</key><string>1</string>"
        "<key>Cryptex1,ChipID</key><string>0x71</string>"
        "<key>Cryptex1,Type</key><integer>1</integer>"
        "<key>Cryptex1,SubType</key><integer>2</integer>"
        "<key>Cryptex1,ProductClass</key><string>0x%x</string>"
        "<key>Cryptex1,NonceDomain</key><integer>%" PRIu64 "</integer>"
        "<key>Cryptex1,Version</key><string>1</string>"
        "<key>Cryptex1,PreauthorizationVersion</key><string>0</string>"
        "<key>Signatures</key><array/><key>Manifest</key><dict>"
        "<key>Cryptex1,GenericDmg</key><dict><key>Info</key><dict>"
        "<key>Path</key><string>payload.dmg</string></dict></dict>"
        "<key>Image4Manifest</key><dict><key>Info</key><dict>"
        "<key>Path</key><string>%s</string></dict></dict>"
        "</dict></dict>", product_class, nonce, ticket);
}

static void cryptex1_selection_and_nonce_bounds(void)
{
    FILE *file = begin_manifest();
    if (!file) return;
    cryptex1_identity(file, 0x90, UINT32_MAX, "default.im4m");
    cryptex1_identity(file, 0x91, UINT32_MAX, "chosen.im4m");
    end_manifest(file);
    cryptex_bundle_t b = bundle();
    img4_chip_instance_t request = chip(0x22);
    request.chid_omit = 0;
    request.chid_fchp = 0x71;
    request.chid_type = 1;
    request.chid_styp = 2;
    request.chid_clas = 0x91;
    cryptex_t result = NULL;
    CHECK(cryptex_bundle_copy_cryptex_for_chip(b, &request, "cryptex1", &result) == 0);
    os_release(b);
    if (result) { copied_manifest(result, "chosen manifest\n"); os_release(result); }

    file = begin_manifest();
    if (!file) return;
    cryptex1_identity(file, 0x91, UINT64_C(0x100000000), "chosen.im4m");
    end_manifest(file);
    b = bundle();
    result = (cryptex_t)(uintptr_t)0x1234;
    CHECK(cryptex_bundle_copy_cryptex_for_chip(b, &request, "cryptex1", &result) == EOVERFLOW);
    CHECK(result == (cryptex_t)(uintptr_t)0x1234);
    os_release(b);
}


static void rejected_input_and_reopen(void)
{
    img4_chip_instance_t request = chip(0x22);
    cryptex_t sentinel = (cryptex_t)(uintptr_t)0x1234;
    FILE *file = begin_manifest();
    if (!file) return;
    identity(file, "chosen", "chosen.im4m", 0x22, 1);
    end_manifest(file);
    cryptex_bundle_t b = bundle();
    CHECK(cryptex_bundle_copy_cryptex_for_chip(b, &request, "chosen", &sentinel) == EINVAL);
    CHECK(sentinel == (cryptex_t)(uintptr_t)0x1234);
    os_release(b);
    file = begin_manifest();
    if (!file) return;
    identity(file, "chosen", "chosen.im4m", 0x22, 0);
    identity(file, "chosen", "default.im4m", 0x22, 0);
    end_manifest(file);
    b = bundle();
    CHECK(cryptex_bundle_copy_cryptex_for_chip(b, &request, "chosen", &sentinel) == EINVAL);
    CHECK(sentinel == (cryptex_t)(uintptr_t)0x1234);
    os_release(b);
    file = begin_manifest();
    if (!file) return;
    identity(file, "chosen", "chosen.im4m", 0x22, 0);
    end_manifest(file);
    b = bundle();
    CHECK(cryptex_bundle_copy_cryptex_for_chip(b, &request, "absent", &sentinel) == ESRCH);
    char path[1024];
    snprintf(path, sizeof(path), "%s/payload.dmg", restore);
    CHECK(unlink(path) == 0);
    CHECK(cryptex_bundle_copy_cryptex_for_chip(b, &request, "chosen", &sentinel) == ENOENT);
    CHECK(sentinel == (cryptex_t)(uintptr_t)0x1234);
    os_release(b);
}

int main(void)
{
    if (!mkdtemp(root)) return 2;
    snprintf(restore, sizeof(restore), "%s/Restore", root);
    CHECK(mkdir(restore, 0700) == 0);
    put("payload.dmg", "unverified image bytes\n");
    put("default.im4m", "default manifest\n");
    put("chosen.im4m", "chosen manifest\n");
    selection_and_ownership();
    wildcard_fallback();
    descriptor_backed_attachment();
    cryptex1_selection_and_nonce_bounds();
    rejected_input_and_reopen();
    const char *files[] = { "BuildManifest.plist", "default.im4m", "chosen.im4m" };
    for (unsigned i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", restore, files[i]);
        CHECK(unlink(path) == 0);
    }
    CHECK(rmdir(restore) == 0);
    CHECK(rmdir(root) == 0);
    printf("CRYPTEX_BUNDLE_FAILURES=%d\n", failures);
    if (!failures) puts("CRYPTEX_BUNDLE_SMOKE=PASS");
    return failures ? 1 : 0;
}
