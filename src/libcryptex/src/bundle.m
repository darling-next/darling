/* Copyright (C) 2026 Darling contributors. SPDX-License-Identifier: GPL-3.0-or-later */

#import "internal.h"

#import <CoreFoundation/CoreFoundation.h>
#import <Foundation/Foundation.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

/* Darwin-specific descriptor helper (libsystem_darwin); it is not part of the
 * standard SDK headers but is re-exported through libSystem. */
extern int claimfd_np(int *fdp, const uint64_t *gdid, unsigned int gdflags);

/* Native asset table order (r447 native-schema-inspection.stdout asset_tables).
 * Bit 0x10 marks descriptors the manifest schema never opens.  The ordinals
 * are the index into struct cryptex_s.assets. */
struct _cryptex_asset_descriptor {
	uint8_t flags;
	NSString *key;
};

static const struct _cryptex_asset_descriptor _cryptex_asset_descriptors[CRYPTEX_ASSET_COUNT] = {
	{ 0x29, @"CryptexDMG" },
	{ 0x00, @"LoadableTrustCache" },
	{ 0x01, @"Ap,CryptexInfoPlist" },
	{ 0x04, @"Image4Manifest" },
	{ 0x10, @"RootDirectory" },
	{ 0x10, @"Content" },
	{ 0x29, @"PersonalizedDMG" },
	{ 0x29, @"RootHash" },
	{ 0x00, @"Cryptex1,GenericTrustCache" },
	{ 0x01, @"Cryptex1,CryptexInfoPlist" },
	{ 0x29, @"Cryptex1,GenericDmg" },
	{ 0x29, @"Cryptex1,GenericVolume" },
	{ 0x04, @"Cryptex1Properties" },
};

#define CRYPTEX_E_MISSING_TYPED 212
#define CRYPTEX_E_NUMERIC 214
#define CRYPTEX_E_OVERFLOW EOVERFLOW
#define CRYPTEX_E_SIGNATURE_SIZE ERANGE

/* ---- typed property-list access ---------------------------------------- */

static id
_cryptex_value(NSDictionary *dictionary, NSString *key, Class expected)
{
	id value = [dictionary objectForKey:key];

	if (value == nil || ![value isKindOfClass:expected]) {
		return nil;
	}
	return value;
}

static NSDictionary *
_cryptex_dictionary(NSDictionary *dictionary, NSString *key)
{
	return (NSDictionary *)_cryptex_value(dictionary, key, [NSDictionary class]);
}

static NSArray *
_cryptex_array(NSDictionary *dictionary, NSString *key)
{
	return (NSArray *)_cryptex_value(dictionary, key, [NSArray class]);
}

static NSString *
_cryptex_string(NSDictionary *dictionary, NSString *key)
{
	return (NSString *)_cryptex_value(dictionary, key, [NSString class]);
}

/* Native typed extraction requires the exact numeric XPC type; a plist real
 * or boolean is not accepted as an integer. */
static NSNumber *
_cryptex_integer(NSDictionary *dictionary, NSString *key)
{
	id value = [dictionary objectForKey:key];

	if (value == nil || ![value isKindOfClass:[NSNumber class]]) {
		return nil;
	}
	if (CFGetTypeID((CFTypeRef)value) == CFBooleanGetTypeID()) {
		return nil;
	}
	if (CFNumberIsFloatType((CFNumberRef)value)) {
		return nil;
	}
	return (NSNumber *)value;
}

static NSNumber *
_cryptex_boolean(NSDictionary *dictionary, NSString *key)
{
	id value = [dictionary objectForKey:key];

	if (value == nil || CFGetTypeID((CFTypeRef)value) != CFBooleanGetTypeID()) {
		return nil;
	}
	return (NSNumber *)value;
}

/* ---- identity parsing --------------------------------------------------- */

/* Native numeric helper 0x288314964: strtoul base 0, trailing junk is an
 * error, a result wider than 32 bits becomes UINT32_MAX (errno is ignored by
 * the identity caller). */
static bool
_cryptex_parse_u32(const char *text, uint32_t *value)
{
	char *end = NULL;
	unsigned long parsed;

	if (text == NULL) {
		return false;
	}
	parsed = strtoul(text, &end, 0);
	if (end != NULL && *end != '\0') {
		return false;
	}
	if (parsed > UINT32_MAX) {
		parsed = UINT32_MAX;
	}
	*value = (uint32_t)parsed;
	return true;
}

