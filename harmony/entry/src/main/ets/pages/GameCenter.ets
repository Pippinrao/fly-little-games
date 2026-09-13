import { common } from '@kit.AbilityKit';
import { preferences } from '@kit.ArkData';
import { router } from '@kit.ArkUI';
import { image } from '@kit.ImageKit';
import { i18n } from '@kit.LocalizationKit';
import nativeProduct, { GameCenterRow } from 'libentry.so';
import { applyMultiplayerFilter, CatalogProductService, MultiplayerCapabilityRegistry, localizedGameTitle } from '../service/CatalogProductService';
import { takeRomOpenError, builtinGamesSync } from '../service/PlayService';
import { BuiltinGames } from '../service/BuiltinGames';
import { CoverStore } from '../service/CoverStore';

@Entry
@Component
struct GameCenter {
  private readonly catalog: CatalogProductService = new CatalogProductService();
  private coverStore: CoverStore | undefined = undefined;
  @State private category: string = 'ALL';
  @State private query: string = '';
  @State private localeTag: string = 'system';
  @State private systemLanguage: string = 'en';
  @State private selectedCanonicalId: string = '';
  @State private compactHeader: boolean = false;
  @State private fontScale: number = 1.0;
  @State private openFailed: boolean = false;
  @State private selectedFavorite: boolean = false;
  @State private detailCover: image.PixelMap | undefined = undefined;
  @State private cardCovers: Map<string, image.PixelMap> = new Map();
  @State private searchOpen: boolean = false;
  private allRows: GameCenterRow[] = [];
  @State private visibleRows: GameCenterRow[] = [];
  // Independent two-player filter (design U04): per-device preference, never
  // changed by category, query, or connection events.
  @State private multiplayerOnly: boolean = false;
  // Capability projection: empty until the shared versioned profile registry
  // is wired, so every game truthfully reads UNKNOWN (design 3.2).
  private readonly multiplayerRegistry: MultiplayerCapabilityRegistry = new MultiplayerCapabilityRegistry(1);

  aboutToAppear(): void {
    this.ensureApp();
    this.ensureCoverStore();
    this.reloadRows();
    this.restoreMultiplayerOnly();
    this.readCompactPolicy();
    this.applyFilter();
    this.refreshDetailExtras();
    // Android parity: search bar starts collapsed; reopen only when a query is live.
    this.searchOpen = this.query.length > 0;
    void this.loadCardCovers();
  }

  onPageShow(): void {
    this.ensureApp();
    this.ensureCoverStore();
    this.reloadRows();
    this.readCompactPolicy();
    this.applyFilter();
    this.refreshDetailExtras();
    void this.loadCardCovers();
    const failed = takeRomOpenError();
    this.openFailed = failed.length > 0;
  }

  private ensureCoverStore(): void {
    if (this.coverStore !== undefined) {
      return;
    }
    const host = this.getUIContext().getHostContext();
    if (host === undefined) {
      return;
    }
    this.coverStore = new CoverStore((host as common.UIAbilityContext).filesDir);
  }

  build() {
    Column({ space: 12 }) {
      if (this.compactHeader) {
        this.compactToolbar()
      } else {
        this.regularToolbar()
      }

      if (this.searchOpen) {
        Row({ space: 8 }) {
          Search({ value: this.query, placeholder: $r('app.string.game_center_search') })
            .layoutWeight(1)
            .height(48)
            .accessibilityText($r('app.string.game_center_search'))
            .onChange((value: string) => {
              this.query = value;
              this.applyFilter();
            })
          Button('✕')
            .width(48)
            .height(48)
            .fontSize(18)
            .type(ButtonType.Circle)
            .backgroundColor('#1B1F28')
            .fontColor('#A8B0BD')
            .accessibilityText($r('app.string.close_search'))
            .onClick(() => {
              this.closeSearch();
            })
        }
        .width('100%')
      }

      Row({ space: 16 }) {
        this.detailPane()
        this.libraryPane()
      }
      .width('100%')
      .layoutWeight(1)
      .alignItems(VerticalAlign.Top)
    }
    .width('100%')
    .height('100%')
    .padding({
      left: 16,
      right: 16,
      top: 16,
      bottom: 16
    })
    .backgroundColor('#12141A')
  }

