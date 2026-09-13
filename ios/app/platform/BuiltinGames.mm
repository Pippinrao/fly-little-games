#import "BuiltinGames.h"

static NSString *const kFlyNesBuiltinGamesAssetName = @"builtin-games.json";
static const NSInteger kFlyNesBuiltinGamesSupportedSchema = 1;

@implementation FlyNesBuiltinGame

- (instancetype)initWithCanonicalId:(NSString *)canonicalId
                      assetFilename:(NSString *)assetFilename
                            titleEn:(NSString *)titleEn
                        titleZhHans:(NSString *)titleZhHans
                             credit:(NSString *)credit
                        licenseFile:(NSString *)licenseFile
                        licenseSpdx:(NSString *)licenseSpdx
                   licenseSourceUrl:(NSString *)licenseSourceUrl
                             mapper:(NSInteger)mapper {
    if ((self = [super init])) {
        _canonicalId = [canonicalId copy];
        _assetFilename = [assetFilename copy];
        _titleEn = [titleEn copy];
        _titleZhHans = [titleZhHans copy];
        _credit = [credit copy];
        _licenseFile = [licenseFile copy];
        _licenseSpdx = [licenseSpdx copy];
        _licenseSourceUrl = [licenseSourceUrl copy];
        _mapper = mapper;
    }
    return self;
}

@end

@implementation FlyNesBuiltinGames {
    NSArray<FlyNesBuiltinGame *> *_games;
}

+ (NSString *)assetName {
    return kFlyNesBuiltinGamesAssetName;
}

+ (NSString *)resourceNameForAssetFilename:(NSString *)assetFilename {
    if (![assetFilename hasSuffix:@".nes"]) return assetFilename;
    return [assetFilename substringToIndex:assetFilename.length - 4];
}

static FlyNesBuiltinGames *gShared = nil;

+ (instancetype)shared {
    @synchronized (self) {
        if (gShared == nil) {
            gShared = [self fromBundle] ?: [self empty];
        }
        return gShared;
    }
}

+ (void)installForTesting:(FlyNesBuiltinGames *)games {
    @synchronized (self) {
        gShared = games ?: [self empty];
    }
}

+ (instancetype)fromBundle {
    NSURL *url = [NSBundle.mainBundle URLForResource:@"builtin-games" withExtension:@"json"];
    if (url == nil) return nil;
    NSData *data = [NSData dataWithContentsOfURL:url];
    if (data == nil) return nil;
    NSError *error = nil;
    id parsed = [self parse:data error:&error];
    if (parsed == nil) {
        NSLog(@"[FlyNES] bundled game manifest is unusable: %@", error.localizedDescription);
        return nil;
    }
    return parsed;
}

+ (instancetype)empty {
    return [[self alloc] initWithGames:@[]];
}

- (instancetype)initWithGames:(NSArray<FlyNesBuiltinGame *> *)games {
    if ((self = [super init])) {
        _games = [games copy];
    }
    return self;
}

