import { BusinessError } from '@kit.BasicServicesKit';
import { router, promptAction } from '@kit.ArkUI';
import { systemDateTime } from '@kit.BasicServicesKit';
import { cryptoFramework } from '@kit.CryptoArchitectureKit';
import { scanBarcode } from '@kit.ScanKit';
import { hilog } from '@kit.PerformanceAnalysisKit';
import nativeSession, { NearbyInviteSnapshotDto } from 'libentry.so';
import { normalizeInviteCode } from '../service/NearbyService';
import { NearbyMvpPort, NearbyMvpScanFlow, NearbyMvpSnapshot } from '../service/NearbyMvpScanFlow';
import {
  NEARBY_MIN_TAP_HEIGHT_VP,
  NEARBY_TYPE,
  nearbyLetterSpacingFp
} from '../ui/NearbyTypography';

// HTML nearby-ui-parity-review.html N01/N02/N03 + design §6 tokens.
const COLOR_BACKGROUND: string = '#121316';
const COLOR_SURFACE: string = '#1B1D22';
const COLOR_FIELD: string = '#292B31';
const COLOR_ON_SURFACE: string = '#F4EFE6';
const COLOR_ON_SURFACE_MUTED: string = '#BEB8AE';
const COLOR_PRIMARY: string = '#FF6B5E';
const COLOR_ON_PRIMARY: string = '#121316';
const COLOR_LINE: string = '#393B40';
const COLOR_AMBER: string = '#E7B775';

class NativeMvpPort implements NearbyMvpPort {
  join(qrText: string): boolean { return nativeSession.nearbyMvpJoin(qrText); }
  snapshot(): NearbyMvpSnapshot { return nativeSession.nearbyMvpSnapshot(); }
  cancel(): boolean { return nativeSession.nearbyMvpCancel(); }
}

@Entry
@Component
struct NearbyPairing {
  // Entry mode (design N01/N02/N03): 'create' | 'join_code' | 'scan'.
  @State private mode: string = '';
  @State private inviteCode: string = '';
  @State private inviteGeneration: number = 0;
  @State private inviteStartedAtMs: number = 0;
  @State private inviteSeconds: number = 0;
  @State private joinInput: string = '';
  @State private joinError: boolean = false;
  @State private joinSubmitted: boolean = false;
  @State private joinFailed: boolean = false;
  @State private availableWidth: number = 0;
  private joinAttemptId: number = 0;
  private inviteTimerId: number = -1;
  private mvpTimerId: number = -1;
  @State private mvpState: number = 0;
  @State private mvpReason: number = 0;
  @State private mvpScanActive: boolean = false;
  private loggedMvpState: number = -1;
  private mvpLobbyOpened: boolean = false;
  private readonly mvpFlow: NearbyMvpScanFlow = new NearbyMvpScanFlow(new NativeMvpPort());

  onPageShow(): void { hilog.info(0, 'FlyNesNearby', 'event=page_show'); }

  onPageHide(): void { hilog.info(0, 'FlyNesNearby', 'event=page_hide'); }

  aboutToAppear(): void {
    hilog.info(0, 'FlyNesNearby', 'event=page_appear');
    const params = router.getParams() as Record<string, string> | undefined;
    this.mode = params && params['mode'] ? params['mode'] : '';
    if (this.mode === 'create') {
      const snapshot: NearbyInviteSnapshotDto = nativeSession.nearbyInviteSnapshot();
      if (snapshot.hostPhase === 1 && snapshot.hostGeneration > 0) {
        nativeSession.nearbyInviteHostCancel(snapshot.hostGeneration);
      }
      this.publishInvite(false);
      this.startInviteTimer();
    } else if (this.mode === 'scan') {
      this.mvpFlow.appear();
      this.startMvpTimer();
    }
  }

  aboutToDisappear(): void {
    hilog.info(0, 'FlyNesNearby', 'event=page_disappear scanning=%{public}s', String(this.mvpScanActive));
    this.stopInviteTimer();
    this.stopMvpTimer();
    // Scan Kit temporarily hides this page. Keep the guest owner alive across
    // that UI extension; explicit cancel/back calls leave() below.
    if (this.mode === 'scan') this.mvpFlow.hideForScanner();
    else nativeSession.nearbyMvpCancel();
    if (this.inviteGeneration > 0) {
      nativeSession.nearbyInviteHostCancel(this.inviteGeneration);
    }
    if (this.joinAttemptId > 0) {
      nativeSession.nearbyInviteCancelCode(this.joinAttemptId);
    }
  }

