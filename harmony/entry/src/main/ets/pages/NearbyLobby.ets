import { common } from '@kit.AbilityKit';
import { router, promptAction } from '@kit.ArkUI';
import nativeSession, { NearbyMvpSnapshotDto } from 'libentry.so';
import { NEARBY_TYPE } from '../ui/NearbyTypography';
import { NearbyLobbyRow, nearbyLobbyDetailRows, nearbyLobbyFirstScreenRows, nearbyString } from '../service/NearbyService';
import { BuiltinGames } from '../service/BuiltinGames';
import { PlayService, RomLaunchLocator } from '../service/PlayService';

@Entry
@Component
struct NearbyLobby {
  @State private detailsOpen: boolean = false;
  @State private detailIndex: number = 0;
  @State private mvpActive: boolean = false;
  @State private mvpState: number = 0;
  @State private mvpReason: number = 0;
  @State private localReady: number = 0;
  @State private peerReady: number = 0;
  @State private gameTitle: string = '';
  @State private gameError: boolean = false;
  private loadingGame: string = '';
  private selectedGame: string = '';
  private timerId: number = -1;
  private playOpened: boolean = false;

  aboutToAppear(): void {
    const snapshot = nativeSession.nearbyMvpSnapshot();
    this.mvpActive = snapshot.state === 3 || snapshot.state === 5 || snapshot.state === 6 || snapshot.state === 7;
    if (!this.mvpActive) return;
    this.updateMvp(snapshot);
    this.timerId = setInterval(() => this.pollMvp(), 50);
  }

  aboutToDisappear(): void {
    if (this.timerId !== -1) clearInterval(this.timerId);
    this.timerId = -1;
  }

  private async selectHostGame(key: string): Promise<void> {
    if (key.length === 0 || this.loadingGame === key || this.selectedGame === key) return;
    this.loadingGame = key;
    this.gameError = false;
    const hostContext = this.getUIContext().getHostContext();
    if (hostContext === undefined) return;
    const context = hostContext as common.UIAbilityContext;
    try {
      const locator = new RomLaunchLocator();
      locator.canonicalId = key;
      const builtin = BuiltinGames.loadSync(context).byCanonicalId(key);
      if (builtin !== undefined) {
        this.gameTitle = builtin.titleZhHans || builtin.titleEn;
      } else {
        const row = nativeSession.catalogSnapshot().find((row) => row.canonicalId === key);
        if (row === undefined) throw new Error('Local game missing');
        locator.sourceUuidHex = row.sourceUuidHex;
        locator.sourceRelativePath = row.sourceRelativePath;
        locator.packageFormat = row.packageFormat;
        locator.zipEntryName = row.originalFilename;
        this.gameTitle = row.titleZhHans || row.titleEn || row.originalFilename;
      }
      const rom = await new PlayService().readRom(context, locator);
      const latest = nativeSession.nearbyMvpSnapshot();
      if (latest.peerGameKey !== key || latest.state !== 5) return;
      if (!nativeSession.nearbyMvpSelectRom(rom)) throw new Error('Local ROM does not match');
      this.selectedGame = key;
    } catch (_) {
      this.gameError = true;
      console.info('FlyNesNearby event=local_game_unavailable');
    } finally {
      if (this.loadingGame === key) this.loadingGame = '';
    }
  }

  private pollMvp(): void {
    const snapshot = nativeSession.nearbyMvpSnapshot();
    this.updateMvp(snapshot);
    if (snapshot.state === 3 || snapshot.state === 7) {
      this.selectedGame = '';
      this.gameTitle = '';
      this.gameError = false;
    }
    if (snapshot.state === 5 && snapshot.localConfigured === 0 && !this.gameError)
      void this.selectHostGame(snapshot.peerGameKey);
    if (snapshot.state === 6 && !this.playOpened) {
      this.playOpened = true;
      if (this.timerId !== -1) clearInterval(this.timerId);
      this.timerId = -1;
      router.replaceUrl({ url: 'pages/RunGame', params: { nearby: true, titleZhHans: this.gameTitle, canonicalId: this.selectedGame } });
    }
  }

  private updateMvp(snapshot: NearbyMvpSnapshotDto): void {
    this.mvpState = snapshot.state;
    this.mvpReason = snapshot.reason;
    this.localReady = snapshot.localReady;
    this.peerReady = snapshot.peerReady;
  }

  private leave(): void {
    if (this.mvpActive) nativeSession.nearbyMvpCancel();
    router.back();
  }

  private confirm(): void {
    if (!this.mvpActive) {
      promptAction.showToast({ message: $r('app.string.nearby_not_supported') });
      return;
    }
    if (this.gameError || this.selectedGame.length === 0) return;
    nativeSession.nearbyMvpConfirm();
    this.updateMvp(nativeSession.nearbyMvpSnapshot());
  }

