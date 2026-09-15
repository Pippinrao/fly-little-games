#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/** One bundled homebrew game, as declared by content/assets/builtin-games.json. */
@interface FlyNesBuiltinGame : NSObject

@property (nonatomic, readonly, copy) NSString *canonicalId;
/** Bare ROM filename inside the bundle, e.g. "game.nes". */
@property (nonatomic, readonly, copy) NSString *assetFilename;
@property (nonatomic, readonly, copy) NSString *titleEn;
@property (nonatomic, readonly, copy) NSString *titleZhHans;
@property (nonatomic, readonly, copy) NSString *credit;
@property (nonatomic, readonly, copy) NSString *licenseFile;
@property (nonatomic, readonly, copy) NSString *licenseSpdx;
@property (nonatomic, readonly, copy) NSString *licenseSourceUrl;
@property (nonatomic, readonly) NSInteger mapper;
@property (nonatomic, readonly) NSInteger multiplayerProfileVersion;
@property (nonatomic, readonly, copy) NSString *multiplayerEligibility;
@property (nonatomic, readonly) NSInteger multiplayerMaxPlayers;

@end

/**
 * The bundled homebrew games, read from the one shared manifest.
 *
 * Nothing in the iOS source may name a bundled game: adding one is a manifest
 * edit plus an asset, and every lookup here picks it up.
 */
@interface FlyNesBuiltinGames : NSObject

@property (nonatomic, readonly) NSInteger multiplayerProfileVersion;

/** Asset name of the manifest, at the root of the app bundle's resources. */
@property (class, nonatomic, readonly) NSString *assetName;
/** Resource name of a bundled ROM, without its extension. */
+ (NSString *)resourceNameForAssetFilename:(NSString *)assetFilename;

/** The manifest shipped in this app bundle; empty when it cannot be read. */
+ (instancetype)shared;
/** Replaces the shared instance, for tests. */
+ (void)installForTesting:(FlyNesBuiltinGames *)games;
/** Parses manifest bytes. Throws when the manifest is unusable. */
+ (instancetype)parse:(NSData *)json error:(NSError **)error;
/** No bundled games: the fallback when the manifest cannot be read. */
+ (instancetype)empty;

- (NSArray<FlyNesBuiltinGame *> *)all;
- (nullable FlyNesBuiltinGame *)byCanonicalId:(nullable NSString *)canonicalId;
- (nullable FlyNesBuiltinGame *)byAssetFilename:(nullable NSString *)assetFilename;
- (nullable FlyNesBuiltinGame *)byLicenseFile:(nullable NSString *)licenseFile;
- (BOOL)isBundled:(nullable NSString *)canonicalId;

@end

NS_ASSUME_NONNULL_END
