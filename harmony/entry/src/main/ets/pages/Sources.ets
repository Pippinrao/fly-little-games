import { common } from '@kit.AbilityKit';
import { fileShare, picker } from '@kit.CoreFileKit';
import { promptAction } from '@kit.ArkUI';
import nativeApp, { GameCenterRow, ScanJobStatusDto, SourceStatusDto } from 'libentry.so';
import { HarmonyRomScan } from '../platform/HarmonyRomScan';
import { HarmonySourceMap } from '../platform/HarmonySourceMap';
import { debugSideloadUrls } from '../platform/DebugSideload';

const SOURCE_SCOPE_USER_DIRECTORY: number = 2;
const SOURCE_SCOPE_MANAGED_LIBRARY: number = 4;
const FRESHNESS_STALE: number = 2;
const SCAN_FULL: number = 1;
const COLOR_PRIMARY: string = '#7EB6FF';

class SourceRow {
  uuidHex: string = '';
  persistableUri: string = '';
  stale: boolean = false;
  lost: boolean = false;
  sourceScope: number = SOURCE_SCOPE_USER_DIRECTORY;
  lastCompleteness: number = 0;
  gameCount: number = 0;
}

@Entry
@Component
struct Sources {
  private readonly sourceMap: HarmonySourceMap = new HarmonySourceMap();
  private readonly romScan: HarmonyRomScan = new HarmonyRomScan();
  @State private rows: SourceRow[] = [];
  @State private status: string = '';
  @State private busy: boolean = false;
  @State private scanActive: boolean = false;
  @State private debugBuild: boolean = false;

  aboutToAppear(): void {
    void this.reload();
  }

