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

#import <PackageKit/PackageKit.h>
#import <PackageKit/PKPackageInfo.h>

/* Element and attribute names below were read from the native PackageKit
 * constants; they are not guessed from documentation. */
static NSString * const PKFormatVersionAttributeName = @"format-version";
static NSString * const PKIdentifierAttributeName = @"identifier";
static NSString * const PKVersionAttributeName = @"version";

static NSString * const PKBundleElementName = @"bundle";
static NSString * const PKBundleIdentifierAttributeName = @"CFBundleIdentifier";
static NSString * const PKBundleFallbackIdentifierAttributeName = @"id";
static NSString * const PKBundlePathAttributeName = @"path";
static NSString * const PKBundleStorageTypeAttributeName = @"storage-type";

static NSString * const PKBundleVersionElementName = @"bundle-version";
static NSString * const PKRelocateElementName = @"relocate";
static NSString * const PKBundleUpgradeElementName = @"upgrade-bundle";
static NSString * const PKBundleUpdateElementName = @"update-bundle";
static NSString * const PKBundleAtomicUpdateElementName = @"atomic-update-bundle";
static NSString * const PKStrictIdentifierElementName = @"strict-identifier";

static NSString * const PKBomPathPrefix = @"./";
static NSString * const PKRootPath = @"/";
static NSString * const PKUnknownIdentifierName = @"Unknown";
static NSString * const PKLooseComponentIdentifierPrefix = @"UnbundledPathsForPackage-";

/* A missing format-version attribute defaults to the native NSNumber 2. */
static NSString * const PKDefaultFormatVersionString = @"2";

@interface PKPackageInfo ()

+ (NSString *) _bundleIdentifierFromBundleElement: (NSXMLElement *) element;
+ (NSString *) _bundlePathFromBundleElement: (NSXMLElement *) element;

- (NSXMLElement *) _rootElement;
- (NSArray *) _topLevelBundleXMLElements;
- (NSArray *) _bundleVersionBundleXMLElements;
- (NSArray *) _relocateBundleXMLElements;
- (NSArray *) _bundleUpgradeBundleXMLElements;
- (NSArray *) _bundleUpdateBundleXMLElements;
- (NSArray *) _bundleAtomicUpdateBundleXMLElements;
- (NSArray *) _strictIdentifierBundleXMLElements;
- (NSArray *) _coalescedBundleElements;
- (NSArray *) _coalescedBundleElementsFromElements: (NSArray *) elements
                                 withParentElement: (NSXMLElement *) parentElement
                bundleIdentifierToBundleElementMap: (NSMutableDictionary *) bundleElements;
- (void) _parseBundleElements: (NSArray *) elements
             intoComponentMap: (NSMutableDictionary *) componentMap
                  withPackage: (PKPackage *) package;
- (void) _parseBundleDirectiveElementsWithComponentMap: (NSMutableDictionary *) componentMap;
- (void) _parseRelocateElementsWithComponentMap: (NSMutableDictionary *) componentMap;
- (NSString *) _looseComponentIdentifier;
- (NSDictionary *) _identifierToComponentMapWithPackage: (PKPackage *) package;

@end

static NSMutableDictionary *PKAttributesDictionaryFromElement (NSXMLElement *element)
{
    NSMutableDictionary *attributes = [NSMutableDictionary dictionary];

    for (NSXMLNode *attribute in [element attributes]) {
        [attributes setObject: [attribute stringValue] forKey: [attribute name]];
    }

    return attributes;
}

static NSXMLElement *PKLastElementForName (NSXMLElement *element, NSString *name, BOOL createIfNeeded)
{
    NSArray *elements = [element elementsForName: name];

    if ([elements count] != 0)
        return [elements lastObject];

    if (!createIfNeeded)
        return nil;

    NSXMLElement *created = [NSXMLElement elementWithName: name];
    [element addChild: created];

    return created;
}

/* Bundle paths are relative to the payload root: strip a BOM "./" prefix and
 * a leading slash, in that order. */
static NSString *PKRelativeBundlePath (NSString *path)
{
    if ([path hasPrefix: PKBomPathPrefix])
        path = [path substringFromIndex: [PKBomPathPrefix length]];

    if ([path hasPrefix: PKRootPath])
        path = [path substringFromIndex: 1];

    return path;
}