  @Builder
  regularToolbar() {
    Row({ space: 8 }) {
      Text($r('app.string.game_center_title'))
        .fontSize(22)
        .fontWeight(FontWeight.Bold)
        .fontColor('#F2F4F7')
        .margin({ right: 8 })
      this.categoryToggles()
      this.navButtons()
    }
    .width('100%')
    .height(64)
    .alignItems(VerticalAlign.Center)
  }

  @Builder
  compactToolbar() {
    Row({ space: 8 }) {
      this.categoryToggles()
      this.navButtons()
    }
    .width('100%')
    .height(72)
    .alignItems(VerticalAlign.Center)
  }

  @Builder
  categoryToggles() {
    Row({ space: 8 }) {
      Toggle({ type: ToggleType.Button, isOn: this.category === 'RECENT' }) {
        Text($r('app.string.game_center_recent'))
          .fontSize(16)
          .fontWeight(FontWeight.Medium)
          .fontColor('#F2F4F7')
      }
      .height(this.compactHeader ? 56 : 48)
      .padding({ left: 16, right: 16 })
      .selectedColor('#3A404C')
      .onChange((isOn: boolean) => {
        if (isOn) {
          this.setCategory('RECENT');
        }
      })
      Toggle({ type: ToggleType.Button, isOn: this.category === 'FAVORITES' }) {
        Text($r('app.string.game_center_favorites'))
          .fontSize(16)
          .fontWeight(FontWeight.Medium)
          .fontColor('#F2F4F7')
      }
      .height(this.compactHeader ? 56 : 48)
      .padding({ left: 16, right: 16 })
      .selectedColor('#3A404C')
      .onChange((isOn: boolean) => {
        if (isOn) {
          this.setCategory('FAVORITES');
        }
      })
      Toggle({ type: ToggleType.Button, isOn: this.category === 'ALL' }) {
        Text($r('app.string.game_center_all'))
          .fontSize(16)
          .fontWeight(FontWeight.Medium)
          .fontColor('#F2F4F7')
      }
      .height(this.compactHeader ? 56 : 48)
      .padding({ left: 16, right: 16 })
      .selectedColor('#3A404C')
      .onChange((isOn: boolean) => {
        if (isOn) {
          this.setCategory('ALL');
        }
      })
      Toggle({ type: ToggleType.Button, isOn: this.category === 'BUILTIN' }) {
        Text($r('app.string.game_center_builtin'))
          .fontSize(16)
          .fontWeight(FontWeight.Medium)
          .fontColor('#F2F4F7')
      }
      .height(this.compactHeader ? 56 : 48)
      .padding({ left: 16, right: 16 })
      .selectedColor('#3A404C')
      .onChange((isOn: boolean) => {
        if (isOn) {
          this.setCategory('BUILTIN');
        }
      })
    }
    .layoutWeight(1)
  }

  @Builder
  navButtons() {
    Button('🔍')
      .width(this.compactHeader ? 56 : 48)
      .height(this.compactHeader ? 56 : 48)
      .fontSize(18)
      .type(ButtonType.Circle)
      .backgroundColor('#1B1F28')
      .fontColor('#F2F4F7')
      .accessibilityText($r('app.string.game_center_search'))
      .onClick(() => {
        this.openSearch();
      })
    Button($r('app.string.game_center_sources'))
      .height(this.compactHeader ? 56 : 48)
      .fontSize(16)
      .onClick(() => {
        void router.pushUrl({ url: 'pages/Sources' });
      })
    Button($r('app.string.game_center_settings'))
      .height(this.compactHeader ? 56 : 48)
      .fontSize(16)
      .onClick(() => {
        void router.pushUrl({ url: 'pages/Settings' });
      })
    // 附近联机 entry: real local navigation into the friends/nearby-devices page.
    Button($r('app.string.nearby_title'))
      .height(this.compactHeader ? 56 : 48)
      .fontSize(16)
      .onClick(() => {
        void router.pushUrl({ url: 'pages/NearbyFriends' });
      })
  }

  private openSearch(): void {
    this.searchOpen = true;
  }

