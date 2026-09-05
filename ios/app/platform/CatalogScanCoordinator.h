#ifndef FLYNES_CATALOG_SCAN_COORDINATOR_H
#define FLYNES_CATALOG_SCAN_COORDINATOR_H

#import <Foundation/Foundation.h>

#include <stdint.h>

NS_ASSUME_NONNULL_BEGIN

@interface CatalogScanCoordinator : NSObject

- (BOOL)scanBookmark:(NSData *)bookmark
          sourceUUID:(NSData *)sourceUUID
         sourceScope:(uint32_t)sourceScope
               error:(NSError * _Nullable * _Nullable)error;

@end

NS_ASSUME_NONNULL_END

#endif
