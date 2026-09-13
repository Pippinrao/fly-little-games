#ifndef FLYNES_RUN_SURFACE_VIEW_CONTROLLER_H
#define FLYNES_RUN_SURFACE_VIEW_CONTROLLER_H

#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

@interface RunSurfaceViewController : UIViewController

@property(nonatomic, copy) NSString *canonicalId;
@property(nonatomic, copy) NSString *gameTitle;
// Canonical ROM bytes resolved by the app bridge before presenting this controller.
@property(nonatomic, copy, nullable) NSData *romData;
@property(nonatomic, copy, nullable) void (^onPauseCommand)(NSString *commandId);

- (void)reloadProductSettings;
- (BOOL)restoreCheckpoint:(NSData *)checkpoint error:(NSError * _Nullable * _Nullable)error;
- (BOOL)resetGame:(NSError * _Nullable * _Nullable)error;

@end

NS_ASSUME_NONNULL_END

#endif