  private closeSearch(): void {
    this.searchOpen = false;
    if (this.query.length > 0) {
      this.query = '';
      this.applyFilter();
    }
  }

  @Builder
  detailPane() {
    Column({ space: 8 }) {
      Stack() {
        if (this.detailCover !== undefined) {
          Image(this.detailCover)
            .width('100%')
            .height('100%')
            .objectFit(ImageFit.Contain)
            .alt($r('app.string.play_screen_alt'))
        } else {
          Text(this.selectedTitle().length > 0 ? this.selectedTitle() : $r('app.string.about_app'))
            .fontSize(20)
            .fontWeight(FontWeight.Bold)
            .fontColor('#A8B0BD')
            .maxLines(2)
            .textAlign(TextAlign.Center)
            .padding(16)
        }
      }
      .width('100%')
      .layoutWeight(1)
      .backgroundColor('#12141A')
      .borderRadius(8)
      .clip(true)

      Row({ space: 8 }) {
        Text(this.selectedTitle())
          .fontSize(24)
          .fontWeight(FontWeight.Bold)
          .fontColor('#F2F4F7')
          .maxLines(this.catalog.detailTitleMaxLines(this.fontScale))
          .textOverflow({ overflow: TextOverflow.Ellipsis })
          .layoutWeight(1)
        Button(this.selectedFavorite ? '♥' : '♡')
          .width(this.catalog.launchButtonHeightVp(this.fontScale))
          .height(this.catalog.launchButtonHeightVp(this.fontScale))
          .fontSize(22)
          .type(ButtonType.Circle)
          .backgroundColor(this.selectedFavorite ? '#3A2A38' : '#1B1F28')
          .fontColor(this.selectedFavorite ? '#FF6B9E' : '#A8B0BD')
          .enabled(this.selectedCanonicalId.length > 0)
          .accessibilityText(this.selectedFavorite ? $r('app.string.game_center_remove_favorite') :
            $r('app.string.game_center_add_favorite'))
          .onClick(() => {
            this.toggleFavorite();
          })
      }
      .width('100%')
      .alignItems(VerticalAlign.Center)
      Text(this.selectedSubtitle())
        .fontSize(16)
        .fontColor('#A8B0BD')
        .maxLines(1)
        .textOverflow({ overflow: TextOverflow.Ellipsis })
        .width('100%')
      if (this.openFailed || (this.selectedCanonicalId.length > 0 && !this.canOpenSelected())) {
        Text($r('app.string.game_center_rom_open_failed'))
          .fontSize(16)
          .fontColor('#FF6B5E')
          .width('100%')
      }
      Blank()
      Button(this.selectedIsRecent() ? $r('app.string.game_center_continue') :
        $r('app.string.game_center_launch'))
        .width('100%')
        .height(this.catalog.launchButtonHeightVp(this.fontScale))
        .fontSize(18)
        .fontWeight(FontWeight.Medium)
        .enabled(this.selectedCanonicalId.length > 0 && this.canOpenSelected())
        .accessibilityText(this.launchAccessibilityText())
        .onClick(() => {
          this.launchSelected();
        })
    }
    .width('32%')
    .height('100%')
    .padding(16)
    .backgroundColor('#1B1F28')
    .borderRadius(12)
  }

