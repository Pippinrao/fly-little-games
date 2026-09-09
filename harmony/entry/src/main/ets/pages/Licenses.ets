import { common } from '@kit.AbilityKit';
import { pasteboard } from '@kit.BasicServicesKit';
import { promptAction, router } from '@kit.ArkUI';
import { util } from '@kit.ArkTS';
import { LicenseEntry, LicenseModel } from './LicenseModel';

const COLOR_BACKGROUND: string = '#121316';
const COLOR_SURFACE: string = '#1B1D22';
const COLOR_ON_SURFACE: string = '#F4EFE6';
const COLOR_ON_SURFACE_MUTED: string = '#BEB8AE';
const COLOR_PRIMARY: string = '#FF6B5E';
const COLOR_ON_PRIMARY: string = '#121316';

@Entry
@Component
struct Licenses {
  @State private entries: LicenseEntry[] = LicenseModel.all();
  @State private selectedFile: string = '';
  @State private body: string = '';
  @State private loadFailed: boolean = false;
  @State private status: string = '';

  aboutToAppear(): void {
    if (this.entries.length > 0) {
      void this.selectEntry(this.entries[0].fileName);
    }
  }

  build() {
    Column() {
      Row() {
        Text('‹')
          .fontSize(26)
          .fontWeight(FontWeight.Bold)
          .fontColor(COLOR_ON_SURFACE)
          .width(44)
          .height(44)
          .textAlign(TextAlign.Center)
          .onClick(() => {
            router.back();
          })
        Text($r('app.string.license_information'))
          .fontSize(20)
          .fontWeight(FontWeight.Bold)
          .fontColor(COLOR_ON_SURFACE)
          .layoutWeight(1)
      }
      .width('100%')
      .height(56)
      .padding({ left: 4, right: 16 })
      .backgroundColor(COLOR_SURFACE)

      Row() {
        List() {
          ForEach(this.entries, (entry: LicenseEntry) => {
            ListItem() {
              Text(entry.displayName)
                .fontSize(16)
                .fontWeight(this.selectedFile === entry.fileName ? FontWeight.Bold : FontWeight.Medium)
                .fontColor(this.selectedFile === entry.fileName ? COLOR_ON_PRIMARY : COLOR_ON_SURFACE)
                .width('100%')
                .height(48)
                .padding({ left: 16, right: 16 })
                .textAlign(TextAlign.Start)
                .backgroundColor(this.selectedFile === entry.fileName ? COLOR_PRIMARY : COLOR_SURFACE)
            }
            .onClick(() => {
              void this.selectEntry(entry.fileName);
            })
          }, (entry: LicenseEntry) => entry.fileName)
        }
        .width(240)
        .height('100%')
        .scrollBar(BarState.Off)
        .backgroundColor(COLOR_SURFACE)

        Column({ space: 12 }) {
          Text(this.selectedHeading())
            .fontSize(22)
            .fontWeight(FontWeight.Bold)
            .fontColor(COLOR_ON_SURFACE)
            .width('100%')
          if (this.loadFailed) {
            Text($r('app.string.license_load_failed'))
              .fontSize(16)
              .fontColor('#FF6B5E')
              .width('100%')
            Button($r('app.string.license_retry'))
              .height(48)
              .onClick(() => {
                void this.selectEntry(this.selectedFile);
              })
          } else {
            Scroll() {
              Text(this.body)
                .fontSize(14)
                .fontColor(COLOR_ON_SURFACE_MUTED)
                .width('100%')
            }
            .layoutWeight(1)
            .width('100%')
          }
          Button($r('app.string.license_copy_link'))
            .width('100%')
            .height(48)
            .enabled(this.selectedHasUrl())
            .accessibilityText(this.selectedHasUrl() ? $r('app.string.license_copy_link') :
              $r('app.string.license_source_unconfigured'))
            .onClick(() => {
              this.copyLink();
            })
          if (this.status.length > 0) {
            Text(this.status)
              .fontSize(14)
              .fontColor(COLOR_ON_SURFACE_MUTED)
              .width('100%')
          }
        }
        .layoutWeight(1)
        .height('100%')
        .padding(16)
        .alignItems(HorizontalAlign.Start)
        .backgroundColor(COLOR_BACKGROUND)
      }
      .layoutWeight(1)
      .width('100%')
    }
    .width('100%')
    .height('100%')
    .backgroundColor(COLOR_BACKGROUND)
  }

  private selectedHeading(): string {
    const entry = LicenseModel.byFileName(this.selectedFile);
    return entry === undefined ? 'Licenses' : entry.displayName;
  }

  private selectedHasUrl(): boolean {
    const entry = LicenseModel.byFileName(this.selectedFile);
    return entry !== undefined && entry.hasSourceUrl();
  }

  private async selectEntry(fileName: string): Promise<void> {
    this.selectedFile = fileName;
    this.status = '';
    const host = this.getUIContext().getHostContext();
    if (host === undefined) {
      this.loadFailed = true;
      this.body = '';
      return;
    }
    const context = host as common.UIAbilityContext;
    try {
      const bytes = await context.resourceManager.getRawFileContent(fileName);
      const text = util.TextDecoder.create('utf-8', { ignoreBOM: true }).decodeWithStream(bytes);
      const entry = LicenseModel.byFileName(fileName);
      let body = LicenseModel.resolveBody(fileName, text);
      if (entry !== undefined && entry.hasSourceUrl()) {
        body = `${body}\n\n${entry.sourceUrl}`;
      }
      this.body = body;
      this.loadFailed = false;
    } catch (error) {
      this.body = '';
      this.loadFailed = true;
      const detail = error instanceof Error ? error.message : 'license load failed';
      console.error(`FlyNES license load failed: ${detail}`);
    }
  }

  private copyLink(): void {
    const entry = LicenseModel.byFileName(this.selectedFile);
    if (entry === undefined || !entry.hasSourceUrl()) {
      this.status = 'Source link not configured';
      return;
    }
    const data = pasteboard.createData(pasteboard.MIMETYPE_TEXT_PLAIN, entry.sourceUrl);
    pasteboard.getSystemPasteboard().setData(data);
    promptAction.showToast({ message: $r('app.string.license_link_copied') });
    this.status = '';
  }
}
