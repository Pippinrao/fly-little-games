import { router } from '@kit.ArkUI';
import { systemDateTime } from '@kit.BasicServicesKit';
import { cryptoFramework } from '@kit.CryptoArchitectureKit';
import nativeSession, { NearbyInviteSnapshotDto } from 'libentry.so';
import {
  NEARBY_STAGE_STATUS_CURRENT,
  NEARBY_STAGE_STATUS_NOT_REACHED,
  NEARBY_STAGE_STATUS_PASSED,
  NEARBY_WIFI_PATH_ORDER,
  NearbyStageRow,
  nearbyPairingCurrentStage,
  nearbyPairingStages,
  nearbyString,
  normalizeInviteCode
} from '../service/NearbyService';

// Android/game-center parity: the same shell colors as pages/NearbyFriends.ets and
// pages/Settings.ets. No new palette is introduced here.
const COLOR_BACKGROUND: string = '#121316';
const COLOR_SURFACE: string = '#1B1D22';
const COLOR_FIELD: string = '#292B31';
const COLOR_ON_SURFACE: string = '#F4EFE6';
const COLOR_ON_SURFACE_MUTED: string = '#BEB8AE';
const COLOR_NOT_REACHED: string = '#292B31';
const COLOR_BLOCKED: string = '#FF6B5E';

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
  private joinAttemptId: number = 0;
  private inviteTimerId: number = -1;

  aboutToAppear(): void {
    const params = router.getParams() as Record<string, string> | undefined;
    this.mode = params && params['mode'] ? params['mode'] : '';
    if (this.mode === 'create') {
      const snapshot: NearbyInviteSnapshotDto = nativeSession.nearbyInviteSnapshot();
      if (snapshot.hostPhase === 1 && snapshot.hostGeneration > 0) {
        nativeSession.nearbyInviteHostCancel(snapshot.hostGeneration);
      }
      this.publishInvite(false);
      this.startInviteTimer();
    }
  }

  aboutToDisappear(): void {
    this.stopInviteTimer();
    if (this.inviteGeneration > 0) {
      nativeSession.nearbyInviteHostCancel(this.inviteGeneration);
    }
    if (this.joinSubmitted && this.joinAttemptId > 0) {
      nativeSession.nearbyInviteCancelCode(this.joinAttemptId);
    }
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
    // C05: no request on incomplete/invalid input; the field error is adjacent
    // to the input and appears only after a submit attempt. With no discovery
    // bearer in this build the honest outcome is the discovery-blocked reason,
    // never a synthetic host approval.
    const code = normalizeInviteCode(this.joinInput);
    if (code === '') {
      this.joinError = true;
      return;
    }
    const nowMs = this.monotonicNowMs();
    this.joinAttemptId = nativeSession.nearbyInviteNextJoinAttemptId();
    nativeSession.nearbyInviteSubmitCode(this.joinAttemptId, code, nowMs);
    this.joinSubmitted = true;
    this.joinError = true;
  }

  build() {
    Column() {
      this.topBar()
      Scroll() {
        Column({ space: 16 }) {
          if (this.mode === 'create') {
            this.createBlock()
          }
          if (this.mode === 'join_code' || this.mode === 'scan') {
            this.joinCodeBlock()
          }
          this.codeBlock()
          this.wifiPathBlock()
          // §4 line 188: the anonymous-join control set (nearby.join.request_anonymous
          // / accept / reject) is kept in the vocabulary but is PENDING §30 centralised
          // review. This block is deliberately not built; do not add it here until that
          // review answers.
        }
        .width('100%')
        .padding(16)
        .alignItems(HorizontalAlign.Start)
      }
      .width('100%')
      .layoutWeight(1)
    }
    .width('100%')
    .height('100%')
    .backgroundColor(COLOR_BACKGROUND)
  }

  @Builder
  topBar() {
    Row() {
      Text('‹')
        .fontSize(26)
        .fontWeight(FontWeight.Bold)
        .fontColor(COLOR_ON_SURFACE)
        .width(48)
        .height(48)
        .textAlign(TextAlign.Center)
        .onClick(() => {
          router.back();
        })
      // Page title is this page's own name, not the feature name nearby.title.
      Text($r('app.string.nearby_pairing_title'))
        .fontSize(20)
        .fontWeight(FontWeight.Bold)
        .fontColor(COLOR_ON_SURFACE)
        .layoutWeight(1)
    }
    .width('100%')
    .height(56)
    .padding({ left: 4, right: 16 })
    .backgroundColor(COLOR_SURFACE)
  }

  // BLE path: one six-digit code, entered on both ends. No code can be generated
  // or verified without the session, so the field and its confirm control are
  // present, disabled, and carry nearby.blocked.auth (§4).
  // N01 创建联机: generation-bound six-digit code with the 60s continuous clock.
  @Builder
  createBlock() {
    Column({ space: 8 }) {
      Text($r('app.string.nearby_invite_codeLabel'))
        .fontSize(18)
        .fontWeight(FontWeight.Bold)
        .fontColor(COLOR_ON_SURFACE)
        .width('100%')
      Text(this.inviteCode === '' ? '· · · · · ·' : this.inviteCode)
        .fontSize(32)
        .fontWeight(FontWeight.Bold)
        .letterSpacing(8)
        .fontColor(COLOR_ON_SURFACE)
        .width('100%')
      if (this.inviteCode !== '') {
        Text(`${$r('app.string.nearby_invite_validFor')}: ${this.remainingInviteSeconds()}s`)
          .fontSize(14)
          .fontColor(COLOR_ON_SURFACE_MUTED)
          .width('100%')
      }
      Row({ space: 8 }) {
        Button($r('app.string.nearby_action_regenerate'))
          .layoutWeight(1)
          .height(48)
          .fontSize(16)
          .backgroundColor(COLOR_FIELD)
          .fontColor(COLOR_ON_SURFACE)
          .onClick(() => this.regenerateInvite())
        Button($r('app.string.nearby_action_cancelInvite'))
          .layoutWeight(1)
          .height(48)
          .fontSize(16)
          .backgroundColor(COLOR_FIELD)
          .fontColor(COLOR_ON_SURFACE)
          .onClick(() => this.cancelInvite())
      }
      .width('100%')
    }
    .width('100%')
    .padding(12)
    .backgroundColor(COLOR_SURFACE)
    .borderRadius(8)
    .alignItems(HorizontalAlign.Start)
  }

  // N02 输入配对码: six digits, leading zeros kept, request never sent on
  // incomplete input, submit locked while a request is in flight (C05/C16).
  @Builder
  joinCodeBlock() {
    Column({ space: 8 }) {
      Text($r('app.string.nearby_join_codeLabel'))
        .fontSize(18)
        .fontWeight(FontWeight.Bold)
        .fontColor(COLOR_ON_SURFACE)
        .width('100%')
      TextInput({ text: this.joinInput })
        .type(InputType.Number)
        .height(48)
        .width('100%')
        .fontColor(COLOR_ON_SURFACE)
        .onChange((value: string) => this.joinInputChanged(value))
      if (this.joinError) {
        Text(this.joinSubmitted
          ? $r('app.string.nearby_stage_discovery_reason')
          : $r('app.string.nearby_reason_code_invalidFormat'))
          .fontSize(14)
          .fontColor(COLOR_BLOCKED)
          .width('100%')
      }
      Row({ space: 8 }) {
        Button($r('app.string.nearby_action_submitJoinCode'))
          .layoutWeight(1)
          .height(48)
          .fontSize(16)
          .backgroundColor(COLOR_BLOCKED)
          .fontColor(COLOR_ON_SURFACE)
          .enabled(normalizeInviteCode(this.joinInput) !== '' && !this.joinSubmitted)
          .onClick(() => this.submitJoin())
        Button($r('app.string.nearby_action_cancelRequest'))
          .layoutWeight(1)
          .height(48)
          .fontSize(16)
          .backgroundColor(COLOR_FIELD)
          .fontColor(COLOR_ON_SURFACE)
          .onClick(() => {
            if (this.joinAttemptId > 0) {
              nativeSession.nearbyInviteCancelCode(this.joinAttemptId);
            }
            this.joinSubmitted = false;
            this.joinError = false;
          })
      }
      .width('100%')
    }
    .width('100%')
    .padding(12)
    .backgroundColor(COLOR_SURFACE)
    .borderRadius(8)
    .alignItems(HorizontalAlign.Start)
  }

  @Builder
  codeBlock() {
    Column({ space: 8 }) {
      Text($r('app.string.nearby_code_label'))
        .fontSize(18)
        .fontWeight(FontWeight.Bold)
        .fontColor(COLOR_ON_SURFACE)
        .width('100%')
      TextInput({ placeholder: $r('app.string.nearby_code_label') })
        .type(InputType.Number)
        .maxLength(6)
        .enabled(false)
        .height(48)
        .width('100%')
      Button($r('app.string.nearby_code_confirm'))
        .width('100%')
        .height(48)
        .fontSize(16)
        .enabled(false)
      Text($r('app.string.nearby_blocked_auth'))
        .fontSize(14)
        .fontColor(COLOR_ON_SURFACE_MUTED)
        .width('100%')
        .textAlign(TextAlign.Center)
    }
    .width('100%')
    .padding(12)
    .backgroundColor(COLOR_SURFACE)
    .borderRadius(8)
    .alignItems(HorizontalAlign.Start)
  }

  // Wi-Fi path: all five stages permission…codec rendered in pipeline order, with
  // only the first failing one marked and explained (D5). This is the same
  // pipeline list as the 附近设备 tab, kept as a separate element from the
  // capability rows (§2 resolved reading).
  @Builder
  wifiPathBlock() {
    Column({ space: 8 }) {
      Text($r('app.string.nearby_wifi_path_building'))
        .fontSize(18)
        .fontWeight(FontWeight.Bold)
        .fontColor(COLOR_ON_SURFACE)
        .width('100%')
      ForEach(nearbyPairingStages(this.currentStage(), NEARBY_WIFI_PATH_ORDER),
        (row: NearbyStageRow) => {
          this.stageRow(row)
        }, (row: NearbyStageRow) => row.stage)
      Button($r('app.string.nearby_wifi_system_confirm_once'))
        .width('100%')
        .height(48)
        .fontSize(16)
        .enabled(false)
      Text($r('app.string.nearby_blocked_wifi'))
        .fontSize(14)
        .fontColor(COLOR_ON_SURFACE_MUTED)
        .width('100%')
        .textAlign(TextAlign.Center)
    }
    .width('100%')
    .padding(12)
    .backgroundColor(COLOR_SURFACE)
    .borderRadius(8)
    .alignItems(HorizontalAlign.Start)
  }

  // One pipeline-stage row: name, announced+visible state, and — on the first
  // failing stage only — its reason. Later stages carry neither a reason nor a
  // blocked key (D5).
  @Builder
  stageRow(row: NearbyStageRow) {
    Column({ space: 4 }) {
      Row({ space: 8 }) {
        Circle()
          .width(8)
          .height(8)
          .fill(this.stageMarkerColor(row))
        Text(nearbyString(row.label))
          .fontSize(16)
          .fontColor(row.status === NEARBY_STAGE_STATUS_NOT_REACHED ? COLOR_ON_SURFACE_MUTED
            : COLOR_ON_SURFACE)
          .fontWeight(row.status === NEARBY_STAGE_STATUS_CURRENT ? FontWeight.Bold : FontWeight.Normal)
          .layoutWeight(1)
        // color-not-only: the state is visible text, not only a marker colour.
        Text(nearbyString(row.status))
          .fontSize(12)
          .fontColor(COLOR_ON_SURFACE_MUTED)
      }
      .width('100%')
      .height(32)
      .alignItems(VerticalAlign.Center)
      if (row.reason.length > 0) {
        Text(nearbyString(row.reason))
          .fontSize(14)
          .fontColor(COLOR_BLOCKED)
          .width('100%')
      }
    }
    .width('100%')
    .padding({ left: 12, right: 12, top: 4, bottom: 4 })
    .backgroundColor(COLOR_SURFACE)
    .borderRadius(8)
    .accessibilityText(nearbyString(row.status))
  }

  // Until the session ABI reports the first failing stage, 权限 is the truthful
  // current stage: this build declares no nearby permission and holds none (D1).
  private currentStage(): string {
    return nearbyPairingCurrentStage('');
  }

  private stageMarkerColor(row: NearbyStageRow): string {
    if (row.status === NEARBY_STAGE_STATUS_CURRENT) {
      return COLOR_BLOCKED;
    }
    return row.status === NEARBY_STAGE_STATUS_PASSED ? COLOR_ON_SURFACE_MUTED : COLOR_NOT_REACHED;
  }
}
