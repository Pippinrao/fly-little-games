import { router } from '@kit.ArkUI';
import {
  NEARBY_STAGE_STATUS_CURRENT,
  NEARBY_STAGE_STATUS_NOT_REACHED,
  NEARBY_STAGE_STATUS_PASSED,
  NEARBY_WIFI_PATH_ORDER,
  NearbyStageRow,
  nearbyPairingCurrentStage,
  nearbyPairingStages,
  nearbyString
} from '../service/NearbyService';

// Android/game-center parity: the same shell colors as pages/NearbyFriends.ets and
// pages/Settings.ets. No new palette is introduced here.
const COLOR_SURFACE: string = '#1B1F28';
const COLOR_ON_SURFACE: string = '#F2F4F7';
const COLOR_ON_SURFACE_MUTED: string = '#A8B0BD';
const COLOR_NOT_REACHED: string = '#3A404C';
const COLOR_BLOCKED: string = '#FF6B5E';

@Entry
@Component
struct NearbyPairing {
  build() {
    Column() {
      this.topBar()
      Scroll() {
        Column({ space: 16 }) {
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
    .backgroundColor($r('app.color.start_window_background'))
  }

  @Builder
  topBar() {
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
