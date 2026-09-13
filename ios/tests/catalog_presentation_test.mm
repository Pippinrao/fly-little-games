#import <Foundation/Foundation.h>
#import "CatalogPresentation.h"
#include <cassert>
#include <cstdio>
int main() { @autoreleasepool {
    // A trusted bundled row takes its titles from the shared manifest, so this
    // names a manifest-declared game instead of a hardcoded pair.
    auto builtin=[FlyNesCatalogPresentation fieldsForFilename:@"thwaite.nes" entryPath:@"" trustedBuiltin:YES];
    assert([builtin[@"titleEn"] isEqual:@"Thwaite"]);
    assert([builtin[@"titleZhHans"] isEqual:@"护村记"]);
    // An indexed (non-bundled) row keeps its own metadata, aliases and search.
    auto indexed=[FlyNesCatalogPresentation fieldsForFilename:@"renamed.nes" entryPath:@"" trustedBuiltin:NO
        indexedTitleEn:@"Contra" indexedTitleZhHans:@"魂斗罗" aliases:@"Gryzor\n魂斗羅"];
    assert([indexed[@"titleEn"] isEqual:@"Contra"] && [indexed[@"titleZhHans"] isEqual:@"魂斗罗"]);
    assert([FlyNesCatalogPresentation fields:indexed matchQuery:@"Gryzor"]);
    assert([FlyNesCatalogPresentation fields:indexed matchQuery:@"renamed.nes"]);
    assert([[FlyNesCatalogPresentation titleForFields:indexed locale:@"zh-Hans"][@"primary"] isEqual:@"魂斗罗"]);
    assert([[FlyNesCatalogPresentation titleForFields:indexed locale:@"fr"][@"primary"] isEqual:@"Contra"]);
    auto english=[FlyNesCatalogPresentation titleForFields:builtin locale:@"en-US"];
    auto chinese=[FlyNesCatalogPresentation titleForFields:builtin locale:@"zh-CN"];
    assert([english[@"primary"] isEqual:@"Thwaite"] && [english[@"secondary"] isEqual:@"护村记"]);
    assert([chinese[@"primary"] isEqual:@"护村记"] && [chinese[@"secondary"] isEqual:@"Thwaite"]);
    auto external=[FlyNesCatalogPresentation fieldsForFilename:@"thwaite.nes" entryPath:@"" trustedBuiltin:NO];
    assert([external[@"titleEn"] isEqual:@"thwaite"] && [external[@"titleZhHans"] length]==0);
    auto zip=[FlyNesCatalogPresentation fieldsForFilename:@"Collection.zip" entryPath:@"folder/超级游戏 (USA).nes" trustedBuiltin:NO];
    assert([zip[@"titleEn"] isEqual:@"Collection"] && [zip[@"titleZhHans"] isEqual:@"超级游戏 (USA)"]);
    assert([FlyNesCatalogPresentation fields:zip matchQuery:@"SUPER"]==NO);
    assert([FlyNesCatalogPresentation fields:zip matchQuery:@" collection.zip "]);
    assert([FlyNesCatalogPresentation fields:zip matchQuery:@"超级游戏"]);
    auto kana=[FlyNesCatalogPresentation fieldsForFilename:@"パズル.nes" entryPath:@"" trustedBuiltin:NO];
    assert([kana[@"titleEn"] length]==0 && [kana[@"titleZhHans"] length]==0);
    assert([[FlyNesCatalogPresentation titleForFields:kana locale:@"en"][@"primary"] isEqual:@"パズル"]);
    auto latin=[FlyNesCatalogPresentation fieldsForFilename:@"Élite.nes" entryPath:@"" trustedBuiltin:NO];
    assert([latin[@"titleEn"] isEqual:@"Élite"]);
    assert([FlyNesCatalogPresentation fields:latin matchQuery:@"élite"]);
    auto merged=[FlyNesCatalogPresentation mergeFields:external with:builtin];
    assert([merged[@"titleEn"] isEqual:@"Thwaite"]);
    assert([FlyNesCatalogPresentation fields:merged matchQuery:@"thwaite.nes"]);
    puts("PASS: trusted bilingual titles, filename classification, ZIP candidates, locale presentation, aliases and trusted merge");
} }
