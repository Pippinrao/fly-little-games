#import <Foundation/Foundation.h>
#import "AppLocalization.h"
#include <cstdio>

int main() {
    @autoreleasepool {
        NSUserDefaults *defaults = NSUserDefaults.standardUserDefaults;
        NSString *previous = [defaults stringForKey:@"FlyNesLocaleTag"];
        NSArray<NSArray<NSString *> *> *cases = @[
            @[@"en", @"Done"], @[@"zh-Hans", @"完成"], @[@"zh-CN", @"完成"], @[@"en-US", @"Done"]];
        for (NSArray<NSString *> *test in cases) {
            [defaults setObject:test[0] forKey:@"FlyNesLocaleTag"];
            if (![FlyNesLocalizedString(@"common.done") isEqualToString:test[1]]) {
                fprintf(stderr, "FAIL: live locale %s returned %s\n", test[0].UTF8String,
                        FlyNesLocalizedString(@"common.done").UTF8String);
                if (previous) [defaults setObject:previous forKey:@"FlyNesLocaleTag"];
                else [defaults removeObjectForKey:@"FlyNesLocaleTag"];
                return 1;
            }
        }
        if (previous) [defaults setObject:previous forKey:@"FlyNesLocaleTag"];
        else [defaults removeObjectForKey:@"FlyNesLocaleTag"];
        puts("PASS: live English, Simplified Chinese, Android zh-CN, and regional English localization");
    }
}