  private startMvpTimer(): void {
    if (this.mvpTimerId !== -1) return;
    this.mvpTimerId = setInterval(() => {
      this.mvpFlow.poll();
      const snapshot = this.mvpFlow.snapshot();
      if (snapshot.state === 0 && this.mvpState === 4) return;
      if (snapshot.state !== this.loggedMvpState) {
        this.loggedMvpState = snapshot.state;
        hilog.info(0x0000, 'FlyNesNearby',
          'LAN state=%{public}d reason=%{public}d transportResult=%{public}d transportOperation=%{public}d',
          snapshot.state, snapshot.reason, snapshot.transportResult, snapshot.transportOperation);
      }
      this.mvpState = snapshot.state;
      this.mvpReason = snapshot.reason;
      if (snapshot.state === 3 && !this.mvpLobbyOpened) {
        this.mvpLobbyOpened = true;
        this.stopMvpTimer();
        router.replaceUrl({ url: 'pages/NearbyLobby' });
      }
    }, 250);
  }

  private stopMvpTimer(): void {
    if (this.mvpTimerId === -1) return;
    clearInterval(this.mvpTimerId);
    this.mvpTimerId = -1;
  }

  private async scanMvpQr(): Promise<void> {
    hilog.info(0, 'FlyNesNearby', 'event=scan_begin');
    const pending = this.mvpFlow.scan(async (): Promise<string> => {
      try {
        const scanned = await scanBarcode.startScanForResult(getContext(this));
        hilog.info(0, 'FlyNesNearby', 'event=scan_return length=%{public}d', scanned.originalValue.length);
        return scanned.originalValue;
      } catch (error) {
        hilog.error(0, 'FlyNesNearby', 'event=scan_error code=%{public}d', (error as BusinessError).code ?? -1);
        throw error as Error;
      }
    });
    this.mvpScanActive = this.mvpFlow.isScanning();
    const joined = await pending;
    hilog.info(0, 'FlyNesNearby', 'event=scan_complete joined=%{public}s', String(joined));
    this.mvpScanActive = this.mvpFlow.isScanning();
    const snapshot = this.mvpFlow.snapshot();
    this.mvpState = snapshot.state;
    this.mvpReason = snapshot.reason;
  }

  private mvpStatusText(): Resource {
    if (this.mvpState === 3) return $r('app.string.nearby_mvp_connected');
    if (this.mvpState === 2) return $r('app.string.nearby_mvp_joining');
    if (this.mvpState === 4 && this.mvpReason === 4) return $r('app.string.nearby_mvp_expired');
    if (this.mvpState === 4) return $r('app.string.nearby_mvp_failed');
    return $r('app.string.nearby_mvp_scan_prompt');
  }

  private remainingInviteSeconds(): number {
    return this.inviteSeconds;
  }

  private regenerateInvite(): void {
    const snapshot: NearbyInviteSnapshotDto = nativeSession.nearbyInviteSnapshot();
    this.publishInvite(snapshot.hostPhase === 1 &&
      snapshot.hostGeneration === this.inviteGeneration);
  }

  private monotonicNowMs(): number {
    return systemDateTime.getUptime(systemDateTime.TimeType.STARTUP);
  }

  private secureInviteCode(): string {
    const random = cryptoFramework.createRandom();
    let digits = '';
    while (digits.length < 6) {
      const bytes: Uint8Array = random.generateRandomSync(8).data;
      for (let index = 0; index < bytes.length && digits.length < 6; index++) {
        // Rejection sampling avoids modulo bias: 250 is divisible by ten.
        if (bytes[index] < 250) {
          digits += String(bytes[index] % 10);
        }
      }
    }
    return digits;
  }

  private publishInvite(regenerate: boolean): void {
    // N01: the shared reducer owns the generation fence and 60-second expiry.
    const nowMs = this.monotonicNowMs();
    const generation = nativeSession.nearbyInviteNextHostGeneration();
    const code = this.secureInviteCode();
    const accepted: boolean = regenerate
      ? nativeSession.nearbyInviteHostRegenerate(generation, code, nowMs)
      : nativeSession.nearbyInviteHostPublish(generation, code, nowMs);
    if (!accepted) {
      this.clearInviteView();
      return;
    }
    this.inviteCode = code;
    this.inviteGeneration = generation;
    this.inviteStartedAtMs = nowMs;
    this.inviteSeconds = 60;
  }

