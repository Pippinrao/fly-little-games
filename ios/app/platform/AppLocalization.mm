#import "AppLocalization.h"

NSString *FlyNesLocalizedString(NSString *key)
{
    NSString *tag = [NSUserDefaults.standardUserDefaults stringForKey:@"FlyNesLocaleTag"] ?: @"system";
    NSArray<NSString *> *preferences = [tag isEqualToString:@"system"] ? NSLocale.preferredLanguages : @[tag];
    // Resolve region aliases too: Android persists zh-CN, while iOS bundles zh-Hans.
    NSString *localization = [NSBundle preferredLocalizationsFromArray:@[@"en", @"zh-Hans"]
                                                       forPreferences:preferences].firstObject ?: @"en";
    NSString *path = [NSBundle.mainBundle pathForResource:localization ofType:@"lproj"];
    NSBundle *bundle = path ? [NSBundle bundleWithPath:path] : NSBundle.mainBundle;
    return [bundle localizedStringForKey:key value:nil table:nil];
}
