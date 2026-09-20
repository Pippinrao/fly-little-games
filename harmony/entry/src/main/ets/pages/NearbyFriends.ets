import { router } from '@kit.ArkUI';
import {
  NEARBY_MIN_TAP_HEIGHT_VP,
  NEARBY_TYPE,
  nearbyLetterSpacingFp
} from '../ui/NearbyTypography';

// Colors and geometry are the approved nearby contract
// (docs/superpowers/specs/2026-09-13-nearby-ui-parity-design.md §6 and
// docs/superpowers/specs/assets/nearby-ui-parity-review.html nearby()).
const COLOR_BACKGROUND: string = '#121316';
const COLOR_SURFACE: string = '#1B1D22';
const COLOR_ON_SURFACE: string = '#F4EFE6';
const COLOR_ON_SURFACE_MUTED: string = '#BEB8AE';
const COLOR_PRIMARY: string = '#FF6B5E';
const COLOR_ON_PRIMARY: string = '#121316';
const COLOR_LINE: string = '#393B40';

const TAB_FRIENDS: string = 'friends';
const TAB_DEVICES: string = 'devices';

function savedFriendCount(): number {
  return 0;
}

function initialNearbyTab(savedFriends: number): string {
  return savedFriends > 0 ? TAB_FRIENDS : TAB_DEVICES;
}

@Entry
@Component
struct NearbyFriends {
  @State private tab: string = initialNearbyTab(savedFriendCount());
  @State private availableWidth: number = 0;