static int
_cryptex_identity_parse(NSDictionary *dictionary, struct cryptex_identity *identity)
{
	NSString *ap_chip = _cryptex_string(dictionary, @"ApChipID");
	NSString *ap_board = _cryptex_string(dictionary, @"ApBoardID");
	NSString *ap_domain = _cryptex_string(dictionary, @"ApSecurityDomain");
	NSNumber *type = _cryptex_integer(dictionary, @"Cryptex1,Type");
	NSNumber *subtype = _cryptex_integer(dictionary, @"Cryptex1,SubType");
	NSString *product_class = _cryptex_string(dictionary, @"Cryptex1,ProductClass");
	NSString *cryptex_chip = _cryptex_string(dictionary, @"Cryptex1,ChipID");

	memset(identity, 0, sizeof(*identity));
	identity->version = 1;

	/* Ap fields are all strings or all absent (r447 identity_parse.asm). */
	if (ap_chip != nil || ap_board != nil || ap_domain != nil) {
		if (ap_chip == nil || ap_board == nil || ap_domain == nil) {
			return EINVAL;
		}
		if (!_cryptex_parse_u32([ap_chip UTF8String], &identity->chip) ||
		    !_cryptex_parse_u32([ap_board UTF8String], &identity->board) ||
		    !_cryptex_parse_u32([ap_domain UTF8String], &identity->security_domain)) {
			return CRYPTEX_E_NUMERIC;
		}
	} else {
		identity->flags |= 1;
	}

	/* Extended group: Type/SubType/ProductClass all present, ChipID optional,
	 * or the whole group absent. */
	if (type != nil || subtype != nil || product_class != nil || cryptex_chip != nil) {
		if (type == nil || subtype == nil || product_class == nil) {
			return EINVAL;
		}
		identity->flags |= 2;
		if (cryptex_chip != nil) {
			identity->flags |= 4;
			if (!_cryptex_parse_u32([cryptex_chip UTF8String], &identity->cryptex_chip)) {
				return CRYPTEX_E_NUMERIC;
			}
		}
		identity->type = (uint32_t)[type longLongValue];
		identity->subtype = (uint32_t)[subtype longLongValue];
		if (!_cryptex_parse_u32([product_class UTF8String], &identity->product_class)) {
			return CRYPTEX_E_NUMERIC;
		}
	}

	return 0;
}

/* Image4 chip conversion (native chip_conversion.decoded.asm C7-35). */
static int
_cryptex_identity_set_chip(struct cryptex_identity *identity, const img4_chip_instance_t *chip)
{
	if (identity == NULL || chip == NULL) {
		return EINVAL;
	}

	identity->flags = 0;
	identity->chip = chip->chid_chip;
	identity->board = chip->chid_bord;
	identity->security_domain = chip->chid_sdom;

	if ((chip->chid_omit & (IMG4_CHIP_INSTANCE_OMIT_FCHP |
	    IMG4_CHIP_INSTANCE_OMIT_TYPE | IMG4_CHIP_INSTANCE_OMIT_CLAS)) != 0) {
		return 0;
	}

	identity->flags = 6;
	identity->cryptex_chip = chip->chid_fchp;
	identity->type = chip->chid_type;
	if ((chip->chid_omit & IMG4_CHIP_INSTANCE_OMIT_STYP) == 0) {
		identity->subtype = chip->chid_styp;
	}
	identity->product_class = chip->chid_clas;
	return 0;
}

/* ---- identity comparators and selection -------------------------------- */

static bool
_cryptex_identity_base_equal(const struct cryptex_identity *request,
    const struct cryptex_identity *candidate)
{
	if (request == NULL || candidate == NULL) {
		return false;
	}
	if ((request->flags & 1) != (candidate->flags & 1)) {
		return false;
	}
	if (request->flags & 1) {
		return true;
	}
	return request->chip == candidate->chip &&
	    request->board == candidate->board &&
	    request->security_domain == candidate->security_domain;
}