  build() {
    Column() {
      Row({ space: 8 }) {
        Button('‹', { type: ButtonType.Normal }).width(48).height(48)
          .fontFamily('HarmonyOS Sans').fontSize('26vp').fontColor('#F4EFE6').backgroundColor(Color.Transparent)
          .accessibilityText($r('app.string.nearby_back')).onClick(() => { this.leave(); })
        Text($r('app.string.nearby_lobby_title')).fontFamily('HarmonyOS Sans').fontSize(NEARBY_TYPE.pageTitle.size)
          .fontWeight(FontWeight.Medium).fontColor('#F4EFE6')
      }.width('100%').constraintSize({ minHeight: 48 })
      Row({ space: 16 }) {
        if (this.detailsOpen) {
          ForEach(nearbyLobbyDetailRows().slice(this.detailIndex, this.detailIndex + 1),
            (row: NearbyLobbyRow) => { this.fieldRow(row) }, (row: NearbyLobbyRow) => row.label)
        } else {
          ForEach(nearbyLobbyFirstScreenRows(),
            (row: NearbyLobbyRow) => { this.fieldRow(row) }, (row: NearbyLobbyRow) => row.label)
        }
      }.width('100%').layoutWeight(1).padding(16)
      if (this.mvpActive) {
        Text(`P2 · ${this.mvpState === 6 ? 'RUNNING' :
          this.localReady === 1 ? (this.peerReady === 1 ? 'READY' : 'WAITING FOR P1') : 'CONFIGURED'}`)
          .id('nearby_lobby_mvp_status').fontFamily('HarmonyOS Sans')
          .fontSize(NEARBY_TYPE.muted.size).fontColor(this.mvpReason === 0 ? '#BEB8AE' : '#FF6B5E')
          .width('100%').padding({ left: 16, right: 16 })
      }
      Row({ space: 12 }) {
        if (this.detailsOpen) {
          this.action($r('app.string.nearby_action_cancel'), 'nearby_lobby_details', false,
            () => { this.detailsOpen = false; this.detailIndex = 0; })
        } else {
          this.action($r('app.string.nearby_diagnostics_title'), 'nearby_lobby_details', false,
            () => { this.detailsOpen = true; this.detailIndex = 0; })
        }
        if (this.detailsOpen) {
          this.action($r('app.string.nearby_details_previous'), 'nearby_details_previous', false,
            () => { this.detailIndex = Math.max(0, this.detailIndex - 1); })
          this.action($r('app.string.nearby_details_next'), 'nearby_details_next', false,
            () => { this.detailIndex = Math.min(nearbyLobbyDetailRows().length - 1, this.detailIndex + 1); })
        } else {
          Blank()
        }
        this.action($r('app.string.nearby_lobby_confirm'), 'nearby_lobby_confirm', true,
          () => { this.confirm(); })
      }.width('100%').padding({ left: 16, right: 16, top: 8, bottom: 8 })
        .border({ width: { top: 1 }, color: '#393B40' })
    }.id('nearby_page').width('100%').height('100%').backgroundColor('#121316')
  }

  private rowValue(row: NearbyLobbyRow): ResourceStr {
    if (this.detailsOpen) return nearbyString(row.blocked);
    if (!this.mvpActive) return $r('app.string.nearby_not_supported');
    if (row.label === 'nearby_lobby_rom_identity') {
      if (this.gameError) return $r('app.string.nearby_local_game_missing');
      return this.gameTitle || $r('app.string.nearby_wait_host_game');
    }
    if (row.label === 'nearby_lobby_network_owner') return 'Android · P1';
    return 'HarmonyOS · P2';
  }

  @Builder
  fieldRow(row: NearbyLobbyRow) {
    Column({ space: 8 }) {
      Text(nearbyString(row.label)).fontFamily('HarmonyOS Sans').fontSize(NEARBY_TYPE.sectionTitle.size)
        .fontWeight(FontWeight.Medium).fontColor('#F4EFE6').width('100%')
      Text(this.rowValue(row))
        .fontFamily('HarmonyOS Sans').fontSize(NEARBY_TYPE.muted.size).fontColor('#BEB8AE').width('100%')
      if (this.detailsOpen) {
        Text(`${this.detailIndex + 1} / ${nearbyLobbyDetailRows().length}`)
          .fontFamily('HarmonyOS Sans').fontSize(NEARBY_TYPE.muted.size).fontColor('#BEB8AE')
      }
    }.id(row.label).layoutWeight(1).height('100%').padding(16)
      .justifyContent(FlexAlign.Center).alignItems(HorizontalAlign.Start)
      .backgroundColor('#1B1D22').borderRadius(12)
  }

  @Builder
  action(label: Resource, id: string, primary: boolean, action: () => void) {
    Button(label, { type: ButtonType.Normal }).id(id).layoutWeight(1)
      .constraintSize({ minHeight: 48 }).padding(8).borderRadius(12)
      .labelStyle({ maxLines: 2 }).fontFamily('HarmonyOS Sans').fontSize(NEARBY_TYPE.action.size).fontColor(primary ? '#121316' : '#F4EFE6')
      .backgroundColor(primary ? '#FF6B5E' : '#1B1D22')
      .border({ width: 1, color: primary ? '#FF6B5E' : '#393B40' })
      .borderRadius(12)
      .onClick(action)
  }
}
