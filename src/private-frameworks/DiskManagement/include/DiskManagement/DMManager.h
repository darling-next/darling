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
#import <DiskArbitration/DiskArbitration.h>

/* The image-backed operations below are synchronous: they run the requested
 * DiskArbitration operation on the manager's own session and return after the
 * completion callback, or after the caller's timeout. They never report a
 * result the backing store did not produce. */
@interface DMManager : NSObject {
	/* Native DiskManagement has one @-typed instance variable here. Keeping
	 * the layout in the interface also keeps the i386 (fragile ABI) slice
	 * buildable. */
	id _instPriv;
}

+ (instancetype)sharedManagerForThread;

- (BOOL)mountDisk:(DADiskRef)disk
	entireDisk:(BOOL)entireDisk
	   atPath:(NSString *)path
	 readOnly:(BOOL)readOnly
 otherArguments:(NSArray *)otherArguments
	  timeout:(double)timeout
	errorDict:(NSDictionary **)errorDict;

- (BOOL)unmountVolumePath:(NSString *)path
		    force:(BOOL)force
		errorDict:(NSDictionary **)errorDict;

- (BOOL)ejectDisk:(DADiskRef)disk errorDict:(NSDictionary **)errorDict;

@end