static bool
_cryptex_identity_t_equal(const struct cryptex_identity *request,
    const struct cryptex_identity *candidate)
{
	if (request == NULL || candidate == NULL ||
	    request->version == 0 || candidate->version == 0) {
		return false;
	}
	if ((request->flags & 4) != (candidate->flags & 4)) {
		return false;
	}
	if ((request->flags & 4) == 0) {
		return true;
	}
	return request->cryptex_chip == candidate->cryptex_chip;
}

static bool
_cryptex_identity_s_equal(const struct cryptex_identity *request,
    const struct cryptex_identity *candidate)
{
	if (request == NULL || candidate == NULL ||
	    request->version == 0 || candidate->version == 0) {
		return false;
	}
	if ((request->flags & 2) != (candidate->flags & 2)) {
		return false;
	}
	if ((request->flags & 2) == 0) {
		return true;
	}
	return request->type == candidate->type &&
	    request->product_class == candidate->product_class;
}

/* Strict identity predicate P (native bundle_match.asm 0x2882f9f14). */
static bool
_cryptex_identity_matches(const struct cryptex_identity *candidate,
    const struct cryptex_identity *request)
{
	if (candidate == NULL || request == NULL) {
		return false;
	}
	if (!_cryptex_identity_base_equal(request, candidate)) {
		return false;
	}
	if (request->version == 0 || candidate->version == 0) {
		return true;
	}
	if ((request->flags & 2) && (candidate->flags & 2) &&
	    !_cryptex_identity_s_equal(request, candidate)) {
		return false;
	}
	if ((request->flags & 4) && (candidate->flags & 4) &&
	    !_cryptex_identity_t_equal(request, candidate)) {
		return false;
	}
	return true;
}

static const char *
_cryptex_variant_utf8(struct cryptex_s *cryptex)
{
	const char *utf8;

	if (cryptex->variant == nil) {
		return "";
	}
	utf8 = [cryptex->variant UTF8String];
	return utf8 != NULL ? utf8 : "";
}

/* Exact two-pass selection (native bundle_match.asm). */
static struct cryptex_s *
_cryptex_bundle_select(struct cryptex_s *entries,
    const struct cryptex_identity *request, const char *variant, bool allow_fallback)
{
	struct cryptex_s *candidate;
	struct cryptex_s *saved;

	for (candidate = entries; candidate != NULL; candidate = candidate->next) {
		if (!_cryptex_identity_matches(&candidate->identity, request)) {
			continue;
		}
		if (variant != NULL) {
			if (strcmp(variant, _cryptex_variant_utf8(candidate)) == 0) {
				return candidate;
			}
		} else if (candidate->flags & 1) {
			return candidate;
		}
	}

	if (!allow_fallback) {
		return NULL;
	}

	saved = NULL;
	for (candidate = entries; candidate != NULL; candidate = candidate->next) {
		const struct cryptex_identity *identity = &candidate->identity;
		bool strict = _cryptex_identity_matches(identity, request);
		bool variant_matches = variant != NULL &&
		    strcmp(variant, _cryptex_variant_utf8(candidate)) == 0;
		bool candidate_optional = identity->version != 0 && (identity->flags & 6) == 6;
		bool request_optional = request->version != 0 && (request->flags & 6) == 6;
		bool eligible = strict;

		if (!eligible && candidate_optional && request_optional &&
		    (identity->flags & 1) &&
		    _cryptex_identity_s_equal(request, identity) &&
		    _cryptex_identity_t_equal(request, identity)) {
			eligible = true;
		}
		if (!eligible && !candidate_optional && (identity->flags & 1)) {
			eligible = true;
		}
		if (!eligible) {
			continue;
		}
		if (!((candidate->flags & 1) || variant_matches)) {
			continue;
		}
		if (saved == NULL || variant_matches || strict) {
			saved = candidate;
		}
	}
	return saved;
}

/* ---- manifest payload population --------------------------------------- */

static bool
_cryptex_asset_present(struct cryptex_s *cryptex, enum cryptex_asset_kind kind)
{
	struct cryptex_asset *asset = &cryptex->assets[kind];

	return asset->path != NULL || asset->fd >= 0;
}

static size_t
_cryptex_asset_count(struct cryptex_s *cryptex)
{
	size_t count = 0;

	for (size_t i = 0; i < CRYPTEX_ASSET_COUNT; i++) {
		if (_cryptex_asset_present(cryptex, (enum cryptex_asset_kind)i)) {
			count++;
		}
	}
	return count;
}