  private cancelInvite(): void {
    if (this.inviteGeneration > 0) {
      nativeSession.nearbyInviteHostCancel(this.inviteGeneration);
    }
    this.clearInviteView();
    router.back();
  }

  private clearInviteView(): void {
    this.inviteCode = '';
    this.inviteGeneration = 0;
    this.inviteStartedAtMs = 0;
    this.inviteSeconds = 0;
  }

  private startInviteTimer(): void {
    if (this.inviteTimerId !== -1) {
      return;
    }
    this.inviteTimerId = setInterval(() => this.tickInvite(), 250);
  }

  private stopInviteTimer(): void {
    if (this.inviteTimerId !== -1) {
      clearInterval(this.inviteTimerId);
      this.inviteTimerId = -1;
    }
  }

  private tickInvite(): void {
    const nowMs = this.monotonicNowMs();
    nativeSession.nearbyInviteTick(nowMs);
    const snapshot: NearbyInviteSnapshotDto = nativeSession.nearbyInviteSnapshot();
    if (snapshot.hostPhase !== 1 || snapshot.hostGeneration !== this.inviteGeneration) {
      this.clearInviteView();
      return;
    }
    this.inviteSeconds = Math.max(0,
      Math.ceil((60000 - (nowMs - this.inviteStartedAtMs)) / 1000));
  }

  private joinInputChanged(value: string): void {
    if (this.joinSubmitted) {
      return;
    }
    this.joinInput = value;
    this.joinError = false;
  }

  private submitJoin(): void {
    if (this.joinSubmitted) {
      return;
    }
    const code = normalizeInviteCode(this.joinInput);
    if (code === '') {
      this.joinError = true;
      return;
    }
    const nowMs = this.monotonicNowMs();
    this.joinAttemptId = nativeSession.nearbyInviteNextJoinAttemptId();
    nativeSession.nearbyInviteSubmitCode(this.joinAttemptId, code, nowMs);
    this.joinSubmitted = true;
    this.joinFailed = false;
    this.joinError = true;
    setTimeout(() => {
      this.joinSubmitted = false;
      this.joinFailed = true;
    }, 0);
  }

  private cancelJoinAttempt(): void {
    if (this.joinAttemptId > 0) {
      nativeSession.nearbyInviteCancelCode(this.joinAttemptId);
      this.joinAttemptId = 0;
    }
    this.joinSubmitted = false;
    this.joinFailed = false;
    router.back();
  }

  private pageTitle(): Resource {
    if (this.mode === 'join_code') {
      return $r('app.string.nearby_screen_joinCode');
    }
    if (this.mode === 'scan') {
      return $r('app.string.nearby_screen_scan');
    }
    return $r('app.string.nearby_screen_invite');
  }

