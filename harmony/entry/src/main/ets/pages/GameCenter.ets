import { common } from '@kit.AbilityKit';
import { router } from '@kit.ArkUI';
import nativeProduct, { GameCenterRow } from 'libentry.so';
import { CatalogProductService } from '../service/CatalogProductService';

@Entry
@Component
struct GameCenter {
  private readonly catalog: CatalogProductService = new CatalogProductService();
  @State private category: string = 'ALL';
  @State private query: string = '';
  @State private selectedCanonicalId: string = '';
  @State private compactHeader: boolean = false;
  @State private fontScale: number = 1.0;
  @State private allRows: GameCenterRow[] = [];
  @State private visibleRows: GameCenterRow[] = [];

  aboutToAppear(): void {
    this.allRows = this.catalog.loadRows();
    this.readCompactPolicy();
    this.applyFilter();
  }

  onPageShow(): void {
    this.readCompactPolicy();
  }

  build() {
    Column({ space: 12 }) {
      if (this.compactHeader) {
        this.compactToolbar()
      } else {
        this.regularToolbar()
      }

      Search({ value: this.query, placeholder: $r('app.string.game_center_search') })
        .width('100%')
        .height(40)
        .onChange((value: string) => {
          this.query = value;
          this.applyFilter();
        })

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
        .fontSize(20)
        .fontWeight(FontWeight.Bold)
        .fontColor('#F2F4F7')
        .margin({ right: 8 })
      this.categoryToggles()
      this.navButtons()
    }
    .width('100%')
    .height(48)
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
          .fontSize(this.compactHeader ? 16 : 14)
          .fontColor('#F2F4F7')
      }
      .layoutWeight(1)
      .height(this.compactHeader ? 64 : 40)
      .selectedColor('#3A404C')
      .onChange((isOn: boolean) => {
        if (isOn) {
          this.setCategory('RECENT');
        }
      })
      Toggle({ type: ToggleType.Button, isOn: this.category === 'FAVORITES' }) {
        Text($r('app.string.game_center_favorites'))
          .fontSize(this.compactHeader ? 16 : 14)
          .fontColor('#F2F4F7')
      }
      .layoutWeight(1)
      .height(this.compactHeader ? 64 : 40)
      .selectedColor('#3A404C')
      .onChange((isOn: boolean) => {
        if (isOn) {
          this.setCategory('FAVORITES');
        }
      })
      Toggle({ type: ToggleType.Button, isOn: this.category === 'ALL' }) {
        Text($r('app.string.game_center_all'))
          .fontSize(this.compactHeader ? 16 : 14)
          .fontColor('#F2F4F7')
      }
      .layoutWeight(1)
      .height(this.compactHeader ? 64 : 40)
      .selectedColor('#3A404C')
      .onChange((isOn: boolean) => {
        if (isOn) {
          this.setCategory('ALL');
        }
      })
      Toggle({ type: ToggleType.Button, isOn: this.category === 'BUILTIN' }) {
        Text($r('app.string.game_center_builtin'))
          .fontSize(this.compactHeader ? 16 : 14)
          .fontColor('#F2F4F7')
      }
      .layoutWeight(1)
      .height(this.compactHeader ? 64 : 40)
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
    Button($r('app.string.game_center_sources'))
      .height(this.compactHeader ? 64 : 40)
      .fontSize(14)
      .onClick(() => {
        void router.pushUrl({ url: 'pages/Sources' });
      })
    Button($r('app.string.game_center_settings'))
      .height(this.compactHeader ? 64 : 40)
      .fontSize(14)
      .onClick(() => {
        void router.pushUrl({ url: 'pages/Settings' });
      })
  }

  @Builder
  detailPane() {
    Column({ space: 8 }) {
      Text(this.selectedTitle())
        .fontSize(22)
        .fontWeight(FontWeight.Bold)
        .fontColor('#F2F4F7')
        .maxLines(this.catalog.detailTitleMaxLines(this.fontScale))
        .textOverflow({ overflow: TextOverflow.Ellipsis })
        .width('100%')
      Text(this.selectedSubtitle())
        .fontSize(14)
        .fontColor('#A8B0BD')
        .maxLines(1)
        .textOverflow({ overflow: TextOverflow.Ellipsis })
        .width('100%')
      Blank()
      Button($r('app.string.game_center_launch'))
        .width('100%')
        .height(this.catalog.launchButtonHeightVp(this.fontScale))
        .fontSize(16)
        .enabled(this.selectedCanonicalId.length > 0)
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
      Text(this.compactHeader ? $r('app.string.game_center_no_external_sources_compact')
        : $r('app.string.game_center_no_external_sources'))
        .fontSize(14)
        .fontColor('#A8B0BD')
        .width('100%')
      if (this.visibleRows.length === 0) {
        Text(this.emptyMessage())
          .fontSize(16)
          .fontColor('#A8B0BD')
          .width('100%')
          .layoutWeight(1)
          .textAlign(TextAlign.Center)
      } else {
        List() {
          ForEach(this.visibleRows, (row: GameCenterRow) => {
            ListItem() {
              Row({ space: 12 }) {
                Text(this.displayTitle(row))
                  .fontSize(16)
                  .fontColor('#F2F4F7')
                  .layoutWeight(1)
                if (row.builtin) {
                  Text($r('app.string.game_center_builtin'))
                    .fontSize(12)
                    .fontColor('#A8B0BD')
                }
              }
              .width('100%')
              .padding(12)
              .backgroundColor(row.canonicalId === this.selectedCanonicalId ? '#2A303C' : '#1B1F28')
              .borderRadius(8)
            }
            .onClick(() => {
              this.selectedCanonicalId = row.canonicalId;
            })
          }, (row: GameCenterRow) => row.canonicalId)
        }
        .width('100%')
        .layoutWeight(1)
        .scrollBar(BarState.Off)
      }
    }
    .layoutWeight(1)
    .height('100%')
  }

  private readCompactPolicy(): void {
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

  private applyFilter(): void {
    this.visibleRows = nativeProduct.gameCenterFilter(this.allRows, this.category, this.query);
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

  private launchSelected(): void {
    if (this.selectedCanonicalId.length === 0) {
      return;
    }
    void router.pushUrl({
      url: 'pages/RunGame',
      params: { 'canonicalId': this.selectedCanonicalId }
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

  private selectedTitle(): string {
    const row = this.selectedRow();
    if (row === undefined) {
      return '';
    }
    return this.displayTitle(row);
  }

  private selectedSubtitle(): string {
    const row = this.selectedRow();
    if (row === undefined) {
      return '';
    }
    return row.originalFilename;
  }

  private displayTitle(row: GameCenterRow): string {
    if (row.titleZhHans.length > 0 && this.preferZh()) {
      return row.titleZhHans;
    }
    return row.titleEn;
  }

  private preferZh(): boolean {
    const host = this.getUIContext().getHostContext();
    if (host === undefined) {
      return false;
    }
    const language = (host as common.UIAbilityContext).config.language;
    if (language === undefined) {
      return false;
    }
    return language === 'zh' || language.startsWith('zh-') || language.startsWith('zh_');
  }

  private emptyMessage(): Resource {
    if (this.query.length > 0) {
      return $r('app.string.game_center_empty_search');
    }
    return $r('app.string.game_center_empty_category');
  }
}
