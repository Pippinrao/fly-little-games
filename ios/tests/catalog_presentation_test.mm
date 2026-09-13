#import <Foundation/Foundation.h>
#import "CatalogPresentation.h"
#include <cassert>
#include <cstdio>
int main() { @autoreleasepool {
    auto indexed=[FlyNesCatalogPresentation fieldsForFilename:@"renamed.nes" entryPath:@"" trustedBuiltin:NO
        indexedTitleEn:@"Contra" indexedTitleZhHans:@"魂斗罗" aliases:@"Gryzor\n魂斗羅"];
    assert([indexed[@"titleEn"] isEqual:@"Contra"] && [indexed[@"titleZhHans"] isEqual:@"魂斗罗"]);
    assert([FlyNesCatalogPresentation fields:indexed matchQuery:@"Gryzor"]);
    assert([FlyNesCatalogPresentation fields:indexed matchQuery:@"renamed.nes"]);
    assert([[FlyNesCatalogPresentation titleForFields:indexed locale:@"zh-Hans"][@"primary"] isEqual:@"魂斗罗"]);
    assert([[FlyNesCatalogPresentation titleForFields:indexed locale:@"fr"][@"primary"] isEqual:@"Contra"]);
    auto trusted=[FlyNesCatalogPresentation fieldsForFilename:@"renamed.nes" entryPath:@"" trustedBuiltin:YES
        indexedTitleEn:@"Contra" indexedTitleZhHans:@"魂斗罗" aliases:@""];
    assert([trusted[@"titleEn"] isEqual:@"From Below"]);
    auto builtin=[FlyNesCatalogPresentation fieldsForFilename:@"from_below.nes" entryPath:@"" trustedBuiltin:YES];
    assert([builtin[@"titleEn"] isEqual:@"From Below"]);
    assert([builtin[@"titleZhHans"] isEqual:@"来自下方"]);
    auto english=[FlyNesCatalogPresentation titleForFields:builtin locale:@"en-US"];
    auto chinese=[FlyNesCatalogPresentation titleForFields:builtin locale:@"zh-CN"];
    assert([english[@"primary"] isEqual:@"From Below"] && [english[@"secondary"] isEqual:@"来自下方"]);
    assert([chinese[@"primary"] isEqual:@"来自下方"] && [chinese[@"secondary"] isEqual:@"From Below"]);
    auto external=[FlyNesCatalogPresentation fieldsForFilename:@"from_below.nes" entryPath:@"" trustedBuiltin:NO];
    assert([external[@"titleEn"] isEqual:@"from_below"] && [external[@"titleZhHans"] length]==0);
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
    assert([merged[@"titleEn"] isEqual:@"From Below"]);
    assert([FlyNesCatalogPresentation fields:merged matchQuery:@"from_below.nes"]);
    puts("PASS: trusted bilingual titles, filename classification, ZIP candidates, locale presentation, aliases and trusted merge");
} }
