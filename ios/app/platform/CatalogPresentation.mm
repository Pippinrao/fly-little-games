#import "CatalogPresentation.h"

namespace {
NSString *title_from_path(NSString *value) {
    NSString *normalized = [value stringByReplacingOccurrencesOfString:@"\\" withString:@"/"];
    NSString *base = [normalized componentsSeparatedByString:@"/"].lastObject ?: normalized;
    NSRange dot = [base rangeOfString:@"." options:NSBackwardsSearch];
    NSString *title = dot.location != NSNotFound && dot.location > 0 ? [base substringToIndex:dot.location] : base;
    return [title stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet].length ? title : value;
}
NSString *language(NSString *title) {
    if ([title rangeOfString:@"\\p{script=Han}" options:NSRegularExpressionSearch].location != NSNotFound) return @"zh";
    if ([title rangeOfString:@"[\\p{script=Latin}&&\\p{L}]" options:NSRegularExpressionSearch].location != NSNotFound) return @"en";
    return @"unknown";
}
NSDictionary *candidate(NSString *title, NSString *lang, NSInteger rank) {
    return @{@"value":title, @"language":lang, @"rank":@(rank)};
}
NSDictionary *fields(NSArray<NSDictionary *> *candidates, NSString *aliases) {
    NSMutableDictionary *chosen = [NSMutableDictionary dictionary];
    for (NSDictionary *value in candidates) {
        NSString *lang = value[@"language"];
        NSDictionary *old = chosen[lang];
        const NSInteger rank = [value[@"rank"] integerValue], oldRank = [old[@"rank"] integerValue];
        // CanonicalGame: verified metadata first, then outer filename before ZIP entry,
        // then literal value order. Unknown presentation keeps first candidate.
        if (!old || (![lang isEqualToString:@"unknown"] && (rank < oldRank ||
            (rank == oldRank && [value[@"value"] compare:old[@"value"] options:NSLiteralSearch] == NSOrderedAscending)))) {
            chosen[lang] = value;
        }
    }
    return @{@"titleEn":chosen[@"en"][@"value"] ?: @"",
             @"titleZhHans":chosen[@"zh"][@"value"] ?: @"",
             @"titleUnknown":chosen[@"unknown"][@"value"] ?: @"",
             @"titleCandidates":candidates, @"searchAliases":aliases};
}
}

@implementation FlyNesCatalogPresentation
+ (NSDictionary *)fieldsForFilename:(NSString *)filename entryPath:(NSString *)entryPath trustedBuiltin:(BOOL)trustedBuiltin {
    NSMutableArray *candidates = [NSMutableArray array];
    if (trustedBuiltin) {
        // These are the only translations in AndroidBuiltinCatalogAdapter's manifest.
        [candidates addObject:candidate(@"From Below", @"en", -1)];
        [candidates addObject:candidate(@"来自下方", @"zh", -1)];
    } else {
        if (filename.length) {
            NSString *title = title_from_path(filename);
            [candidates addObject:candidate(title, language(title), 0)];
        }
        if (entryPath.length) {
            NSString *title = title_from_path(entryPath);
            [candidates addObject:candidate(title, language(title), 1)];
        }
    }
    return fields(candidates, [NSString stringWithFormat:@"%@ %@", filename, entryPath]);
}
+ (NSDictionary *)mergeFields:(NSDictionary *)first with:(NSDictionary *)second {
    NSMutableArray *candidates = [NSMutableArray arrayWithArray:first[@"titleCandidates"] ?: @[]];
    for (NSDictionary *value in second[@"titleCandidates"] ?: @[]) {
        if (![candidates containsObject:value]) [candidates addObject:value];
    }
    return fields(candidates, [NSString stringWithFormat:@"%@ %@", first[@"searchAliases"] ?: @"", second[@"searchAliases"] ?: @""]);
}
+ (NSDictionary *)titleForFields:(NSDictionary *)fields locale:(NSString *)locale {
    const BOOL chinese = [locale.lowercaseString hasPrefix:@"zh"];
    NSString *primary = fields[chinese ? @"titleZhHans" : @"titleEn"] ?: @"";
    NSString *secondary = fields[chinese ? @"titleEn" : @"titleZhHans"] ?: @"";
    if (primary.length) return @{@"primary":primary, @"secondary":[secondary isEqualToString:primary] ? @"" : secondary};
    if (secondary.length) return @{@"primary":secondary, @"secondary":@""};
    NSString *unknown = fields[@"titleUnknown"] ?: @"";
    return @{@"primary":unknown.length ? unknown : chinese ? @"未命名游戏" : @"Untitled game", @"secondary":@""};
}
+ (BOOL)fields:(NSDictionary *)fields matchQuery:(NSString *)query {
    NSLocale *root = [NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"];
    NSString *needle = [[query stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet] lowercaseStringWithLocale:root];
    if (!needle.length) return YES;
    for (NSString *key in @[@"titleEn", @"titleZhHans", @"titleUnknown", @"searchAliases"]) {
        if ([[fields[key] ?: @"" lowercaseStringWithLocale:root] rangeOfString:needle].location != NSNotFound) return YES;
    }
    return NO;
}
@end
