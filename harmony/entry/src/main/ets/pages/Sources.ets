import { common } from '@kit.AbilityKit';
import { fileIo, fileShare, picker } from '@kit.CoreFileKit';
import nativeApp, { SourceStatusDto } from 'libentry.so';
import { HarmonySourceMap } from '../platform/HarmonySourceMap';

class ListFolderOptions {
  recursion: boolean = true;
  listNum: number = 0;
}

const SOURCE_SCOPE_USER_DIRECTORY: number = 2;
const SCAN_FULL: number = 1;
const SCAN_PARTIAL: number = 2;
const FRESHNESS_STALE: number = 2;

class SourceRow {
  uuidHex: string = '';
  persistableUri: string = '';
  stale: boolean = false;
  lost: boolean = false;
}

@Entry
@Component
struct Sources {
  private readonly sourceMap: HarmonySourceMap = new HarmonySourceMap();
  @State private rows: SourceRow[] = [];
  @State private status: string = '';
  @State private busy: boolean = false;

  aboutToAppear(): void {
    void this.reload();
  }

  build() {
    Column({ space: 12 }) {
      Row() {
        Text($r('app.string.manage_sources'))
          .fontSize(22)
          .fontWeight(FontWeight.Bold)
          .fontColor('#F2F4F7')
          .layoutWeight(1)
        Button($r('app.string.sources_add_folder'))
          .height(40)
          .enabled(!this.busy)
          .onClick(() => {
            void this.addFolder('');
          })
      }
      .width('100%')

      if (this.status.length > 0) {
        Text(this.status)
          .fontSize(14)
          .fontColor('#A8B0BD')
          .width('100%')
      }

      if (this.rows.length === 0) {
        Text($r('app.string.sources_empty'))
          .fontSize(16)
          .fontColor('#A8B0BD')
          .width('100%')
          .layoutWeight(1)
          .textAlign(TextAlign.Center)
      } else {
        List() {
          ForEach(this.rows, (row: SourceRow) => {
            ListItem() {
              Column({ space: 6 }) {
                Text(this.displayUri(row.persistableUri))
                  .fontSize(15)
                  .fontColor('#F2F4F7')
                  .width('100%')
                  .maxLines(2)
                  .textOverflow({ overflow: TextOverflow.Ellipsis })
                if (row.lost || row.stale) {
                  Text($r('app.string.sources_stale'))
                    .fontSize(13)
                    .fontColor('#FFB35E')
                    .width('100%')
                  Button($r('app.string.sources_reauth'))
                    .height(40)
                    .onClick(() => {
                      void this.addFolder(row.uuidHex);
                    })
                }
              }
              .width('100%')
              .padding(12)
              .backgroundColor('#1B1F28')
              .borderRadius(8)
            }
          }, (row: SourceRow) => row.uuidHex)
        }
        .width('100%')
        .layoutWeight(1)
      }
    }
    .width('100%')
    .height('100%')
    .padding(16)
    .backgroundColor('#12141A')
  }

  private async reload(): Promise<void> {
    const host = this.getUIContext().getHostContext();
    if (host === undefined) {
      this.status = 'UIAbility context is unavailable';
      return;
    }
    const context = host as common.UIAbilityContext;
    try {
      nativeApp.appOpen(context.filesDir, context.cacheDir);
      await this.sourceMap.load(context);
      this.rows = this.composeRows();
      this.status = '';
    } catch (error) {
      this.status = error instanceof Error ? error.message : 'source list failed';
    }
  }

  private composeRows(): SourceRow[] {
    const rows: SourceRow[] = [];
    const mapped = this.sourceMap.uuids();
    let statuses: SourceStatusDto[] = [];
    try {
      statuses = nativeApp.sourceStatusList();
    } catch (error) {
      statuses = [];
    }
    for (let index = 0; index < mapped.length; index++) {
      const uuidHex = mapped[index];
      const row = new SourceRow();
      row.uuidHex = uuidHex;
      row.persistableUri = this.sourceMap.get(uuidHex);
      const status = this.statusFor(statuses, uuidHex);
      row.stale = status !== undefined && status.freshness === FRESHNESS_STALE;
      row.lost = false;
      rows.push(row);
    }
    for (let index = 0; index < statuses.length; index++) {
      const status = statuses[index];
      if (status.sourceScope !== SOURCE_SCOPE_USER_DIRECTORY) {
        continue;
      }
      if (this.sourceMap.get(status.uuidHex).length > 0) {
        continue;
      }
      const row = new SourceRow();
      row.uuidHex = status.uuidHex;
      row.persistableUri = '';
      row.stale = true;
      row.lost = true;
      rows.push(row);
    }
    return rows;
  }

