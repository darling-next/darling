/*
This file is part of Darling.

Copyright (C) 2017 Lubos Dolezel

Darling is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Darling is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Darling.  If not, see <http://www.gnu.org/licenses/>.
*/

#import <Foundation/Foundation.h>

@class PKArchive;
@class PKPackageInfo;
@class PKBundleComponent;

/* Called for each packaged bundle, with the nearest packaged ancestor. */
typedef void (^PKBundleComponentEnumerator) (PKBundleComponent *component,
                                             PKBundleComponent *parent,
                                             BOOL *stop);

/* NSDictionary key: NSData with the expected archive table-of-contents digest. */
extern NSString * const PKPackageOptionValidateDigest;

/*
 * A flat XAR package: archive plus PackageInfo metadata.
 *
 * Implemented capability boundary: local flat-XAR package identity
 * (packageIdentifier/packageVersion), the packaged bundle graph and
 * enumerateBundleComponentsUsingBlock: with ascending bundlePath order,
 * nearest packaged ancestor and stop support.
 *
 * Not implemented, and deliberately not simulated: package signatures and
 * trust levels, authorization, installation preflight and installation
 * itself.  Parsing a package is not trust acceptance, and none of these
 * methods report a trusted or installed package.
 */
@interface PKPackage : NSObject

+ (instancetype) packageWithPath: (NSString *) path;
+ (instancetype) packageWithPath: (NSString *) path options: (NSDictionary *) options;
+ (instancetype) packageWithURL: (NSURL *) url;
+ (instancetype) packageWithURL: (NSURL *) url options: (NSDictionary *) options;

/* The concrete package readers, in the order the factory tries them. */
+ (NSArray *) _allPackageClasses;

/* Subclass hook: YES for every URL in the base class. */
+ (BOOL) canInitWithURL: (NSURL *) url;

- (instancetype) initWithURL: (NSURL *) url options: (NSDictionary *) options;

- (NSURL *) fileURL;
- (PKArchive *) archive;
- (PKPackageInfo *) packageInfo;

- (NSString *) packageIdentifier;
- (NSString *) packageVersion;

- (NSArray *) components;
- (id) componentForIdentifier: (NSString *) identifier;

- (void) enumerateBundleComponentsUsingBlock: (PKBundleComponentEnumerator) block;

@end
