#import <Foundation/Foundation.h>
NS_ASSUME_NONNULL_BEGIN
/// Uses the mirrored locale only; safe to call while the shared app mutex is held.
FOUNDATION_EXPORT NSString *FlyNesLocalizedString(NSString *key);
NS_ASSUME_NONNULL_END