@implementation PKPackageInfo

- (instancetype) initWithData: (NSData *) data
{
    NSXMLDocument *document = [[NSXMLDocument alloc] initWithData: data options: 0 error: NULL];
    NSXMLElement *root;

    if (document == nil)
        return nil;

    self = [super init];
    if (self == nil)
        return nil;

    _xmlDocument = document;

    root = [self _rootElement];
    if ([root attributeForName: PKFormatVersionAttributeName] == nil) {
        [root addAttribute: [NSXMLNode attributeWithName: PKFormatVersionAttributeName
                                            stringValue: PKDefaultFormatVersionString]];
    }

    return self;
}

- (NSXMLElement *) _rootElement
{
    if (_xmlDocument == nil) {
        [NSException raise: NSInvalidArgumentException
                    format: @"-[%@ %@]: package info has no XML document",
                            [self class], NSStringFromSelector(_cmd)];
    }

    return [_xmlDocument rootElement];
}

- (NSString *) identifier
{
    return [[[self _rootElement] attributeForName: PKIdentifierAttributeName] stringValue];
}

- (NSString *) version
{
    return [[[self _rootElement] attributeForName: PKVersionAttributeName] stringValue];
}

- (NSNumber *) formatVersion
{
    NSString *value = [[[self _rootElement] attributeForName: PKFormatVersionAttributeName] stringValue];

    return [NSNumber numberWithDouble: (double) [value intValue]];
}

#pragma mark - Bundle graph

+ (NSString *) _bundleIdentifierFromBundleElement: (NSXMLElement *) element
{
    NSXMLNode *attribute = [element attributeForName: PKBundleIdentifierAttributeName];

    if (attribute == nil)
        attribute = [element attributeForName: PKBundleFallbackIdentifierAttributeName];

    return [attribute stringValue];
}

+ (NSString *) _bundlePathFromBundleElement: (NSXMLElement *) element
{
    NSString *path = [[element attributeForName: PKBundlePathAttributeName] stringValue];

    if (path == nil)
        return nil;

    return PKRelativeBundlePath(path);
}

- (NSArray *) _topLevelBundleXMLElements
{
    return [[self _rootElement] elementsForName: PKBundleElementName];
}

- (NSArray *) _bundleVersionBundleXMLElements
{
    NSXMLElement *container = PKLastElementForName([self _rootElement], PKBundleVersionElementName, NO);

    return [container elementsForName: PKBundleElementName];
}

- (NSArray *) _relocateBundleXMLElements
{
    NSMutableArray *bundles = [NSMutableArray array];

    for (NSXMLElement *element in [[self _rootElement] elementsForName: PKRelocateElementName]) {
        if ([element children] == nil || [[element children] count] == 0)
            continue;

        NSXMLNode *child = [element childAtIndex: 0];
        if ([[child name] isEqualToString: PKBundleElementName]) {
            [bundles addObject: child];
        } else {
            NSLog(@"PackageKit: ignoring a %@ element without a %@ child",
                  PKRelocateElementName, PKBundleElementName);
        }
    }

    return bundles;
}

- (NSArray *) _bundleUpgradeBundleXMLElements
{
    NSXMLElement *container = PKLastElementForName([self _rootElement], PKBundleUpgradeElementName, NO);

    return [container elementsForName: PKBundleElementName];
}

- (NSArray *) _bundleUpdateBundleXMLElements
{
    NSXMLElement *container = PKLastElementForName([self _rootElement], PKBundleUpdateElementName, NO);

    return [container elementsForName: PKBundleElementName];
}

- (NSArray *) _bundleAtomicUpdateBundleXMLElements
{
    NSXMLElement *container = PKLastElementForName([self _rootElement], PKBundleAtomicUpdateElementName, NO);

    return [container elementsForName: PKBundleElementName];
}

- (NSArray *) _strictIdentifierBundleXMLElements
{
    NSXMLElement *container = PKLastElementForName([self _rootElement], PKStrictIdentifierElementName, NO);

    return [container elementsForName: PKBundleElementName];
}