  @Builder
  libraryPane() {
    Column({ space: 8 }) {
      Row({ space: 8 }) {
        Text(this.libraryStatusText())
          .fontSize(16)
          .fontColor('#A8B0BD')
          .layoutWeight(1)
          .maxLines(2)
        Toggle({ type: ToggleType.Checkbox, isOn: this.multiplayerOnly })
          .selectedColor('#FF6B5E')
          .onChange((isOn: boolean) => {
            this.multiplayerOnly = isOn;
            this.persistMultiplayerOnly(isOn);
            this.applyFilter();
          })
        Text($r('app.string.nearby_filter_multiplayerOnly'))
          .fontSize(14)
          .fontColor('#A8B0BD')
          .onClick(() => {
            this.multiplayerOnly = !this.multiplayerOnly;
            this.persistMultiplayerOnly(this.multiplayerOnly);
            this.applyFilter();
          })
      }
      .width('100%')
      .alignItems(VerticalAlign.Center)
      if (this.multiplayerOnly && this.visibleRows.length === 0) {
        // Zero result keeps the category and offers the contract action (C03).
        Button($r('app.string.nearby_action_disableMultiplayerFilter'))
          .height(48)
          .fontSize(16)
          .backgroundColor('#292B31')
          .fontColor('#FF6B5E')
          .onClick(() => {
            this.multiplayerOnly = false;
            this.persistMultiplayerOnly(false);
            this.applyFilter();
          })
      }
      if (this.visibleRows.length === 0) {
        Text(this.emptyMessage())
          .fontSize(18)
          .fontColor('#A8B0BD')
          .width('100%')
          .layoutWeight(1)
          .textAlign(TextAlign.Center)
      } else {
        // Android parity: horizontal two-row grid (GridLayoutManager(span=2, HORIZONTAL)).
        // Card: fixed width (168 / 232 large text), full lane height, art slot 80vp.
        List() {
          ForEach(this.visibleRows, (row: GameCenterRow) => {
            ListItem() {
              Row({ space: 8 }) {
                Stack() {
                  if (this.cardCovers.get(row.canonicalId) !== undefined) {
                    Image(this.cardCovers.get(row.canonicalId))
                      .width('100%')
                      .height('100%')
                      .objectFit(ImageFit.Contain)
                      .alt($r('app.string.play_screen_alt'))
                  } else {
                    Text(this.displayTitle(row))
                      .fontSize(10)
                      .fontWeight(FontWeight.Bold)
                      .fontColor('#A8B0BD')
                      .maxLines(2)
                      .textAlign(TextAlign.Center)
                      .padding(4)
                  }
                }
                .width(80)
                .height('100%')
                .backgroundColor('#12141A')
                .borderRadius(6)
                .clip(true)
                Column({ space: 4 }) {
                  Text(this.displayTitle(row))
                    .fontSize(15)
                    .fontWeight(FontWeight.Bold)
                    .fontColor('#F2F4F7')
                    .maxLines(2)
                    .textOverflow({ overflow: TextOverflow.Ellipsis })
                    .width('100%')
                  Text(this.cardMeta(row))
                    .fontSize(11)
                    .fontColor('#A8B0BD')
                    .maxLines(1)
                    .textOverflow({ overflow: TextOverflow.Ellipsis })
                    .width('100%')
                    .visibility(this.cardMeta(row).length > 0 ? Visibility.Visible :
                      Visibility.None)
                }
                .alignItems(HorizontalAlign.Start)
                .layoutWeight(1)
              }
              .width(this.fontScale >= 1.8 ? 232 : 168)
              .height('100%')
              .padding(8)
              .margin(4)
              .backgroundColor(row.canonicalId === this.selectedCanonicalId ? '#2A303C' :
                '#1B1F28')
              .borderRadius(8)
              .accessibilityText(this.cardAccessibilityText(row))
            }
            .onClick(() => {
              this.selectedCanonicalId = row.canonicalId;
              this.refreshDetailExtras();
            })
          }, (row: GameCenterRow) => `${row.canonicalId}:${row.favorite ? 1 : 0}:${row.lastPlayedSequence}`)
        }
        .width('100%')
        .layoutWeight(1)
        .listDirection(Axis.Horizontal)
        .lanes(2)
        .scrollBar(BarState.Auto)
      }
    }
    .layoutWeight(1)
    .height('100%')
  }

  private ensureApp(): void {
    const host = this.getUIContext().getHostContext();
    if (host === undefined) {
      return;
    }
    const context = host as common.UIAbilityContext;
    try {
      nativeProduct.appOpen(context.filesDir, context.cacheDir);
    } catch (error) {
      const detail = error instanceof Error ? error.message : 'appOpen failed';
      console.error(`FlyNES appOpen failed: ${detail}`);
    }
  }

  private reloadRows(): void {
    const host = this.getUIContext().getHostContext();
    const games = host === undefined
      ? BuiltinGames.empty()
      : builtinGamesSync(host as common.UIAbilityContext);
    this.allRows = this.catalog.loadRows(games);
  }

