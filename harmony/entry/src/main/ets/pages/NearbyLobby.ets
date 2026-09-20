import { router } from '@kit.ArkUI';
import { NearbyLobbyRow, nearbyLobbyDetailRows, nearbyLobbyFirstScreenRows, nearbyString } from '../service/NearbyService';
import {
  NEARBY_MIN_TAP_HEIGHT_VP,
  NEARBY_TYPE
} from '../ui/NearbyTypography';

// Android/game-center parity: the same shell colors as pages/NearbyFriends.ets.
const COLOR_BACKGROUND: string = '#121316';
const COLOR_SURFACE: string = '#1B1D22';
const COLOR_RAISED: string = '#292B31';
const COLOR_ON_SURFACE: string = '#F4EFE6';
const COLOR_ON_SURFACE_MUTED: string = '#BEB8AE';
const COLOR_PRIMARY: string = '#FF6B5E';

const LOBBY_CONFIRM: string = 'nearby_lobby_confirm';

@Entry
@Component
struct NearbyLobby {
  @State private detailsOpen: boolean = false;

  build() {
    Column() {
      this.topBar()
      Scroll() {
        Column({ space: 8 }) {
          ForEach(nearbyLobbyFirstScreenRows(), (row: NearbyLobbyRow) => {
            this.fieldRow(row)
          }, (row: NearbyLobbyRow) => row.label)
          Button($r('app.string.nearby_diagnostics_title'))
            .id('nearby_lobby_details')
            .width('100%')
            .constraintSize({ minHeight: NEARBY_MIN_TAP_HEIGHT_VP })
            .fontSize(NEARBY_TYPE.action.size)
            .fontWeight(NEARBY_TYPE.action.weight)
            .backgroundColor(COLOR_SURFACE)
            .fontColor(COLOR_ON_SURFACE)
            .onClick(() => {
              this.detailsOpen = !this.detailsOpen;
            })
          if (this.detailsOpen) {
            ForEach(nearbyLobbyDetailRows(), (row: NearbyLobbyRow) => {
              this.fieldRow(row)
            }, (row: NearbyLobbyRow) => row.label)
          }
        }
        .width('100%')
        .padding(16)
        .alignItems(HorizontalAlign.Start)
      }
      .width('100%')
      .layoutWeight(1)
      this.confirmFooter()
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
        .constraintSize({ minWidth: NEARBY_MIN_TAP_HEIGHT_VP, minHeight: NEARBY_MIN_TAP_HEIGHT_VP })
        .textAlign(TextAlign.Center)
        .onClick(() => {
          router.back();
        })
      // Page title is this page's own name, not the feature name nearby.title.
      Text($r('app.string.nearby_lobby_title'))
        .fontSize(NEARBY_TYPE.pageTitle.size)
        .fontWeight(NEARBY_TYPE.pageTitle.weight)
        .lineHeight(NEARBY_TYPE.pageTitle.lineHeight)
        .fontColor(COLOR_ON_SURFACE)
        .layoutWeight(1)
    }
    .width('100%')
    .constraintSize({ minHeight: 56 })
    .padding({ left: 4, right: 16 })
    .backgroundColor(COLOR_SURFACE)
  }

  @Builder
  fieldRow(row: NearbyLobbyRow) {
    Column({ space: 4 }) {
      Text(nearbyString(row.label))
        .fontSize(NEARBY_TYPE.body.size)
        .fontWeight(NEARBY_TYPE.body.weight)
        .lineHeight(NEARBY_TYPE.body.lineHeight)
        .fontColor(COLOR_ON_SURFACE)
        .width('100%')
      Text(nearbyString(row.blocked))
        .fontSize(NEARBY_TYPE.muted.size)
        .fontWeight(NEARBY_TYPE.muted.weight)
        .lineHeight(NEARBY_TYPE.muted.lineHeight)
        .fontColor(COLOR_ON_SURFACE_MUTED)
        .width('100%')
    }
    .id(row.label)
    .width('100%')
    .padding({ left: 12, right: 12, top: 8, bottom: 8 })
    .backgroundColor(COLOR_RAISED)
    .borderRadius(8)
    .alignItems(HorizontalAlign.Start)
  }

  // D8: 双端确认 is one primary 确认入局 button per side, bound to a pending-config
  // fingerprint both ends render identically. Neither the button nor the
  // fingerprint can exist before the session surface does, so the single control
  // is present and disabled with its reason instead of being hidden (§4).
  // The 确认已失效 banner needs an authority/seat change plus a pending confirm,
  // so it appears with the slice that owns those values; rendering it now would
  // claim a confirmation that never existed.
  @Builder
  confirmFooter() {
    Column({ space: 8 }) {
      Button(nearbyString(LOBBY_CONFIRM))
        .id('nearby_lobby_confirm')
        .width('100%')
        .constraintSize({ maxWidth: 320, minHeight: NEARBY_MIN_TAP_HEIGHT_VP })
        .fontSize(NEARBY_TYPE.primaryAction.size)
        .fontWeight(NEARBY_TYPE.primaryAction.weight)
        .backgroundColor(COLOR_PRIMARY)
        .fontColor(COLOR_ON_SURFACE)
        .enabled(false)
      Text($r('app.string.nearby_blocked_session_read'))
        .fontSize(NEARBY_TYPE.muted.size)
        .fontWeight(NEARBY_TYPE.muted.weight)
        .lineHeight(NEARBY_TYPE.muted.lineHeight)
        .fontColor(COLOR_ON_SURFACE_MUTED)
        .width('100%')
        .textAlign(TextAlign.Center)
    }
    .width('100%')
    .padding({ left: 16, right: 16, top: 12, bottom: 16 })
    .backgroundColor(COLOR_SURFACE)
    .alignItems(HorizontalAlign.Center)
  }
}