+ (instancetype)parse:(NSData *)json error:(NSError **)error {
    NSString *asset = kFlyNesBuiltinGamesAssetName;
    id root = [NSJSONSerialization JSONObjectWithData:json options:0 error:error];
    if (![root isKindOfClass:NSDictionary.class]) {
        if (error) {
            *error = [NSError errorWithDomain:@"FlyNESBuiltinGames" code:1 userInfo:@{
                NSLocalizedDescriptionKey: [NSString stringWithFormat:@"%@ is not a JSON object", asset]}];
        }
        return nil;
    }
    NSNumber *schema = root[@"schemaVersion"];
    if (![schema isKindOfClass:NSNumber.class] || schema.integerValue != kFlyNesBuiltinGamesSupportedSchema) {
        if (error) {
            *error = [NSError errorWithDomain:@"FlyNESBuiltinGames" code:2 userInfo:@{
                NSLocalizedDescriptionKey: [NSString stringWithFormat:
                    @"%@ schemaVersion %@ is not supported (expected %ld)",
                    asset, schema ?: @"(missing)", (long)kFlyNesBuiltinGamesSupportedSchema]}];
        }
        return nil;
    }
    NSArray *rows = root[@"games"];
    if (![rows isKindOfClass:NSArray.class] || rows.count == 0) {
        if (error) {
            *error = [NSError errorWithDomain:@"FlyNESBuiltinGames" code:3 userInfo:@{
                NSLocalizedDescriptionKey: [NSString stringWithFormat:@"%@ bundles no games", asset]}];
        }
        return nil;
    }

    NSMutableArray<FlyNesBuiltinGame *> *games = [NSMutableArray arrayWithCapacity:rows.count];
    for (NSUInteger index = 0; index < rows.count; index++) {
        NSDictionary *row = rows[index];
        if (![row isKindOfClass:NSDictionary.class]) {
            if (error) {
                *error = [NSError errorWithDomain:@"FlyNESBuiltinGames" code:4 userInfo:@{
                    NSLocalizedDescriptionKey: [NSString stringWithFormat:
                        @"%@ game #%lu is not an object", asset, (unsigned long)index]}];
            }
            return nil;
        }
        NSString *canonicalId = [row[@"canonicalId"] isKindOfClass:NSString.class] ? row[@"canonicalId"] : @"";
        NSString *assetFilename = [row[@"assetFilename"] isKindOfClass:NSString.class] ? row[@"assetFilename"] : @"";
        if (canonicalId.length == 0 || assetFilename.length == 0) {
            if (error) {
                *error = [NSError errorWithDomain:@"FlyNESBuiltinGames" code:5 userInfo:@{
                    NSLocalizedDescriptionKey: [NSString stringWithFormat:
                        @"%@ game #%lu is missing its id or asset", asset, (unsigned long)index]}];
            }
            return nil;
        }
        NSDictionary *license = [row[@"license"] isKindOfClass:NSDictionary.class] ? row[@"license"] : @{};
        [games addObject:[[FlyNesBuiltinGame alloc]
            initWithCanonicalId:canonicalId
                  assetFilename:assetFilename
                        titleEn:[row[@"titleEn"] isKindOfClass:NSString.class] ? row[@"titleEn"] : @""
                    titleZhHans:[row[@"titleZhHans"] isKindOfClass:NSString.class] ? row[@"titleZhHans"] : @""
                         credit:[row[@"credit"] isKindOfClass:NSString.class] ? row[@"credit"] : @""
                    licenseFile:[license[@"file"] isKindOfClass:NSString.class] ? license[@"file"] : @""
                    licenseSpdx:[license[@"spdx"] isKindOfClass:NSString.class] ? license[@"spdx"] : @""
               licenseSourceUrl:[license[@"sourceUrl"] isKindOfClass:NSString.class] ? license[@"sourceUrl"] : @""
                         mapper:[row[@"mapper"] isKindOfClass:NSNumber.class] ? [row[@"mapper"] integerValue] : 0]];
    }
    [games sortUsingComparator:^NSComparisonResult(FlyNesBuiltinGame *left, FlyNesBuiltinGame *right) {
        NSNumber *leftOrder = nil, *rightOrder = nil;
        for (NSDictionary *row in rows) {
            if ([row[@"canonicalId"] isEqual:left.canonicalId]) leftOrder = row[@"sortOrder"];
            if ([row[@"canonicalId"] isEqual:right.canonicalId]) rightOrder = row[@"sortOrder"];
        }
        return [(leftOrder ?: @0) compare:(rightOrder ?: @0)];
    }];
    return [[self alloc] initWithGames:games];
}

- (NSArray<FlyNesBuiltinGame *> *)all {
    return _games;
}

- (nullable FlyNesBuiltinGame *)byCanonicalId:(nullable NSString *)canonicalId {
    if (canonicalId.length == 0) return nil;
    for (FlyNesBuiltinGame *game in _games) {
        if ([game.canonicalId isEqualToString:canonicalId]) return game;
    }
    return nil;
}

- (nullable FlyNesBuiltinGame *)byAssetFilename:(nullable NSString *)assetFilename {
    if (assetFilename.length == 0) return nil;
    for (FlyNesBuiltinGame *game in _games) {
        if ([game.assetFilename isEqualToString:assetFilename]) return game;
    }
    return nil;
}

- (nullable FlyNesBuiltinGame *)byLicenseFile:(nullable NSString *)licenseFile {
    if (licenseFile.length == 0) return nil;
    for (FlyNesBuiltinGame *game in _games) {
        if (game.licenseFile.length > 0 && [game.licenseFile isEqualToString:licenseFile]) return game;
    }
    return nil;
}

- (BOOL)isBundled:(nullable NSString *)canonicalId {
    return [self byCanonicalId:canonicalId] != nil;
}

@end