  private statusFor(statuses: SourceStatusDto[], uuidHex: string): SourceStatusDto | undefined {
    for (let index = 0; index < statuses.length; index++) {
      if (statuses[index].uuidHex === uuidHex) {
        return statuses[index];
      }
    }
    return undefined;
  }

  private displayUri(uri: string): string {
    if (uri.length === 0) {
      return '—';
    }
    const parts = uri.split('/');
    return parts.length > 0 ? parts[parts.length - 1] : uri;
  }

  private async addFolder(existingUuidHex: string): Promise<void> {
    if (this.busy) {
      return;
    }
    const host = this.getUIContext().getHostContext();
    if (host === undefined) {
      this.status = 'UIAbility context is unavailable';
      return;
    }
    const context = host as common.UIAbilityContext;
    this.busy = true;
    try {
      nativeApp.appOpen(context.filesDir, context.cacheDir);
      const options = new picker.DocumentSelectOptions();
      options.maxSelectNumber = 1;
      options.selectMode = picker.DocumentSelectMode.FOLDER;
      const documentPicker = new picker.DocumentViewPicker(context);
      const uris = await documentPicker.select(options);
      if (uris.length === 0) {
        this.status = '';
        return;
      }
      const persistableUri = uris[0];
      await this.persistUriPermission(persistableUri);
      const uuidHex = existingUuidHex.length === 32 ? existingUuidHex : this.sourceMap.newUuidHex();
      this.sourceMap.put(uuidHex, persistableUri);
      await this.sourceMap.flush();
      await this.scanUri(uuidHex, persistableUri);
      this.rows = this.composeRows();
      this.status = '';
    } catch (error) {
      nativeApp.scanAbort();
      this.status = error instanceof Error ? error.message : 'add folder failed';
    } finally {
      this.busy = false;
    }
  }

  private async persistUriPermission(uri: string): Promise<void> {
    try {
      const policy: fileShare.PolicyInfo = {
        uri: uri,
        operationMode: fileShare.OperationMode.READ_MODE
      };
      await fileShare.persistPermission([policy]);
    } catch (error) {
      const detail = error instanceof Error ? error.message : 'persist permission failed';
      console.error(`FlyNES persist URI permission failed: ${detail}`);
    }
    try {
      const policy: fileShare.PolicyInfo = {
        uri: uri,
        operationMode: fileShare.OperationMode.READ_MODE
      };
      await fileShare.activatePermission([policy]);
    } catch (error) {
      const detail = error instanceof Error ? error.message : 'activate permission failed';
      console.error(`FlyNES activate URI permission failed: ${detail}`);
    }
  }

  private async scanUri(uuidHex: string, uri: string): Promise<void> {
    let names: string[] = [];
    try {
      const options = new ListFolderOptions();
      names = fileIo.listFileSync(uri, options);
    } catch (error) {
      const detail = error instanceof Error ? error.message : 'list folder failed';
      throw new Error(detail);
    }
    nativeApp.scanBegin(uuidHex, SOURCE_SCOPE_USER_DIRECTORY);
    let completeness = SCAN_FULL;
    try {
      for (let index = 0; index < names.length; index++) {
        const relative = names[index].replace(/\\/g, '/');
        if (!this.isCatalogCandidate(relative)) {
          continue;
        }
        const childUri = `${uri}/${relative}`;
        let file: fileIo.File | undefined = undefined;
        try {
          file = fileIo.openSync(childUri, fileIo.OpenMode.READ_ONLY);
          const stat = fileIo.statSync(file.fd);
          nativeApp.scanAddFile(relative, this.fileName(relative), file.fd, stat.size);
        } catch (error) {
          completeness = SCAN_PARTIAL;
          const detail = error instanceof Error ? error.message : 'scan file failed';
          console.error(`FlyNES scan file failed: ${detail}`);
        } finally {
          if (file !== undefined) {
            fileIo.closeSync(file.fd);
          }
        }
      }
      nativeApp.scanCommit(completeness);
    } catch (error) {
      nativeApp.scanAbort();
      const detail = error instanceof Error ? error.message : 'scan commit failed';
      throw new Error(detail);
    }
  }

  private isCatalogCandidate(relative: string): boolean {
    const lower = relative.toLowerCase();
    return lower.endsWith('.nes') || lower.endsWith('.zip') || lower.endsWith('.unf') ||
      lower.endsWith('.unif');
  }

  private fileName(relative: string): string {
    const parts = relative.split('/');
    return parts.length > 0 ? parts[parts.length - 1] : relative;
  }
}
