import { router } from '@kit.ArkUI';
import { NearbyManageAction, nearbyManageActions, nearbyString } from '../service/NearbyService';

// Android/game-center parity: the same shell colors as pages/NearbyFriends.ets.
const COLOR_SURFACE: string = '#1B1F28';
const COLOR_ON_SURFACE: string = '#F2F4F7';
const COLOR_ON_SURFACE_MUTED: string = '#A8B0BD';

@Entry
@Component
struct NearbyFriendsManage {
  build() {
    Column() {
      this.topBar()
      Scroll() {
        Column({ space: 12 }) {
          Text($r('app.string.nearby_friends_section'))
            .fontSize(18)
            .fontWeight(FontWeight.Bold)
            .fontColor(COLOR_ON_SURFACE)
            .width('100%')
          // No friend can be proven to exist without the friend store, so the
          // list shows its empty state and never a sample row (§4).
          Text($r('app.string.nearby_friends_empty'))
            .fontSize(16)
            .fontColor(COLOR_ON_SURFACE)
            .width('100%')
            .textAlign(TextAlign.Center)
          Text($r('app.string.nearby_friends_saved_after_auth'))
            .fontSize(13)
            .fontColor(COLOR_ON_SURFACE_MUTED)
            .width('100%')
            .textAlign(TextAlign.Center)

          // §10 D2: 好友管理 rows live here, reachable from Settings. Every action
          // needs the local friend store, so each entry is present, disabled, and
          // carries nearby.blocked.friend_store (§4).
          ForEach(nearbyManageActions(), (action: NearbyManageAction) => {
            this.actionRow(action)
          }, (action: NearbyManageAction) => action.label)
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
      Text($r('app.string.nearby_friends_manage'))
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

  @Builder
  actionRow(action: NearbyManageAction) {
    Column({ space: 4 }) {
      Button(nearbyString(action.label))
        .width('100%')
        .height(48)
        .fontSize(16)
        .enabled(false)
      Text(nearbyString(action.blocked))
        .fontSize(13)
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
}