  build() {
    Column() {
      Row({ space: 8 }) {
        Button('‹', { type: ButtonType.Normal })
          .id('nearby_back').width(48).height(48).fontFamily('HarmonyOS Sans').fontSize('26vp')
          .fontColor(COLOR_ON_SURFACE).backgroundColor(Color.Transparent)
          .accessibilityText($r('app.string.nearby_back'))
          .onClick(() => {
            if (this.mode === 'scan') this.mvpFlow.leave();
            router.back();
          })
        Text(this.pageTitle()).fontFamily('HarmonyOS Sans').fontSize(NEARBY_TYPE.pageTitle.size)
          .fontWeight(FontWeight.Medium).fontColor(COLOR_ON_SURFACE).layoutWeight(1)
      }.width('100%').constraintSize({ minHeight: 48 })
      Row({ space: 18 }) {
        Column({ space: 8 }) {
          Text(this.mode === 'scan' ? $r('app.string.nearby_scan_headline')
            : this.mode === 'join_code' ? $r('app.string.nearby_join_headline')
            : $r('app.string.nearby_invite_headline'))
            .id('nearby_invite_headline').fontFamily('HarmonyOS Sans').fontSize(NEARBY_TYPE.paneTitle.size)
            .fontWeight(FontWeight.Medium).fontColor(COLOR_ON_SURFACE).width('100%')
          Text(this.mode === 'scan' ? $r('app.string.nearby_scan_subtitle')
            : this.mode === 'join_code' ? $r('app.string.nearby_join_subtitle')
            : $r('app.string.nearby_invite_subtitle'))
            .fontFamily('HarmonyOS Sans').fontSize(NEARBY_TYPE.muted.size).fontColor(COLOR_ON_SURFACE_MUTED).width('100%')
        }.width('44%').height('100%').justifyContent(FlexAlign.Center)
          .alignItems(HorizontalAlign.Start)
        Column({ space: 12 }) {
          Text(this.mode === 'scan' ? this.mvpStatusText() : $r('app.string.nearby_not_supported'))
            .id('nearby_pairing_status').fontFamily('HarmonyOS Sans').fontSize(NEARBY_TYPE.body.size)
            .fontColor(COLOR_ON_SURFACE).width('100%')
          if (this.mode === 'scan') {
            this.action($r('app.string.nearby_action_switchToJoinCode'),
              'nearby_scan_switch_to_code', false, () => {
                promptAction.showToast({ message: $r('app.string.nearby_not_supported') });
              })
          }
        }.layoutWeight(1).height('100%').justifyContent(FlexAlign.Center)
          .alignItems(HorizontalAlign.Start).padding(16).backgroundColor(COLOR_SURFACE).borderRadius(12)
      }.width('100%').layoutWeight(1).padding({ left: 16, right: 16, top: 8, bottom: 8 })
      Row({ space: 16 }) {
        this.action($r('app.string.nearby_action_cancel'), 'nearby_action_cancel', false,
          () => {
            if (this.mode === 'join_code') this.cancelJoinAttempt();
            else this.footerAction();
          })
        Blank()
        if (this.mode === 'scan') {
          this.action($r('app.string.nearby_mvp_scan_action'), 'nearby_mvp_scan_action', true,
            () => { void this.scanMvpQr(); })
        } else {
          this.action($r('app.string.nearby_action_submitJoinCode'), 'nearby_join_submit', true,
            () => { promptAction.showToast({ message: $r('app.string.nearby_not_supported') }); })
        }
      }.width('100%').padding({ left: 16, right: 16, top: 8, bottom: 8 })
        .border({ width: { top: 1 }, color: COLOR_LINE })
    }.id('nearby_page').width('100%').height('100%').backgroundColor(COLOR_BACKGROUND)
  }

  @Builder
  action(label: Resource, id: string, primary: boolean, action: () => void) {
    Button(label, { type: ButtonType.Normal })
      .id(id).constraintSize({ minWidth: 120, minHeight: 48, maxWidth: id === 'nearby_scan_switch_to_code' ? '100%' : '48%' })
      .padding({ left: 16, right: 16, top: 8, bottom: 8 }).borderRadius(12)
      .labelStyle({ maxLines: 2 }).fontFamily('HarmonyOS Sans').fontSize(NEARBY_TYPE.action.size).fontWeight(primary ? FontWeight.Medium : FontWeight.Normal)
      .fontColor(primary ? COLOR_ON_PRIMARY : COLOR_ON_SURFACE)
      .backgroundColor(primary ? COLOR_PRIMARY : COLOR_SURFACE)
      .border({ width: 1, color: primary ? COLOR_PRIMARY : COLOR_LINE })
      .borderRadius(12)
      .enabled(id !== 'nearby_mvp_scan_action' || !this.mvpScanActive)
      .onClick(action)
  }

  private footerCopy(): Resource {
    if (this.mode === 'join_code') {
      return $r('app.string.nearby_join_footer');
    }
    if (this.mode === 'scan') {
      return $r('app.string.nearby_scan_footer');
    }
    return $r('app.string.nearby_invite_footer');
  }

  private footerActionLabel(): Resource {
    if (this.mode === 'join_code') {
      return $r('app.string.nearby_action_submitJoinCode');
    }
    if (this.mode === 'scan') {
      return $r('app.string.nearby_action_cancel');
    }
    return $r('app.string.nearby_action_cancelInvite');
  }

  private footerAction(): void {
    if (this.mode === 'join_code') {
      this.submitJoin();
      return;
    }
    if (this.mode === 'scan') {
      this.mvpFlow.leave();
      router.back();
      return;
    }
    this.cancelInvite();
  }
}