- (NSArray *) _coalescedBundleElements
{
    NSMutableArray *elements = [NSMutableArray array];

    [elements addObjectsFromArray: [self _topLevelBundleXMLElements]];
    [elements addObjectsFromArray: [self _bundleVersionBundleXMLElements]];
    [elements addObjectsFromArray: [self _relocateBundleXMLElements]];
    [elements addObjectsFromArray: [self _bundleUpgradeBundleXMLElements]];
    [elements addObjectsFromArray: [self _bundleUpdateBundleXMLElements]];

    return [self _coalescedBundleElementsFromElements: elements
                                    withParentElement: nil
                   bundleIdentifierToBundleElementMap: [NSMutableDictionary dictionary]];
}

- (NSArray *) _coalescedBundleElementsFromElements: (NSArray *) elements
                                 withParentElement: (NSXMLElement *) parentElement
                bundleIdentifierToBundleElementMap: (NSMutableDictionary *) bundleElements
{
    for (NSXMLElement *element in elements) {
        @autoreleasepool {
            NSMutableDictionary *attributes = PKAttributesDictionaryFromElement(element);

            /* A nested bundle path is relative to its packaged parent. */
            if (parentElement != nil && [attributes objectForKey: PKBundlePathAttributeName] != nil) {
                NSString *parentPath = [[parentElement attributeForName: PKBundlePathAttributeName] stringValue];
                NSString *path = [parentPath stringByAppendingPathComponent:
                                    [[attributes objectForKey: PKBundlePathAttributeName]
                                        stringByStandardizingPath]];

                [attributes setObject: path forKey: PKBundlePathAttributeName];
                [element setAttributesAsDictionary: attributes];
            }

            if ([[element name] isEqualToString: PKBundleElementName]) {
                NSString *identifier = [PKPackageInfo _bundleIdentifierFromBundleElement: element];

                if (identifier == nil) {
                    NSLog(@"PackageKit: ignoring a %@ element without an identifier", PKBundleElementName);
                    continue;
                }

                NSXMLElement *existing = [bundleElements objectForKey: identifier];
                if (existing != nil) {
                    /* ID-only directives must not erase packaged paths or
                     * versions; explicitly supplied later attributes win. */
                    for (NSXMLNode *attribute in [existing attributes]) {
                        NSString *name = [attribute name];
                        if ([attributes objectForKey: name] == nil)
                            [attributes setObject: [attribute stringValue] forKey: name];
                    }
                    [existing setAttributesAsDictionary: attributes];
                } else {
                    [bundleElements setObject: element forKey: identifier];
                }

                NSArray *children = [element elementsForName: PKBundleElementName];
                if (children != nil) {
                    [self _coalescedBundleElementsFromElements: children
                                             withParentElement: element
                            bundleIdentifierToBundleElementMap: bundleElements];
                }

                /* Nested bundles become top-level entries once coalesced. */
                [element setChildren: @[]];
                for (NSXMLElement *child in children)
                    [[self _rootElement] addChild: child];
            } else {
                NSLog(@"PackageKit: ignoring a non-%@ bundle element", PKBundleElementName);
            }
        }
    }

    return [bundleElements allValues];
}

- (void) _parseBundleElements: (NSArray *) elements
             intoComponentMap: (NSMutableDictionary *) componentMap
                  withPackage: (PKPackage *) package
{
    for (NSXMLElement *element in elements) {
        NSString *identifier = [PKPackageInfo _bundleIdentifierFromBundleElement: element];

        if (identifier == nil) {
            NSLog(@"PackageKit: ignoring a %@ element without an identifier", PKBundleElementName);
            continue;
        }

        NSDictionary *attributes = PKAttributesDictionaryFromElement(element);
        NSString *bundlePath = [PKPackageInfo _bundlePathFromBundleElement: element];

        if (bundlePath == nil) {
            NSLog(@"PackageKit: ignoring bundle %@ without a path", identifier);
            continue;
        }

        NSString *storageType = [[element attributeForName: PKBundleStorageTypeAttributeName] stringValue];
        PKBundleComponent *component = [[PKBundleComponent alloc] initWithIdentifier: identifier
                                                                   versionAttributes: attributes
                                                                          bundlePath: bundlePath
                                                                        storageType: storageType];

        if (component == nil) {
            NSLog(@"PackageKit: ignoring bundle %@ that could not be described", identifier);
            continue;
        }

        [component setPackage: package];
        [componentMap setObject: component forKey: identifier];
    }
}

