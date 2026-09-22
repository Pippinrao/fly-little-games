import { router, promptAction } from '@kit.ArkUI';
import { NEARBY_TYPE } from '../ui/NearbyTypography';
import { NearbyManageAction, nearbyManageActions, nearbyString } from '../service/NearbyService';

@Entry
@Component
struct NearbyFriendsManage {
  build() {
    Column() {
      Row({ space: 8 }) {
        Button('‹', { type: ButtonType.Normal }).width(48).height(48)
          .fontFamily('HarmonyOS Sans').fontSize('26vp').fontColor('#F4EFE6').backgroundColor(Color.Transparent)
          .accessibilityText($r('app.string.nearby_back')).onClick(() => { router.back(); })
        Text($r('app.string.nearby_friends_manage')).fontFamily('HarmonyOS Sans').fontSize(NEARBY_TYPE.pageTitle.size)
          .fontWeight(FontWeight.Medium).fontColor('#F4EFE6')
      }.width('100%').constraintSize({ minHeight: 48 })
      Row({ space: 24 }) {
        Column({ space: 12 }) {
          Text($r('app.string.nearby_friends_section')).fontFamily('HarmonyOS Sans').fontSize(NEARBY_TYPE.paneTitle.size)
            .fontWeight(FontWeight.Medium).fontColor('#F4EFE6').width('100%')
          Text($r('app.string.nearby_friends_empty')).fontFamily('HarmonyOS Sans').fontSize(NEARBY_TYPE.body.size)
            .fontColor('#BEB8AE').width('100%')
          Text($r('app.string.nearby_not_supported')).fontFamily('HarmonyOS Sans').fontSize(NEARBY_TYPE.muted.size)
            .fontColor('#BEB8AE').width('100%')
        }.width('36%').alignItems(HorizontalAlign.Start)
        Column({ space: 12 }) {
          Row({ space: 12 }) {
            ForEach(nearbyManageActions().slice(0, 2), (action: NearbyManageAction) => {
              this.action(action)
            }, (action: NearbyManageAction) => action.label)
          }.width('100%')
          Row({ space: 12 }) {
            ForEach(nearbyManageActions().slice(2), (action: NearbyManageAction) => {
              this.action(action)
            }, (action: NearbyManageAction) => action.label)
          }.width('100%')
        }.layoutWeight(1)
      }.width('100%').layoutWeight(1).padding(16)
    }.id('nearby_page').width('100%').height('100%').backgroundColor('#121316')
  }

  @Builder
  action(action: NearbyManageAction) {
    Button(nearbyString(action.label), { type: ButtonType.Normal })
      .id(action.label).layoutWeight(1).constraintSize({ minHeight: 64 }).padding(12)
      .labelStyle({ maxLines: 2 }).fontFamily('HarmonyOS Sans').fontSize(NEARBY_TYPE.action.size).fontColor('#F4EFE6')
      .backgroundColor('#1B1D22').border({ width: 1, color: '#393B40' }).borderRadius(12)
      .onClick(() => { promptAction.showToast({ message: $r('app.string.nearby_not_supported') }); })
  }
}