static int
_cryptex_populate_signatures(struct cryptex_s *cryptex, NSArray *paths, int restore_fd)
{
	NSMutableArray *signatures = [[NSMutableArray alloc] initWithCapacity:[paths count]];
	int error = 0;

	if (signatures == nil) {
		return ENOMEM;
	}

	for (id item in paths) {
		const char *path;
		struct stat status;
		NSData *data;
		void *mapping;
		size_t size;
		int fd;

		if (![item isKindOfClass:[NSString class]]) {
			error = CRYPTEX_E_MISSING_TYPED;
			break;
		}
		path = [(NSString *)item UTF8String];
		if (path == NULL) {
			error = CRYPTEX_E_MISSING_TYPED;
			break;
		}
		fd = openat(restore_fd, path, O_RDONLY);
		if (fd < 0) {
			error = errno;
			break;
		}
		if (fstat(fd, &status) != 0) {
			error = errno;
			close(fd);
			break;
		}
		if (status.st_size < 0 || (uint64_t)status.st_size > UINT32_MAX) {
			error = CRYPTEX_E_SIGNATURE_SIZE;
			close(fd);
			break;
		}
		size = (size_t)status.st_size;
		mapping = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
		if (mapping == MAP_FAILED) {
			error = errno;
			close(fd);
			break;
		}
		data = [[NSData alloc] initWithBytes:mapping length:size];
		if (munmap(mapping, size) != 0) {
			error = errno;
			[data release];
			close(fd);
			break;
		}
		close(fd);
		if (data == nil) {
			error = ENOMEM;
			break;
		}
		[signatures addObject:data];
		[data release];
	}

	if (error != 0) {
		[signatures release];
		return error;
	}
	cryptex->signatures = signatures;
	return 0;
}

static int
_cryptex_populate_assets(struct cryptex_s *cryptex, NSDictionary *manifest, int restore_fd)
{
	for (size_t i = 0; i < CRYPTEX_ASSET_COUNT; i++) {
		const struct _cryptex_asset_descriptor *descriptor = &_cryptex_asset_descriptors[i];
		id entry_value;
		NSDictionary *entry;
		NSDictionary *info;
		NSString *path;
		int fd;
		int error;

		if (descriptor->flags & 0x10) {
			continue;
		}
		entry_value = [manifest objectForKey:descriptor->key];
		if (entry_value == nil) {
			continue;
		}
		entry = _cryptex_dictionary(manifest, descriptor->key);
		if (entry == nil) {
			return CRYPTEX_E_MISSING_TYPED;
		}
		info = _cryptex_dictionary(entry, @"Info");
		if (info == nil) {
			return CRYPTEX_E_MISSING_TYPED;
		}
		path = _cryptex_string(info, @"Path");
		if (path == nil || [path UTF8String] == NULL) {
			return CRYPTEX_E_MISSING_TYPED;
		}
		fd = openat(restore_fd, [path UTF8String], O_RDONLY);
		if (fd < 0) {
			return errno;
		}
		error = _cryptex_asset_claim(&cryptex->assets[i], &fd);
		if (error != 0) {
			if (fd >= 0) {
				close(fd);
			}
			return error;
		}
	}
	return 0;
}

static int
_cryptex_populate_properties(struct cryptex_s *cryptex, NSDictionary *identity)
{
	NSNumber *nonce_domain = _cryptex_integer(identity, @"Cryptex1,NonceDomain");
	NSString *version = _cryptex_string(identity, @"Cryptex1,Version");
	NSNumber *subtype = _cryptex_integer(identity, @"Cryptex1,SubType");
	NSString *preauthorization = _cryptex_string(identity, @"Cryptex1,PreauthorizationVersion");
	NSNumber *use_product_class = _cryptex_boolean(identity, @"Cryptex1,UseProductClass");
	uint64_t value;
	NSString *copy;

	if (nonce_domain == nil || version == nil || preauthorization == nil) {
		return EINVAL;
	}
	value = (uint64_t)[nonce_domain longLongValue];
	if ((value >> 32) != 0) {
		return CRYPTEX_E_OVERFLOW;
	}
	cryptex->nonce_domain = (uint32_t)value;

	if (subtype != nil) {
		value = (uint64_t)[subtype longLongValue];
		if ((value >> 32) != 0) {
			return EINVAL;
		}
		cryptex->subtype = (uint32_t)value;
	}

	copy = [version copy];
	if (copy == nil) {
		return ENOMEM;
	}
	cryptex->version = copy;

	copy = [preauthorization copy];
	if (copy == nil) {
		return ENOMEM;
	}
	cryptex->preauthorization_version = copy;

	cryptex->use_product_class = use_product_class != nil && [use_product_class boolValue];
	return 0;
}

