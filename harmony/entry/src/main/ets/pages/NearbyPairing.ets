import { router } from '@kit.ArkUI';
import { systemDateTime } from '@kit.BasicServicesKit';
import { cryptoFramework } from '@kit.CryptoArchitectureKit';
import nativeSession, { NearbyInviteSnapshotDto } from 'libentry.so';
import { normalizeInviteCode } from '../service/NearbyService';
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
    if (this.joinAttemptId > 0) {
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
      this.topBar()
      Scroll() {
        if (this.availableWidth > 580) {
          Row({ space: 18 }) {
            this.leftPane()
            this.rightPane()
          }
          .width('100%')
          .alignItems(VerticalAlign.Top)
        } else {
          Column({ space: 18 }) {
            this.leftPane()
            this.rightPane()
          }
          .width('100%')
        }
      }
      .width('100%')
      .layoutWeight(1)
      .padding({ left: 16, right: 16, top: 8, bottom: 8 })
      .align(Alignment.TopStart)
      .clip(true)
      this.footer()
    }
    .width('100%')
    .height('100%')
    .backgroundColor(COLOR_BACKGROUND)
    .onAreaChange((_old: Area, area: Area) => {
      this.availableWidth = Number(area.width);
    })
  }

  @Builder
  topBar() {
    Row({ space: 10 }) {
      Text('‹')
        .id('nearby_back')
        .fontSize(26)
        .fontWeight(FontWeight.Bold)
        .fontColor(COLOR_ON_SURFACE)
        .constraintSize({ minWidth: NEARBY_MIN_TAP_HEIGHT_VP, minHeight: NEARBY_MIN_TAP_HEIGHT_VP })
        .textAlign(TextAlign.Center)
        .onClick(() => {
          router.back();
        })
      Text(this.pageTitle())
        .fontSize(NEARBY_TYPE.pageTitle.size)
        .fontWeight(NEARBY_TYPE.pageTitle.weight)
        .lineHeight(NEARBY_TYPE.pageTitle.lineHeight)
        .fontColor(COLOR_ON_SURFACE)
        .layoutWeight(1)
        .maxLines(1)
        .textOverflow({ overflow: TextOverflow.Ellipsis })
      Text($r('app.string.nearby_entry_stageHint'))
        .fontSize(NEARBY_TYPE.muted.size)
        .fontWeight(NEARBY_TYPE.muted.weight)
        .lineHeight(NEARBY_TYPE.muted.lineHeight)
        .fontColor(COLOR_ON_SURFACE_MUTED)
      Button() {
        Text('i')
          .fontSize(16)
          .fontWeight(FontWeight.Bold)
          .fontColor(COLOR_ON_SURFACE)
      }
      .width(48)
      .height(48)
      .backgroundColor('#00000000')
      .borderRadius(12)
      .accessibilityText($r('app.string.nearby_diagnostics_title'))
    }
    .width('100%')
    .constraintSize({ minHeight: 64 })
    .padding({ left: 4, right: 8 })
    .backgroundColor(COLOR_BACKGROUND)
  }

  @Builder
  leftPane() {
    if (this.mode === 'join_code') {
      this.joinLeft()
    } else if (this.mode === 'scan') {
      this.scanLeft()
    } else {
      this.inviteLeft()
    }
  }

  @Builder
  rightPane() {
    if (this.mode === 'join_code') {
      this.joinRight()
    } else if (this.mode === 'scan') {
      this.scanRight()
    } else {
      this.inviteRight()
    }
  }

  @Builder
  inviteLeft() {
    Column({ space: 10 }) {
      Text($r('app.string.nearby_invite_kicker'))
        .fontSize(NEARBY_TYPE.kicker.size)
        .fontWeight(NEARBY_TYPE.kicker.weight)
        .lineHeight(NEARBY_TYPE.kicker.lineHeight)
        .fontColor(COLOR_PRIMARY)
        .letterSpacing(nearbyLetterSpacingFp(NEARBY_TYPE.kicker))
        .width('100%')
      Text($r('app.string.nearby_invite_headline'))
        .id('nearby_invite_headline')
        .fontSize(NEARBY_TYPE.paneTitle.size)
        .fontWeight(NEARBY_TYPE.paneTitle.weight)
        .lineHeight(NEARBY_TYPE.paneTitle.lineHeight)
        .fontColor(COLOR_ON_SURFACE)
        .width('100%')
      Text($r('app.string.nearby_invite_subtitle'))
        .fontSize(NEARBY_TYPE.muted.size)
        .fontWeight(NEARBY_TYPE.muted.weight)
        .lineHeight(NEARBY_TYPE.muted.lineHeight)
        .fontColor(COLOR_ON_SURFACE_MUTED)
        .width('100%')
      Text($r('app.string.nearby_invite_codeLabel'))
        .fontSize(NEARBY_TYPE.body.size)
        .fontWeight(NEARBY_TYPE.body.weight)
        .lineHeight(NEARBY_TYPE.body.lineHeight)
        .fontColor(COLOR_ON_SURFACE)
        .width('100%')
      Text(this.inviteCode === '' ? '· · · · · ·' : this.inviteCode)
        .fontSize(NEARBY_TYPE.inviteCode.size)
        .fontWeight(NEARBY_TYPE.inviteCode.weight)
        .letterSpacing(nearbyLetterSpacingFp(NEARBY_TYPE.inviteCode))
        .lineHeight(NEARBY_TYPE.inviteCode.lineHeight)
        .fontColor(COLOR_ON_SURFACE)
        .width('100%')
        .maxLines(1)
        .textAlign(TextAlign.Center)
        .padding({ top: 10, bottom: 10 })
        .backgroundColor(COLOR_FIELD)
        .borderRadius(10)
      Row({ space: 4 }) {
        Text($r('app.string.nearby_invite_validFor'))
          .fontSize(NEARBY_TYPE.muted.size)
          .fontWeight(NEARBY_TYPE.muted.weight)
          .lineHeight(NEARBY_TYPE.muted.lineHeight)
          .fontColor(COLOR_ON_SURFACE_MUTED)
        Text(`${this.remainingInviteSeconds()}s`)
          .fontSize(NEARBY_TYPE.muted.size)
          .fontWeight(NEARBY_TYPE.muted.weight)
          .lineHeight(NEARBY_TYPE.muted.lineHeight)
          .fontColor(COLOR_ON_SURFACE_MUTED)
      }
      .width('100%')
      Button($r('app.string.nearby_action_regenerate'))
        .width('100%')
        .constraintSize({ minHeight: NEARBY_MIN_TAP_HEIGHT_VP })
        .fontSize(NEARBY_TYPE.action.size)
        .fontWeight(NEARBY_TYPE.action.weight)
        .backgroundColor(COLOR_SURFACE)
        .fontColor(COLOR_ON_SURFACE)
        .onClick(() => this.regenerateInvite())
    }
    .width(this.availableWidth > 580 ? 224 : '100%')
    .alignItems(HorizontalAlign.Start)
  }

  @Builder
  inviteRight() {
    Column({ space: 10 }) {
      Column() {
        Text($r('app.string.nearby_invite_qrLabel'))
          .fontSize(NEARBY_TYPE.muted.size)
          .fontWeight(NEARBY_TYPE.muted.weight)
          .lineHeight(NEARBY_TYPE.muted.lineHeight)
          .fontColor(COLOR_ON_SURFACE_MUTED)
      }
      .width(170)
      .height(170)
      .flexShrink(0)
      .backgroundColor('#FFFFFF')
      .justifyContent(FlexAlign.Center)
      Text($r('app.string.nearby_invite_qrHint'))
        .fontSize(NEARBY_TYPE.sectionTitle.size)
        .fontWeight(NEARBY_TYPE.sectionTitle.weight)
        .lineHeight(NEARBY_TYPE.sectionTitle.lineHeight)
        .fontColor(COLOR_ON_SURFACE)
      Text($r('app.string.nearby_invite_hostMustAccept'))
        .fontSize(NEARBY_TYPE.muted.size)
        .fontWeight(NEARBY_TYPE.muted.weight)
        .lineHeight(NEARBY_TYPE.muted.lineHeight)
        .fontColor(COLOR_ON_SURFACE_MUTED)
        .textAlign(TextAlign.Center)
    }
    .width('100%')
    .layoutWeight(this.availableWidth > 580 ? 1 : 0)
    .justifyContent(FlexAlign.Center)
    .alignItems(HorizontalAlign.Center)
  }

  @Builder
  joinLeft() {
    Column({ space: 10 }) {
      Text($r('app.string.nearby_join_kicker'))
        .fontSize(NEARBY_TYPE.kicker.size)
        .fontWeight(NEARBY_TYPE.kicker.weight)
        .lineHeight(NEARBY_TYPE.kicker.lineHeight)
        .fontColor(COLOR_PRIMARY)
        .letterSpacing(nearbyLetterSpacingFp(NEARBY_TYPE.kicker))
        .width('100%')
      Text($r('app.string.nearby_join_headline'))
        .fontSize(NEARBY_TYPE.paneTitle.size)
        .fontWeight(NEARBY_TYPE.paneTitle.weight)
        .lineHeight(NEARBY_TYPE.paneTitle.lineHeight)
        .fontColor(COLOR_ON_SURFACE)
        .width('100%')
      Text($r('app.string.nearby_join_subtitle'))
        .fontSize(NEARBY_TYPE.muted.size)
        .fontWeight(NEARBY_TYPE.muted.weight)
        .lineHeight(NEARBY_TYPE.muted.lineHeight)
        .fontColor(COLOR_ON_SURFACE_MUTED)
        .width('100%')
      Button($r('app.string.nearby_action_switchToScan'))
        .width('100%')
        .constraintSize({ minHeight: NEARBY_MIN_TAP_HEIGHT_VP })
        .fontSize(NEARBY_TYPE.action.size)
        .fontWeight(NEARBY_TYPE.action.weight)
        .backgroundColor(COLOR_SURFACE)
        .fontColor(COLOR_ON_SURFACE)
        .onClick(() => {
          this.mode = 'scan';
        })
    }
    .width(this.availableWidth > 580 ? 224 : '100%')
    .alignItems(HorizontalAlign.Start)
  }

  @Builder
  joinRight() {
    Column({ space: 12 }) {
      Text($r('app.string.nearby_join_codeLabel'))
        .fontSize(NEARBY_TYPE.body.size)
        .fontWeight(NEARBY_TYPE.body.weight)
        .lineHeight(NEARBY_TYPE.body.lineHeight)
        .fontColor(COLOR_ON_SURFACE)
        .width('100%')
      TextInput({ text: this.joinInput, placeholder: $r('app.string.nearby_join_placeholder') })
        .id('nearby_join_code_input')
        .type(InputType.Number)
        .maxLength(6)
        .constraintSize({ minHeight: 60 })
        .width('100%')
        .fontSize(NEARBY_TYPE.codeInput.size)
        .fontWeight(NEARBY_TYPE.codeInput.weight)
        .letterSpacing(nearbyLetterSpacingFp(NEARBY_TYPE.codeInput))
        .textAlign(TextAlign.Center)
        .fontColor(COLOR_ON_SURFACE)
        .placeholderColor(COLOR_ON_SURFACE_MUTED)
        .backgroundColor(COLOR_SURFACE)
        .enabled(!this.joinSubmitted)
        .onChange((value: string) => this.joinInputChanged(value))
      if (this.joinError) {
        Text(this.joinSubmitted || this.joinFailed
          ? $r('app.string.nearby_stage_discovery_reason')
          : $r('app.string.nearby_reason_code_invalidFormat'))
          .fontSize(NEARBY_TYPE.body.size)
          .fontWeight(NEARBY_TYPE.body.weight)
          .lineHeight(NEARBY_TYPE.body.lineHeight)
          .fontColor(COLOR_AMBER)
          .width('100%')
      }
      Text($r('app.string.nearby_join_hint'))
        .fontSize(NEARBY_TYPE.muted.size)
        .fontWeight(NEARBY_TYPE.muted.weight)
        .lineHeight(NEARBY_TYPE.muted.lineHeight)
        .fontColor(COLOR_ON_SURFACE_MUTED)
        .width('100%')
    }
    .width('100%')
    .layoutWeight(this.availableWidth > 580 ? 1 : 0)
    .alignItems(HorizontalAlign.Start)
  }

  @Builder
  scanLeft() {
    Column({ space: 10 }) {
      Text($r('app.string.nearby_scan_kicker'))
        .fontSize(NEARBY_TYPE.kicker.size)
        .fontWeight(NEARBY_TYPE.kicker.weight)
        .lineHeight(NEARBY_TYPE.kicker.lineHeight)
        .fontColor(COLOR_PRIMARY)
        .letterSpacing(nearbyLetterSpacingFp(NEARBY_TYPE.kicker))
        .width('100%')
      Text($r('app.string.nearby_scan_headline'))
        .fontSize(NEARBY_TYPE.paneTitle.size)
        .fontWeight(NEARBY_TYPE.paneTitle.weight)
        .lineHeight(NEARBY_TYPE.paneTitle.lineHeight)
        .fontColor(COLOR_ON_SURFACE)
        .width('100%')
      Text($r('app.string.nearby_scan_subtitle'))
        .fontSize(NEARBY_TYPE.muted.size)
        .fontWeight(NEARBY_TYPE.muted.weight)
        .lineHeight(NEARBY_TYPE.muted.lineHeight)
        .fontColor(COLOR_ON_SURFACE_MUTED)
        .width('100%')
      Button($r('app.string.nearby_action_switchToJoinCode'))
        .width('100%')
        .constraintSize({ minHeight: NEARBY_MIN_TAP_HEIGHT_VP })
        .fontSize(NEARBY_TYPE.action.size)
        .fontWeight(NEARBY_TYPE.action.weight)
        .backgroundColor(COLOR_SURFACE)
        .fontColor(COLOR_ON_SURFACE)
        .onClick(() => {
          this.mode = 'join_code';
        })
      Text($r('app.string.nearby_scan_cameraNote'))
        .fontSize(NEARBY_TYPE.muted.size)
        .fontWeight(NEARBY_TYPE.muted.weight)
        .lineHeight(NEARBY_TYPE.muted.lineHeight)
        .fontColor(COLOR_ON_SURFACE_MUTED)
        .width('100%')
    }
    .width(this.availableWidth > 580 ? 224 : '100%')
    .alignItems(HorizontalAlign.Start)
  }

  @Builder
  scanRight() {
    Column({ space: 10 }) {
      Column({ space: 14 }) {
        Text('▣')
          .fontSize(44)
          .fontColor(COLOR_ON_SURFACE_MUTED)
        Text($r('app.string.nearby_scan_cameraHint'))
          .fontSize(NEARBY_TYPE.sectionTitle.size)
          .fontWeight(NEARBY_TYPE.sectionTitle.weight)
          .lineHeight(NEARBY_TYPE.sectionTitle.lineHeight)
          .fontColor(COLOR_ON_SURFACE)
          .textAlign(TextAlign.Center)
      }
      .width('100%')
      .constraintSize({ minHeight: 206 })
      .padding(16)
      .justifyContent(FlexAlign.Center)
      .alignItems(HorizontalAlign.Center)
      .border({ width: 2, color: COLOR_ON_SURFACE_MUTED, style: BorderStyle.Dashed })
      .borderRadius(16)
      .backgroundColor(COLOR_SURFACE)
      Text($r('app.string.nearby_scan_afterScan'))
        .fontSize(NEARBY_TYPE.muted.size)
        .fontWeight(NEARBY_TYPE.muted.weight)
        .lineHeight(NEARBY_TYPE.muted.lineHeight)
        .fontColor(COLOR_ON_SURFACE_MUTED)
        .width('100%')
    }
    .width('100%')
    .layoutWeight(this.availableWidth > 580 ? 1 : 0)
    .alignItems(HorizontalAlign.Start)
  }

  @Builder
  footer() {
    Row({ space: 10 }) {
      Text(this.footerCopy())
        .fontSize(NEARBY_TYPE.muted.size)
        .fontWeight(NEARBY_TYPE.muted.weight)
        .lineHeight(NEARBY_TYPE.muted.lineHeight)
        .fontColor(COLOR_ON_SURFACE_MUTED)
        .layoutWeight(1)
      if (this.mode === 'join_code' && (this.joinSubmitted || this.joinFailed)) {
        Button($r('app.string.nearby_action_cancelRequest'))
          .id('nearby_join_cancel')
          .constraintSize({ minWidth: 120, minHeight: NEARBY_MIN_TAP_HEIGHT_VP })
          .fontSize(NEARBY_TYPE.action.size)
          .fontWeight(NEARBY_TYPE.action.weight)
          .backgroundColor(COLOR_SURFACE)
          .fontColor(COLOR_ON_SURFACE)
          .onClick(() => this.cancelJoinAttempt())
      }
      Button(this.footerActionLabel())
        .id(this.mode === 'join_code' ? 'nearby_join_submit'
          : (this.mode === 'scan' ? 'nearby_action_cancel' : 'nearby_invite_cancel'))
        .constraintSize({ minWidth: 160, minHeight: NEARBY_MIN_TAP_HEIGHT_VP })
        .fontSize(this.mode === 'join_code' ? NEARBY_TYPE.primaryAction.size : NEARBY_TYPE.action.size)
        .fontWeight(this.mode === 'join_code' ? NEARBY_TYPE.primaryAction.weight : NEARBY_TYPE.action.weight)
        .backgroundColor(this.mode === 'join_code' ? COLOR_PRIMARY : COLOR_SURFACE)
        .fontColor(this.mode === 'join_code' ? COLOR_ON_PRIMARY : COLOR_ON_SURFACE)
        .enabled(!(this.mode === 'join_code' && this.joinSubmitted))
        .onClick(() => this.footerAction())
    }
    .width('100%')
    .constraintSize({ minHeight: 58 })
    .padding({ left: 16, right: 16, top: 8, bottom: 8 })
    .border({ width: { top: 1 }, color: COLOR_LINE })
    .backgroundColor(COLOR_BACKGROUND)
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
      router.back();
      return;
    }
    this.cancelInvite();
  }
}