  build() {
    Column({ space: 12 }) {
      Row({ space: 8 }) {
        Text($r('app.string.manage_sources'))
          .fontSize(24)
          .fontWeight(FontWeight.Bold)
          .fontColor('#F2F4F7')
          .layoutWeight(1)
          .maxLines(1)
          .textOverflow({ overflow: TextOverflow.Ellipsis })
        Button($r('app.string.sources_add_games'))
          .height(48)
          .enabled(!this.busy)
          .onClick(() => {
            void this.addRomFiles();
          })
        Button($r('app.string.sources_add_folder'))
          .height(48)
          .enabled(!this.busy)
          .onClick(() => {
            void this.addFolder('');
          })
        if (this.scanActive) {
          Button($r('app.string.sources_cancel_scan'))
            .id('sources-cancel-scan')
            .height(48)
            .onClick(() => {
              if (this.romScan.cancelActiveScan()) {
                this.status = 'Cancelling scan…';
              }
            })
        }
      }
      .width('100%')
      .constraintSize({ minHeight: 48 })

      if (this.status.length > 0) {
        Text(this.status)
          .fontSize(16)
          .fontColor('#A8B0BD')
          .width('100%')
      }

      if (this.debugBuild) {
        Row({ space: 8 }) {
          Button($r('app.string.sources_debug_sideload'))
            .id('sources-debug-sideload')
            .height(48)
            .enabled(!this.busy)
            .onClick(() => {
              void this.importDebugSideload();
            })
          Button($r('app.string.sources_debug_cleanup'))
            .id('sources-debug-cleanup')
            .height(48)
            .enabled(!this.busy)
            .onClick(() => {
              void this.cleanupDebugSideload();
            })
        }
        .width('100%')
      }

      if (this.rows.length === 0) {
        Text($r('app.string.sources_empty'))
          .fontSize(18)
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
                  .fontSize(16)
                  .fontColor('#F2F4F7')
                  .width('100%')
                  .maxLines(2)
                  .textOverflow({ overflow: TextOverflow.Ellipsis })
                Row({ space: 4 }) {
                  Text(`${row.gameCount}`)
                    .fontSize(14)
                    .fontColor('#A8B0BD')
                  Text($r('app.string.sources_games'))
                    .fontSize(14)
                    .fontColor('#A8B0BD')
                  Text(`· ${this.sourceState(row)}`)
                    .fontSize(14)
                    .fontColor(row.stale || row.lost ? '#FFB35E' : '#A8B0BD')
                }
                if (row.lost || row.stale) {
                  Text($r('app.string.sources_stale'))
                    .fontSize(14)
                    .fontColor('#FFB35E')
                    .width('100%')
                  Button($r('app.string.sources_reauth'))
                    .height(40)
                    .enabled(!this.busy)
                    .onClick(() => {
                      void this.addFolder(row.uuidHex);
                    })
                }
                if (!row.lost && row.persistableUri.length > 0) {
                  Button($r('app.string.sources_rescan'))
                    .id(`sources-rescan-${row.uuidHex}`)
                    .height(40)
                    .enabled(!this.busy)
                    .onClick(() => {
                      void this.rescanRow(row);
                    })
                }
                Button($r('app.string.remove_source'))
                  .height(40)
                  .enabled(!this.busy)
                  .onClick(() => {
                    this.confirmRemove(row.uuidHex);
                  })
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
    this.debugBuild = context.applicationInfo.debug;
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
    let catalog: GameCenterRow[] = [];
    try {
      catalog = nativeApp.catalogSnapshot();
    } catch (error) {
      catalog = [];
    }
    for (let index = 0; index < mapped.length; index++) {
      const uuidHex = mapped[index];
      const row = new SourceRow();
      row.uuidHex = uuidHex;
      row.persistableUri = this.sourceMap.get(uuidHex);
      const status: SourceStatusDto | undefined = this.statusFor(statuses, uuidHex);
      row.stale = status !== undefined && status.freshness === FRESHNESS_STALE;
      row.lost = false;
      row.sourceScope = status !== undefined ? status.sourceScope : SOURCE_SCOPE_USER_DIRECTORY;
      row.lastCompleteness = status !== undefined ? status.lastCompleteness : 0;
      row.gameCount = this.countGames(catalog, uuidHex);
      rows.push(row);
    }
    for (let index = 0; index < statuses.length; index++) {
      const status = statuses[index];
      if (status.sourceScope !== SOURCE_SCOPE_USER_DIRECTORY &&
        status.sourceScope !== SOURCE_SCOPE_MANAGED_LIBRARY) {
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
      row.sourceScope = status.sourceScope;
      row.lastCompleteness = status.lastCompleteness;
      row.gameCount = this.countGames(catalog, status.uuidHex);
      rows.push(row);
    }
    return rows;
  }

  private countGames(catalog: GameCenterRow[], uuidHex: string): number {
    let count = 0;
    for (let index = 0; index < catalog.length; index++) {
      if (!catalog[index].builtin && catalog[index].sourceUuidHex === uuidHex) {
        count += 1;
      }

    }
    return count;
  }

  private sourceState(row: SourceRow): string {
    if (row.lost) return 'authorization lost';
    if (row.stale) return 'stale';
    return row.lastCompleteness === SCAN_FULL ? 'ready' : 'partial';
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

  private confirmRemove(uuidHex: string): void {
    if (this.busy) {
      return;
    }
    promptAction.showDialog({
      title: $r('app.string.remove_source_title'),
      message: $r('app.string.remove_source_message'),
      buttons: [
        { text: $r('app.string.cancel'), color: '#A8B0BD' },
        { text: $r('app.string.remove'), color: COLOR_PRIMARY }
      ]
    }).then((result) => {
      if (result.index === 1) {
        void this.removeSource(uuidHex);
      }
    }).catch(() => {});
  }

  private async removeSource(uuidHex: string): Promise<void> {
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
      nativeApp.sourceRemove(uuidHex);
      // HarmonyOS has no SAF-style persistable URI revoke API; catalog + map cleanup is enough.
      this.sourceMap.remove(uuidHex);
      await this.sourceMap.flush();
      this.rows = this.composeRows();
      this.status = '';
    } catch (error) {
      this.status = error instanceof Error ? error.message : 'Could not update that source';
    } finally {
      this.busy = false;
    }
  }

  private async rescanRow(row: SourceRow): Promise<void> {
    if (this.busy || row.persistableUri.length === 0) {
      return;
    }
    this.busy = true;
    try {
      await this.runScan(row.uuidHex, row.persistableUri, row.sourceScope);
      this.rows = this.composeRows();
      this.status = '';
    } catch (error) {
      this.status = error instanceof Error ? error.message : 'rescan failed';
    } finally {
      this.busy = false;
    }
  }

  /** Phone-supported path: pick .nes/.zip files and import into the managed roms library. */
  private async addRomFiles(): Promise<void> {
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
      await this.sourceMap.load(context);
      const options = new picker.DocumentSelectOptions();
      options.maxSelectNumber = 100;
      options.selectMode = picker.DocumentSelectMode.FILE;
      options.fileSuffixFilters = ['NES ROMs|.nes,.zip,.unf,.unif'];
      const documentPicker = new picker.DocumentViewPicker(context);
      const uris = await documentPicker.select(options);
      if (uris.length === 0) {
        this.status = '';
        return;
      }
      const romsDir = `${context.filesDir}/roms`;
      const copied = this.romScan.importDocumentUris(uris, romsDir);
      if (copied === 0) {
        this.status = 'No compatible .nes/.zip files were imported';
        return;
      }
      await this.rescanManagedLibrary(context, romsDir);
      this.rows = this.composeRows();
      this.status = `Imported ${copied} game file(s)`;
    } catch (error) {
      nativeApp.scanAbort();
      this.status = error instanceof Error ? error.message : 'add games failed';
    } finally {
      this.busy = false;
    }
  }

  /**
   * Folder pick requires SystemCapability.FileManagement.UserFileService.FolderSelection
   * (documented as 2-in-1). On phones the picker often shows an empty list — keep as
   * secondary path for devices that support it.
   */
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
        this.status =
          'No folder selected. On phones use “Add games” to pick .nes/.zip files instead.';
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

  private async rescanManagedLibrary(context: common.UIAbilityContext, romsDir: string): Promise<void> {
    let uuidHex = '';
    const mapped = this.sourceMap.uuids();
    for (let index = 0; index < mapped.length; index++) {
      if (this.sourceMap.get(mapped[index]) === romsDir) {
        uuidHex = mapped[index];
        break;
      }
    }
    if (uuidHex.length === 0) {
      uuidHex = this.sourceMap.newUuidHex();
      this.sourceMap.put(uuidHex, romsDir);
      await this.sourceMap.flush();
    }
    await this.runScan(uuidHex, romsDir, SOURCE_SCOPE_MANAGED_LIBRARY);
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
    await this.runScan(uuidHex, uri, SOURCE_SCOPE_USER_DIRECTORY);
  }

  private async importDebugSideload(): Promise<void> {
    if (this.busy || !this.debugBuild) {
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
      await this.sourceMap.load(context);
      const romsDir = `${context.filesDir}/roms`;
      const copied = await this.romScan.fetchHttpSideload(debugSideloadUrls(), romsDir);
      if (copied === 0) {
        this.status = 'Debug sideload endpoint returned no new ROM files';
        return;
      }
      await this.rescanManagedLibrary(context, romsDir);
      this.rows = this.composeRows();
      this.status = `Debug sideload imported ${copied} file(s)`;
    } catch (error) {
      this.status = error instanceof Error ? error.message : 'debug sideload failed';
    } finally {
      this.busy = false;
    }
  }

  private async cleanupDebugSideload(): Promise<void> {
    if (this.busy || !this.debugBuild) {
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
      await this.sourceMap.load(context);
      const romsDir = `${context.filesDir}/roms`;
      const removed = await this.romScan.removeDebugSideloadFixtures(romsDir);
      await this.rescanManagedLibrary(context, romsDir);
      this.rows = this.composeRows();
      this.status = `Removed ${removed} debug fixture file(s)`;
    } catch (error) {
      this.status = error instanceof Error ? error.message : 'debug sideload cleanup failed';
    } finally {
      this.busy = false;
    }
  }

  private async runScan(uuidHex: string, uri: string, sourceScope: number): Promise<void> {
    this.scanActive = true;
    try {
      await this.romScan.scanDirectory(uuidHex, uri, sourceScope, (progress: ScanJobStatusDto) => {
        this.status = `Scanning ${progress.processedFiles} file(s)…`;
      });
    } finally {
      this.scanActive = false;
    }
  }
}