/* ---- manifest parsing --------------------------------------------------- */

static int
_cryptex_bundle_build_entries(struct cryptex_bundle_s *bundle)
{
	NSArray *identities = _cryptex_array(bundle->manifest, @"BuildIdentities");

	if (identities == nil) {
		return CRYPTEX_E_MISSING_TYPED;
	}

	for (id item in identities) {
		NSDictionary *identity_dictionary;
		struct cryptex_identity identity;
		NSDictionary *info;
		NSString *variant_string;
		const char *variant;
		NSDictionary *manifest;
		NSArray *signatures;
		struct cryptex_s *cryptex;
		bool legacy_group;
		bool cryptex1_group;
		int error;

		if (![item isKindOfClass:[NSDictionary class]]) {
			return CRYPTEX_E_MISSING_TYPED;
		}
		identity_dictionary = (NSDictionary *)item;

		error = _cryptex_identity_parse(identity_dictionary, &identity);
		if (error != 0) {
			return error;
		}

		info = _cryptex_dictionary(identity_dictionary, @"Info");
		if (info == nil) {
			return CRYPTEX_E_MISSING_TYPED;
		}
		variant_string = _cryptex_string(info, @"Variant");
		variant = variant_string != nil ? [variant_string UTF8String] : NULL;
		if (variant_string != nil && variant == NULL) {
			variant = "";
		}

		/* Duplicate identities are rejected, not silently replaced. */
		if (_cryptex_bundle_select(bundle->entries, &identity, variant, false) != NULL) {
			return EINVAL;
		}

		manifest = _cryptex_dictionary(identity_dictionary, @"Manifest");
		if (manifest == nil) {
			return CRYPTEX_E_MISSING_TYPED;
		}
		signatures = _cryptex_array(identity_dictionary, @"Signatures");
		if (signatures == nil) {
			return CRYPTEX_E_MISSING_TYPED;
		}

		cryptex = _cryptex_alloc();
		if (cryptex == NULL) {
			return ENOMEM;
		}
		cryptex->identity = identity;
		cryptex->flags = variant_string == nil ? 1 : 0;
		if (variant_string != nil) {
			cryptex->variant = [variant_string copy];
			if (cryptex->variant == nil) {
				[(id)cryptex release];
				return ENOMEM;
			}
		}

		error = _cryptex_populate_signatures(cryptex, signatures, bundle->restore_fd);
		if (error == 0) {
			error = _cryptex_populate_assets(cryptex, manifest, bundle->restore_fd);
		}
		if (error != 0) {
			[(id)cryptex release];
			return error;
		}

		/* An identity without any opened asset is skipped by native. */
		if (_cryptex_asset_count(cryptex) == 0) {
			[(id)cryptex release];
			continue;
		}

		legacy_group = _cryptex_asset_present(cryptex, CRYPTEX_ASSET_PERSONALIZED_DMG) ||
		    _cryptex_asset_present(cryptex, CRYPTEX_ASSET_ROOT_HASH) ||
		    _cryptex_asset_present(cryptex, CRYPTEX_ASSET_INFO) ||
		    _cryptex_asset_present(cryptex, CRYPTEX_ASSET_TRUST_CACHE);
		cryptex1_group = _cryptex_asset_present(cryptex, CRYPTEX_ASSET_GENERIC_DMG) ||
		    _cryptex_asset_present(cryptex, CRYPTEX_ASSET_GENERIC_VOLUME) ||
		    _cryptex_asset_present(cryptex, CRYPTEX_ASSET_GENERIC_INFO) ||
		    _cryptex_asset_present(cryptex, CRYPTEX_ASSET_GENERIC_TRUST_CACHE);
		if (legacy_group && cryptex1_group) {
			[(id)cryptex release];
			return ENOTSUP;
		}

		/* Native core_is_cryptex1 is true when at least one of the four
		 * Cryptex1 asset slots is present. */
		if (cryptex1_group) {
			error = _cryptex_populate_properties(cryptex, identity_dictionary);
			if (error != 0) {
				[(id)cryptex release];
				return error;
			}
		}

		cryptex->next = bundle->entries;
		bundle->entries = cryptex;
		_cryptex_assets_close(cryptex);
	}

	return 0;
}

