#import <Foundation/Foundation.h>
@class FlyNesAppBridge;
NS_ASSUME_NONNULL_BEGIN
/// Synchronous platform boundary; callers run file operations off the UI thread.
@interface CatalogSourceService : NSObject
+ (instancetype)sharedInstance;
- (instancetype)initWithBridge:(FlyNesAppBridge *)bridge defaults:(NSUserDefaults *)defaults builtinURL:(nullable NSURL *)builtinURL;
- (NSArray<NSDictionary<NSString *, id> *> *)sources;
- (BOOL)prepareBuiltin:(NSError **)error NS_SWIFT_NAME(prepareBuiltin());
- (nullable NSString *)addURL:(NSURL *)url directory:(BOOL)directory error:(NSError **)error NS_SWIFT_NAME(add(url:directory:));
- (BOOL)rescanUUID:(NSString *)uuid error:(NSError **)error NS_SWIFT_NAME(rescan(uuid:));
- (BOOL)reauthorizeUUID:(NSString *)uuid URL:(NSURL *)url error:(NSError **)error NS_SWIFT_NAME(reauthorize(uuid:url:));
- (BOOL)removeUUID:(NSString *)uuid error:(NSError **)error NS_SWIFT_NAME(remove(uuid:));
- (nullable NSData *)romDataForCanonicalID:(NSString *)canonicalID error:(NSError **)error NS_SWIFT_NAME(romData(canonicalID:));
@end
NS_ASSUME_NONNULL_END
