#import <Foundation/Foundation.h>
NS_ASSUME_NONNULL_BEGIN
@interface FlyNesCatalogPresentation : NSObject
+ (NSDictionary<NSString *, id> *)fieldsForFilename:(NSString *)filename
                                         entryPath:(NSString *)entryPath
                                    trustedBuiltin:(BOOL)trustedBuiltin;
+ (NSDictionary<NSString *, id> *)fieldsForFilename:(NSString *)filename
                                         entryPath:(NSString *)entryPath
                                    trustedBuiltin:(BOOL)trustedBuiltin
                                    indexedTitleEn:(NSString *)titleEn
                                indexedTitleZhHans:(NSString *)titleZhHans
                                           aliases:(NSString *)aliases;
+ (NSDictionary<NSString *, id> *)mergeFields:(NSDictionary<NSString *, id> *)first
                                        with:(NSDictionary<NSString *, id> *)second;
+ (NSDictionary<NSString *, NSString *> *)titleForFields:(NSDictionary<NSString *, id> *)fields
                                                locale:(NSString *)locale;
+ (BOOL)fields:(NSDictionary<NSString *, id> *)fields matchQuery:(NSString *)query;
@end
NS_ASSUME_NONNULL_END