static int
_cryptex_bundle_initialize(struct cryptex_bundle_s *bundle)
{
	struct stat status;
	NSAutoreleasePool *pool;
	NSData *data;
	id plist;
	void *mapping;
	size_t size;
	int manifest_fd;
	int error = 0;

	if (bundle->manifest != nil) {
		return 0;
	}

	if (bundle->directory_fd < 0) {
		NSArray *identities = [[NSArray alloc] init];
		NSDictionary *manifest = [[NSDictionary alloc] initWithObjectsAndKeys:
		    identities, @"BuildIdentities", nil];
		[identities release];
		bundle->manifest = manifest;
		if (manifest == nil) {
			return ENOMEM;
		}
		return _cryptex_bundle_build_entries(bundle);
	}

	if (bundle->restore_fd < 0) {
		bundle->restore_fd = openat(bundle->directory_fd, "Restore", O_RDONLY | O_DIRECTORY);
		if (bundle->restore_fd < 0) {
			return errno;
		}
	}

	manifest_fd = openat(bundle->restore_fd, "BuildManifest.plist", O_RDONLY);
	if (manifest_fd < 0) {
		return errno;
	}

	pool = [[NSAutoreleasePool alloc] init];
	if (fstat(manifest_fd, &status) != 0) {
		error = errno;
		goto out;
	}
	size = (size_t)status.st_size;
	mapping = mmap(NULL, size, PROT_READ, MAP_PRIVATE, manifest_fd, 0);
	if (mapping == MAP_FAILED) {
		error = errno;
		goto out;
	}
	data = [[NSData alloc] initWithBytesNoCopy:mapping length:size freeWhenDone:NO];
	plist = data != nil ? [NSPropertyListSerialization propertyListWithData:data
	    options:NSPropertyListImmutable format:NULL error:NULL] : nil;
	if (munmap(mapping, size) != 0) {
		error = errno;
	}
	[data release];
	if (error != 0) {
		goto out;
	}
	if (![plist isKindOfClass:[NSDictionary class]]) {
		error = CRYPTEX_E_MISSING_TYPED;
		goto out;
	}
	bundle->manifest = [(NSDictionary *)plist retain];

out:
	[pool drain];
	close(manifest_fd);
	if (error != 0) {
		return error;
	}
	return _cryptex_bundle_build_entries(bundle);
}

/* ---- public entry points ----------------------------------------------- */

cryptex_bundle_t
cryptex_bundle_create_with_directory(int *directory_fd, uint64_t flags)
{
	struct cryptex_bundle_s *bundle;

	if (directory_fd == NULL) {
		return NULL;
	}
	bundle = _cryptex_bundle_alloc();
	if (bundle == NULL) {
		return NULL;
	}
	bundle->flags = flags;
	bundle->directory_fd = claimfd_np(directory_fd, NULL, 0);
	return (cryptex_bundle_t)bundle;
}

int
cryptex_bundle_copy_cryptex_for_chip(cryptex_bundle_t bundle,
    const img4_chip_instance_t *chip, const char *variant, cryptex_t *result)
{
	@autoreleasepool {
		struct cryptex_bundle_s *internal = (struct cryptex_bundle_s *)bundle;
		struct cryptex_identity identity;
		struct cryptex_s *selected;
		int error;

		if (internal == NULL || result == NULL) {
			return EINVAL;
		}

		memset(&identity, 0, sizeof(identity));
		identity.version = 1;
		error = _cryptex_identity_set_chip(&identity, chip);
		if (error != 0) {
			return error;
		}

		error = _cryptex_bundle_initialize(internal);
		if (error != 0) {
			return error;
		}

		selected = _cryptex_bundle_select(internal->entries, &identity, variant, true);
		if (selected == NULL) {
			/* Outer cryptex code 8 (no identity) converts to ESRCH. */
			return ESRCH;
		}

		error = _cryptex_assets_open(selected);
		if (error != 0) {
			return error;
		}

		*result = (cryptex_t)[(id)selected retain];
		return 0;
	}
}
