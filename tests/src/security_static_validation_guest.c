#include <CommonCrypto/CommonDigest.h>
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/xattr.h>
#include <unistd.h>

/*
 * FileDiskRep stores a raw CodeDirectory in com.apple.cs.CodeDirectory;
 * SingleDiskRep signs the entire file. An ad-hoc signature needs no CMS,
 * certificate, signing identity, codesign_allocate, or external signing tool.
 * These are actual signature bytes consumed by Security, not a substitute
 * implementation of validation. Both checks below use default validation flags.
 */
static const char attribute[] = "com.apple.cs.CodeDirectory";
static const char identifier[] = "org.darling.tests.security-static-validation-guest";
static const unsigned char payload[] =
    "Darling Security static validation: this entire file is sealed.\n";

enum {
    /* v0x20001 ends after spare2; all multibyte fields are big-endian. */
    code_directory_header_size = 44,
    code_directory_hash_offset = code_directory_header_size + sizeof(identifier),
    code_directory_size = code_directory_hash_offset + CC_SHA256_DIGEST_LENGTH
};

static void put_be32(unsigned char *destination, uint32_t value)
{
    destination[0] = (unsigned char)(value >> 24);
    destination[1] = (unsigned char)(value >> 16);
    destination[2] = (unsigned char)(value >> 8);
    destination[3] = (unsigned char)value;
}

static int check_validity(const char *path, const char *phase, OSStatus expected)
{
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault,
        (const UInt8 *)path, (CFIndex)strlen(path), false);
    if (!url) {
        fprintf(stderr, "FAIL %s: create file URL\n", phase);
        return 0;
    }
    /* Never reuse a validation result cached in a previous SecStaticCode. */
    SecStaticCodeRef code = NULL;
    OSStatus status = SecStaticCodeCreateWithPath(url, kSecCSDefaultFlags, &code);
    CFRelease(url);
    if (status != errSecSuccess || !code) {
        fprintf(stderr, "FAIL %s: create static code status=%d object=%d\n",
            phase, (int)status, code != NULL);
        if (code) CFRelease(code);
        return 0;
    }

    CFErrorRef error = NULL;
    printf("SECURITY_STATIC_VALIDATION phase=%s checking\n", phase);
    fflush(stdout);
    status = SecStaticCodeCheckValidityWithErrors(code, kSecCSDefaultFlags,
        NULL, &error);
    printf("SECURITY_STATIC_VALIDATION phase=%s status=%d expected=%d\n",
        phase, (int)status, (int)expected);
    if (status != expected) {
        fprintf(stderr, "FAIL %s: validity status=%d expected=%d\n",
            phase, (int)status, (int)expected);
        if (error) CFShow(error);
    }
    if (error) CFRelease(error);
    CFRelease(code);
    return status == expected;
}

int main(void)
{
    char path[] = "/tmp/darling-security-static-validation-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) {
        perror("FAIL mkstemp");
        return 1;
    }
    int result = 1;
    unsigned char directory[code_directory_size] = {0};
    put_be32(directory + 0, UINT32_C(0xfade0c02)); /* CodeDirectory magic */
    put_be32(directory + 4, sizeof(directory));
    put_be32(directory + 8, UINT32_C(0x20001));
    put_be32(directory + 12, kSecCodeSignatureAdhoc);
    put_be32(directory + 16, code_directory_hash_offset);
    put_be32(directory + 20, code_directory_header_size);
    put_be32(directory + 24, 0); /* no special slots */
    put_be32(directory + 28, 1); /* one code page */
    put_be32(directory + 32, sizeof(payload) - 1);
    directory[36] = CC_SHA256_DIGEST_LENGTH;
    directory[37] = kSecCodeSignatureHashSHA256;
    directory[38] = 0; /* not an Apple platform binary */
    directory[39] = 12; /* 4096-byte pages; final page is partial */
    /* spare2 at offset 40 remains zero. */
    memcpy(directory + code_directory_header_size, identifier, sizeof(identifier));
    /*
     * The identifier also keeps the complete blob larger than the current
     * CodeDirectory C++ header, as required by this reader's validateBlob().
     * Fields added after v0x20001 are not present and must not be interpreted.
     */
    if (!CC_SHA256(payload, (CC_LONG)(sizeof(payload) - 1),
            directory + code_directory_hash_offset)) {
        fputs("FAIL SHA256\n", stderr);
        goto cleanup;
    }
    if (write(fd, payload, sizeof(payload) - 1) != (ssize_t)(sizeof(payload) - 1)) {
        perror("FAIL write sealed payload");
        goto cleanup;
    }
    if (fsetxattr(fd, attribute, directory, sizeof(directory), 0, XATTR_CREATE)) {
        perror("FAIL write CodeDirectory xattr");
        goto cleanup;
    }
    if (!check_validity(path, "intact", errSecSuccess))
        goto cleanup;

    /* Change content in place, keeping its length and signature unchanged. */
    const unsigned char changed = payload[0] ^ 1;
    if (pwrite(fd, &changed, sizeof(changed), 0) != (ssize_t)sizeof(changed)) {
        perror("FAIL tamper payload");
        goto cleanup;
    }
    if (!check_validity(path, "tampered", errSecCSSignatureFailed))
        goto cleanup;
    result = 0;

cleanup:
    if (close(fd)) {
        perror("FAIL close temporary file");
        result = 1;
    }
    if (unlink(path)) {
        perror("FAIL unlink temporary file");
        result = 1;
    }
    if (!result)
        puts("SECURITY_STATIC_VALIDATION_GUEST_OK");
    return result;
}
