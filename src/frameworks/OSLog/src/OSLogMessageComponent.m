/*
 This file is part of Darling.

 Copyright (C) 2026 Darling Team

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

#import "OSLogPrivate.h"

@interface OSLogMessageComponent ()

/* The component's own values, as properties for the reason stated in OSLogPrivate.h. */
@property (nonatomic, copy) NSString *formatSubstring;
@property (nonatomic, copy) NSString *placeholder;
@property (nonatomic) OSLogMessageComponentArgumentCategory argumentCategory;
@property (nonatomic, copy, nullable) NSData *dataValue;
@property (nonatomic, strong, nullable) NSNumber *numberValue;
@property (nonatomic, copy, nullable) NSString *stringValue;

- (instancetype)_initWithFormatSubstring:(NSString *)formatSubstring
                             placeholder:(NSString *)placeholder
                                category:(OSLogMessageComponentArgumentCategory)category
                               dataValue:(nullable NSData *)dataValue
                             numberValue:(nullable NSNumber *)numberValue
                             stringValue:(nullable NSString *)stringValue;

@end

/* One piece of a decomposed message: the literal text between two placeholders, or a placeholder
 * and the value that went into it.  The value is held in the form the category names -- DATA, a
 * STRING, or a NUMBER that the three numeric getters read -- exactly as Apple's does, so the
 * getters for the categories a component is not are absent rather than fabricated. */
@implementation OSLogMessageComponent

- (instancetype)_initWithFormatSubstring:(NSString *)formatSubstring
                             placeholder:(NSString *)placeholder
                                category:(OSLogMessageComponentArgumentCategory)category
                               dataValue:(NSData *)dataValue
                             numberValue:(NSNumber *)numberValue
                             stringValue:(NSString *)stringValue
{
	self = [super init];
	if (self) {
		self.formatSubstring = formatSubstring;
		self.placeholder = placeholder;
		self.argumentCategory = category;
		self.dataValue = dataValue;
		self.numberValue = numberValue;
		self.stringValue = stringValue;
	}
	return self;
}

- (NSData *)argumentDataValue
{
	return self.argumentCategory == OSLogMessageComponentArgumentCategoryData ? self.dataValue : nil;
}

- (NSString *)argumentStringValue
{
	return self.argumentCategory == OSLogMessageComponentArgumentCategoryString ? self.stringValue : nil;
}

- (NSNumber *)argumentNumberValue
{
	switch (self.argumentCategory) {
	case OSLogMessageComponentArgumentCategoryInt64:
	case OSLogMessageComponentArgumentCategoryUInt64:
	case OSLogMessageComponentArgumentCategoryDouble:
		return self.numberValue;
	default:
		return nil;
	}
}

- (double)argumentDoubleValue
{
	return self.argumentCategory == OSLogMessageComponentArgumentCategoryDouble ? self.numberValue.doubleValue : 0.0;
}

- (int64_t)argumentInt64Value
{
	return self.argumentCategory == OSLogMessageComponentArgumentCategoryInt64 ? self.numberValue.longLongValue : 0;
}

- (uint64_t)argumentUInt64Value
{
	return self.argumentCategory == OSLogMessageComponentArgumentCategoryUInt64 ? self.numberValue.unsignedLongLongValue : 0;
}

- (NSString *)description
{
	return [NSString stringWithFormat:@"<%@: %p %@%@>", NSStringFromClass([self class]), self,
		self.formatSubstring,
		self.placeholder.length != 0
			? self.numberValue ?: self.stringValue ?: self.dataValue ?: self.placeholder : @""];
}

#pragma mark - NSCoding

+ (BOOL)supportsSecureCoding
{
	return YES;
}

- (void)encodeWithCoder:(NSCoder *)coder
{
	[coder encodeObject:self.formatSubstring forKey:@"formatSubstring"];
	[coder encodeObject:self.placeholder forKey:@"placeholder"];
	[coder encodeInteger:self.argumentCategory forKey:@"category"];
	[coder encodeObject:self.dataValue forKey:@"dataValue"];
	[coder encodeObject:self.numberValue forKey:@"numberValue"];
	[coder encodeObject:self.stringValue forKey:@"stringValue"];
}

- (instancetype)initWithCoder:(NSCoder *)coder
{
	self = [super init];
	if (self) {
		self.formatSubstring = [coder decodeObjectOfClass:[NSString class] forKey:@"formatSubstring"];
		self.placeholder = [coder decodeObjectOfClass:[NSString class] forKey:@"placeholder"];
		self.argumentCategory = (OSLogMessageComponentArgumentCategory)[coder decodeIntegerForKey:@"category"];
		self.dataValue = [coder decodeObjectOfClass:[NSData class] forKey:@"dataValue"];
		self.numberValue = [coder decodeObjectOfClass:[NSNumber class] forKey:@"numberValue"];
		self.stringValue = [coder decodeObjectOfClass:[NSString class] forKey:@"stringValue"];

		if (self.formatSubstring == nil) self.formatSubstring = @"";
		if (self.placeholder == nil) self.placeholder = @"";
	}
	return self;
}

@end