- (void) _parseBundleDirectiveElementsWithComponentMap: (NSMutableDictionary *) componentMap
{
    for (NSXMLElement *element in [self _bundleVersionBundleXMLElements]) {
        NSString *identifier = [PKPackageInfo _bundleIdentifierFromBundleElement: element];

        if (identifier == nil) {
            NSLog(@"PackageKit: ignoring a %@ element without an identifier", PKBundleVersionElementName);
            continue;
        }

        [[componentMap objectForKey: identifier] setVersionChecked: YES];
    }

    for (NSXMLElement *element in [self _bundleUpgradeBundleXMLElements]) {
        NSString *identifier = [PKPackageInfo _bundleIdentifierFromBundleElement: element];

        if (identifier == nil) {
            NSLog(@"PackageKit: ignoring a %@ element without an identifier", PKBundleUpgradeElementName);
            continue;
        }

        [[componentMap objectForKey: identifier] setOverwriteAction: 1];
    }

    for (NSXMLElement *element in [self _bundleUpdateBundleXMLElements]) {
        NSString *identifier = [PKPackageInfo _bundleIdentifierFromBundleElement: element];

        if (identifier == nil) {
            NSLog(@"PackageKit: ignoring a %@ element without an identifier", PKBundleUpdateElementName);
            continue;
        }

        [[componentMap objectForKey: identifier] setOverwriteAction: 2];
    }

    for (NSXMLElement *element in [self _bundleAtomicUpdateBundleXMLElements]) {
        NSString *identifier = [PKPackageInfo _bundleIdentifierFromBundleElement: element];

        if (identifier == nil) {
            NSLog(@"PackageKit: ignoring a %@ element without an identifier", PKBundleAtomicUpdateElementName);
            continue;
        }

        [[componentMap objectForKey: identifier] setOverwriteAction: 3];
    }

    for (NSXMLElement *element in [self _strictIdentifierBundleXMLElements]) {
        NSString *identifier = [PKPackageInfo _bundleIdentifierFromBundleElement: element];

        if (identifier == nil) {
            NSLog(@"PackageKit: ignoring a %@ element without an identifier", PKStrictIdentifierElementName);
            continue;
        }

        [[componentMap objectForKey: identifier] setStrictIdentifier: YES];
    }
}

- (void) _parseRelocateElementsWithComponentMap: (NSMutableDictionary *) componentMap
{
    for (NSXMLElement *element in [self _relocateBundleXMLElements]) {
        NSString *identifier = [PKPackageInfo _bundleIdentifierFromBundleElement: element];

        if (identifier == nil) {
            NSLog(@"PackageKit: ignoring a %@ element without an identifier", PKRelocateElementName);
            continue;
        }

        [[componentMap objectForKey: identifier] setRelocatable: YES];
    }
}

- (NSString *) _looseComponentIdentifier
{
    return [PKLooseComponentIdentifierPrefix stringByAppendingString:
                ([self identifier] ?: PKUnknownIdentifierName)];
}

- (NSDictionary *) _identifierToComponentMapWithPackage: (PKPackage *) package
{
    NSMutableDictionary *componentMap = [NSMutableDictionary dictionary];
    NSString *looseIdentifier;
    PKComponent *looseComponent;

    [self _parseBundleElements: [self _coalescedBundleElements]
              intoComponentMap: componentMap
                   withPackage: package];
    [self _parseBundleDirectiveElementsWithComponentMap: componentMap];
    [self _parseRelocateElementsWithComponentMap: componentMap];

    /* Payload/script file containers are installation material; they are not
     * parsed, so no component subpaths are invented here. */
    looseIdentifier = [self _looseComponentIdentifier];
    looseComponent = [[PKComponent alloc] initWithIdentifier: looseIdentifier
                                                     version: ([self version] ?: @"")];
    [looseComponent setPackage: package];
    [componentMap setObject: looseComponent forKey: looseIdentifier];

    return componentMap;
}

@end