  private readCompactPolicy(): void {
    this.localeTag = nativeProduct.settingsGet().localeTag;
    this.systemLanguage = i18n.System.getSystemLanguage();
    const host = this.getUIContext().getHostContext();
    if (host === undefined) {
      this.compactHeader = false;
      this.fontScale = 1.0;
      return;
    }
    const context = host as common.UIAbilityContext;
    const fontScale = context.config.fontSizeScale;
    this.fontScale = fontScale === undefined ? 1.0 : fontScale;
    const language = context.config.language === undefined ? '' : context.config.language;
    this.compactHeader = this.catalog.compactHeader(this.fontScale, language);
  }

  private setCategory(next: string): void {
    if (this.category === next) {
      return;
    }
    this.category = next;
    this.applyFilter();
  }

  private persistMultiplayerOnly(value: boolean): void {
    AppStorage.setOrCreate('GameCenterMultiplayerOnly', value);
    try {
      const context = getContext(this) as common.UIAbilityContext;
      const store = preferences.getPreferencesSync(context, { name: 'game_center_ui' });
      store.putSync('multiplayerOnly', value);
      store.flushSync();
    } catch (error) {
      console.error(`FlyNES multiplayer filter persist failed: ${error}`);
    }
  }

  private restoreMultiplayerOnly(): void {
    try {
      const context = getContext(this) as common.UIAbilityContext;
      const store = preferences.getPreferencesSync(context, { name: 'game_center_ui' });
      this.multiplayerOnly = store.getSync('multiplayerOnly', false) as boolean;
    } catch (error) {
      this.multiplayerOnly = false;
    }
  }

  private applyFilter(): void {
    const rows: GameCenterRow[] = [];
    for (let index = 0; index < this.allRows.length; index++) {
      rows.push(this.allRows[index]);
    }
    this.visibleRows = nativeProduct.gameCenterFilter(rows, this.category, this.query);
    // Stable post-filter (design 3.2): removes non-SUPPORTED rows only, never re-sorts.
    if (this.multiplayerOnly) {
      this.visibleRows = applyMultiplayerFilter(this.visibleRows, this.multiplayerRegistry);
    }
    let stillVisible = false;
    for (let index = 0; index < this.visibleRows.length; index++) {
      if (this.visibleRows[index].canonicalId === this.selectedCanonicalId) {
        stillVisible = true;
        break;
      }
    }
    if (!stillVisible) {
      this.selectedCanonicalId = this.visibleRows.length > 0 ? this.visibleRows[0].canonicalId : '';
    }
  }

  private canOpenSelected(): boolean {
    const row: GameCenterRow | undefined = this.selectedRow();
    if (row === undefined) {
      return false;
    }
    if (row.builtin) {
      return true;
    }
    return row.sourceUuidHex.length === 32 && row.sourceRelativePath.length > 0;
  }

  private launchSelected(): void {
    if (this.selectedCanonicalId.length === 0) {
      return;
    }
    if (!this.canOpenSelected()) {
      this.openFailed = true;
      return;
    }
    const row: GameCenterRow | undefined = this.selectedRow();
    if (row === undefined) {
      return;
    }
    this.openFailed = false;
    void router.pushUrl({
      url: 'pages/RunGame',
      params: {
        'canonicalId': row.canonicalId,
        'sourceUuidHex': row.sourceUuidHex,
        'sourceRelativePath': row.sourceRelativePath,
        'packageFormat': `${row.packageFormat}`,
        'zipEntryName': row.originalFilename,
        'title': this.displayTitle(row),
        'titleEn': row.titleEn,
        'titleZhHans': row.titleZhHans
      }
    });
  }

  private selectedRow(): GameCenterRow | undefined {
    for (let index = 0; index < this.visibleRows.length; index++) {
      if (this.visibleRows[index].canonicalId === this.selectedCanonicalId) {
        return this.visibleRows[index];
      }
    }
    return undefined;
  }

  private libraryStatusText(): Resource {
    for (let index = 0; index < this.allRows.length; index++) {
      if (!this.allRows[index].builtin) {
        return $r('app.string.game_center_external_ready');
      }
    }
    return this.compactHeader ? $r('app.string.game_center_no_external_sources_compact')
      : $r('app.string.game_center_no_external_sources');
  }