  build() {
    Column() {
      this.topBar()
      Scroll() {
        if (this.availableWidth > 580) {
          Row({ space: 18 }) {
            this.leftActions()
            this.rightPane()
          }
          .width('100%')
          .alignItems(VerticalAlign.Top)
        } else {
          Column({ space: 18 }) {
            this.leftActions()
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
      Text($r('app.string.nearby_title'))
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
    .padding({ left: 4, right: 16 })
    .backgroundColor(COLOR_BACKGROUND)
  }

  @Builder
  leftActions() {
    Column({ space: 8 }) {
      Text($r('app.string.nearby_entry_kicker'))
        .fontSize(NEARBY_TYPE.kicker.size)
        .fontWeight(NEARBY_TYPE.kicker.weight)
        .lineHeight(NEARBY_TYPE.kicker.lineHeight)
        .fontColor(COLOR_PRIMARY)
        .letterSpacing(nearbyLetterSpacingFp(NEARBY_TYPE.kicker))
        .width('100%')
      Text($r('app.string.nearby_entry_headline'))
        .id('nearby_entry_headline')
        .fontSize(NEARBY_TYPE.paneTitle.size)
        .fontWeight(NEARBY_TYPE.paneTitle.weight)
        .fontColor(COLOR_ON_SURFACE)
        .width('100%')
        .lineHeight(NEARBY_TYPE.paneTitle.lineHeight)
      Text($r('app.string.nearby_entry_subtitle'))
        .id('nearby_entry_subtitle')
        .fontSize(NEARBY_TYPE.muted.size)
        .fontWeight(NEARBY_TYPE.muted.weight)
        .fontColor(COLOR_ON_SURFACE_MUTED)
        .width('100%')
        .lineHeight(NEARBY_TYPE.muted.lineHeight)
      Button($r('app.string.nearby_action_create'))
        .id('nearby_action_create')
        .width('100%')
        .constraintSize({ minHeight: NEARBY_MIN_TAP_HEIGHT_VP })
        .fontSize(NEARBY_TYPE.primaryAction.size)
        .fontWeight(NEARBY_TYPE.primaryAction.weight)
        .backgroundColor(COLOR_PRIMARY)
        .fontColor(COLOR_ON_PRIMARY)
        .onClick(() => {
          void router.pushUrl({ url: 'pages/NearbyPairing', params: { mode: 'create' } });
        })
      Button($r('app.string.nearby_action_enterCode'))
        .id('nearby_action_enterCode')
        .width('100%')
        .constraintSize({ minHeight: NEARBY_MIN_TAP_HEIGHT_VP })
        .fontSize(NEARBY_TYPE.action.size)
        .fontWeight(NEARBY_TYPE.action.weight)
        .backgroundColor(COLOR_SURFACE)
        .fontColor(COLOR_ON_SURFACE)
        .onClick(() => {
          void router.pushUrl({ url: 'pages/NearbyPairing', params: { mode: 'join_code' } });
        })
      Button($r('app.string.nearby_action_scanQr'))
        .width('100%')
        .constraintSize({ minHeight: NEARBY_MIN_TAP_HEIGHT_VP })
        .fontSize(NEARBY_TYPE.action.size)
        .fontWeight(NEARBY_TYPE.action.weight)
        .backgroundColor(COLOR_SURFACE)
        .fontColor(COLOR_ON_SURFACE)
        .onClick(() => {
          void router.pushUrl({ url: 'pages/NearbyPairing', params: { mode: 'scan' } });
        })
    }
    .width(this.availableWidth > 580 ? 224 : '100%')
    .alignItems(HorizontalAlign.Start)
    .padding({ right: this.availableWidth > 580 ? 18 : 0 })
  }

  @Builder
  rightPane() {
    Column({ space: 12 }) {
      this.tabRow()
      if (this.tab === TAB_FRIENDS) {
        this.friendsTab()
      } else {
        this.devicesTab()
      }
    }
    .width('100%')
    .layoutWeight(this.availableWidth > 580 ? 1 : 0)
    .constraintSize({ minWidth: 0 })
    .alignItems(HorizontalAlign.Start)
  }

  @Builder
  tabRow() {
    Row({ space: 10 }) {
      Text($r('app.string.nearby_tab_devices'))
        .fontSize(NEARBY_TYPE.action.size)
        .fontWeight(this.tab === TAB_DEVICES ? NEARBY_TYPE.primaryAction.weight : NEARBY_TYPE.action.weight)
        .lineHeight(NEARBY_TYPE.action.lineHeight)
        .fontColor(this.tab === TAB_DEVICES ? COLOR_ON_SURFACE : COLOR_ON_SURFACE_MUTED)
        .padding({ left: 8, right: 8, top: 12, bottom: 12 })
        .border({
          width: { bottom: 2 },
          color: this.tab === TAB_DEVICES ? COLOR_PRIMARY : '#00000000'
        })
        .onClick(() => {
          this.tab = TAB_DEVICES;
        })
      Text($r('app.string.nearby_tab_friends'))
        .fontSize(NEARBY_TYPE.action.size)
        .fontWeight(this.tab === TAB_FRIENDS ? NEARBY_TYPE.primaryAction.weight : NEARBY_TYPE.action.weight)
        .lineHeight(NEARBY_TYPE.action.lineHeight)
        .fontColor(this.tab === TAB_FRIENDS ? COLOR_ON_SURFACE : COLOR_ON_SURFACE_MUTED)
        .padding({ left: 8, right: 8, top: 12, bottom: 12 })
        .border({
          width: { bottom: 2 },
          color: this.tab === TAB_FRIENDS ? COLOR_PRIMARY : '#00000000'
        })
        .onClick(() => {
          this.tab = TAB_FRIENDS;
        })
      Blank()
      Button() {
        Text(this.tab === TAB_FRIENDS ? '⚙' : '⌕')
          .fontSize(18)
          .fontColor(COLOR_ON_SURFACE)
      }
      .width(48)
      .height(48)
      .type(ButtonType.Circle)
      .backgroundColor(COLOR_SURFACE)
      .accessibilityText(this.tab === TAB_FRIENDS ? $r('app.string.nearby_friends_manage') :
        $r('app.string.nearby_find_devices'))
      .onClick(() => {
        if (this.tab === TAB_FRIENDS) {
          void router.pushUrl({ url: 'pages/NearbyFriendsManage' });
        }
      })
    }
    .width('100%')
    .constraintSize({ minHeight: NEARBY_MIN_TAP_HEIGHT_VP })
    .alignItems(VerticalAlign.Center)
  }

  @Builder
  friendsTab() {
    Column({ space: 8 }) {
      Text($r('app.string.nearby_friends_empty'))
        .fontSize(NEARBY_TYPE.body.size)
        .fontWeight(NEARBY_TYPE.body.weight)
        .lineHeight(NEARBY_TYPE.body.lineHeight)
        .fontColor(COLOR_ON_SURFACE)
        .width('100%')
      Text($r('app.string.nearby_blocked_friend_store'))
        .fontSize(NEARBY_TYPE.muted.size)
        .fontWeight(NEARBY_TYPE.muted.weight)
        .lineHeight(NEARBY_TYPE.muted.lineHeight)
        .fontColor(COLOR_ON_SURFACE_MUTED)
        .width('100%')
      Text($r('app.string.nearby_friends_saved_after_auth'))
        .fontSize(NEARBY_TYPE.muted.size)
        .fontWeight(NEARBY_TYPE.muted.weight)
        .lineHeight(NEARBY_TYPE.muted.lineHeight)
        .fontColor(COLOR_ON_SURFACE_MUTED)
        .width('100%')
        .margin({ top: 4 })
    }
    .width('100%')
    .alignItems(HorizontalAlign.Start)
  }

  @Builder
  devicesTab() {
    Column({ space: 8 }) {
      Text($r('app.string.nearby_devices_empty'))
        .fontSize(NEARBY_TYPE.muted.size)
        .fontWeight(NEARBY_TYPE.muted.weight)
        .lineHeight(NEARBY_TYPE.muted.lineHeight)
        .fontColor(COLOR_ON_SURFACE_MUTED)
        .width('100%')
      Button($r('app.string.nearby_find_devices'))
        .width('100%')
        .constraintSize({ maxWidth: 280, minHeight: NEARBY_MIN_TAP_HEIGHT_VP })
        .fontSize(NEARBY_TYPE.action.size)
        .fontWeight(NEARBY_TYPE.action.weight)
        .enabled(false)
        .backgroundColor(COLOR_SURFACE)
        .fontColor(COLOR_ON_SURFACE)
      Text($r('app.string.nearby_blocked_discovery'))
        .fontSize(NEARBY_TYPE.muted.size)
        .fontWeight(NEARBY_TYPE.muted.weight)
        .lineHeight(NEARBY_TYPE.muted.lineHeight)
        .fontColor(COLOR_ON_SURFACE_MUTED)
        .width('100%')
        .margin({ top: 4 })
    }
    .width('100%')
    .alignItems(HorizontalAlign.Start)
  }

  @Builder
  footer() {
    Row({ space: 10 }) {
      Text($r('app.string.nearby_entry_footer'))
        .fontSize(NEARBY_TYPE.muted.size)
        .fontWeight(NEARBY_TYPE.muted.weight)
        .lineHeight(NEARBY_TYPE.muted.lineHeight)
        .fontColor(COLOR_ON_SURFACE_MUTED)
        .layoutWeight(1)
    }
    .width('100%')
    .constraintSize({ minHeight: 58 })
    .padding({ left: 16, right: 16, top: 8, bottom: 8 })
    .border({ width: { top: 1 }, color: COLOR_LINE })
    .backgroundColor(COLOR_BACKGROUND)
  }
}
