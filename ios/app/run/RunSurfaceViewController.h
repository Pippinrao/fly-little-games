#ifndef FLYNES_RUN_SURFACE_VIEW_CONTROLLER_H
#define FLYNES_RUN_SURFACE_VIEW_CONTROLLER_H

#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

@interface RunSurfaceViewController : UIViewController

@property(nonatomic, copy) NSString *canonicalId;
@property(nonatomic, copy, nullable) void (^onPauseCommand)(NSString *commandId);

- (void)reloadProductSettings;

@end

NS_ASSUME_NONNULL_END

#endif