  private selectedTitle(): string {
    const row: GameCenterRow | undefined = this.selectedRow();
    if (row === undefined) {
      return '';
    }
    return this.displayTitle(row);
  }

  private selectedSubtitle(): string {
    const row: GameCenterRow | undefined = this.selectedRow();
    if (row === undefined) {
      return '';
    }
    return row.originalFilename;
  }

  private displayTitle(row: GameCenterRow): string {
    return localizedGameTitle(row.titleEn, row.titleZhHans, row.originalFilename,
      this.localeTag, this.systemLanguage);
  }

  private emptyMessage(): Resource {
    if (this.query.length > 0) {
      return $r('app.string.game_center_empty_search');
    }
    return $r('app.string.game_center_empty_category');
  }

  private selectedIsRecent(): boolean {
    const row: GameCenterRow | undefined = this.selectedRow();
    return row !== undefined && row.lastPlayedSequence > 0;
  }

  private cardMeta(row: GameCenterRow): string {
    if (row.favorite) {
      let badge = '♥';
      if (row.builtin) {
        const host = this.getUIContext().getHostContext();
        if (host !== undefined) {
          try {
            badge = `♥ · ${(host as common.UIAbilityContext).resourceManager
              .getStringSync($r('app.string.game_center_builtin').id)}`;
          } catch (error) {
            badge = '♥';
          }
        }
      }
      return badge;
    }
    return row.builtin ? '' : row.originalFilename;
  }

  private refreshDetailExtras(): void {
    const row: GameCenterRow | undefined = this.selectedRow();
    this.selectedFavorite = row !== undefined && row.favorite;
    this.detailCover = undefined;
    const store = this.coverStore;
    const canonicalId = this.selectedCanonicalId;
    if (store === undefined || canonicalId.length === 0) {
      return;
    }
    store.load(canonicalId).then((loaded?: image.PixelMap) => {
      if (loaded !== undefined && canonicalId === this.selectedCanonicalId) {
        this.detailCover = loaded;
      }
    }).catch((error: Object) => {
      const detail = error instanceof Error ? error.message : 'detail cover load failed';
      console.error(`FlyNES detail cover load failed: ${detail}`);
    });
  }

  private async loadCardCovers(): Promise<void> {
    const store = this.coverStore;
    if (store === undefined) {
      return;
    }
    const next = new Map<string, image.PixelMap>();
    for (let index = 0; index < this.visibleRows.length; index++) {
      const row = this.visibleRows[index];
      try {
        const loaded = await store.load(row.canonicalId);
        if (loaded !== undefined) {
          next.set(row.canonicalId, loaded);
        }
      } catch (error) {
        const detail = error instanceof Error ? error.message : 'card cover load failed';
        console.error(`FlyNES card cover load failed: ${detail}`);
      }
    }
    this.cardCovers = next;
  }

  private toggleFavorite(): void {
    if (this.selectedCanonicalId.length === 0) {
      return;
    }
    const next = !this.selectedFavorite;
    try {
      nativeProduct.catalogFavoriteSet(this.selectedCanonicalId, next);
      this.selectedFavorite = next;
      this.reloadRows();
      this.applyFilter();
      // applyFilter() may replace visibleRows; re-read the favorite flag from the
      // freshly loaded row so the heart state always mirrors persisted state.
      const updated: GameCenterRow | undefined = this.selectedRow();
      this.selectedFavorite = updated !== undefined && updated.favorite;
    } catch (error) {
      const detail = error instanceof Error ? error.message : 'favorite toggle failed';
      console.error(`FlyNES favorite toggle failed: ${detail}`);
    }
  }

  private launchAccessibilityText(): string {
    const title = this.selectedTitle();
    const action = this.selectedIsRecent() ? 'Continue game' : 'Start game';
    if (title.length === 0) {
      return action;
    }
    return `${action}, ${title}`;
  }

  private cardAccessibilityText(row: GameCenterRow): string {
    const title = this.displayTitle(row);
    if (row.canonicalId === this.selectedCanonicalId) {
      return `${title}, Ready to play`;
    }
    return title;
  }
}
