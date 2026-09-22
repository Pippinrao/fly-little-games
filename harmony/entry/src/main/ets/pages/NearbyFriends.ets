import { router, promptAction } from '@kit.ArkUI';
import { common } from '@kit.AbilityKit';
import { NEARBY_TYPE } from '../ui/NearbyTypography';

@Entry
@Component
struct NearbyFriends {
  @State private tab: string = 'devices';
  @State private largeText: boolean = false;
  @State private availableHeight: number = 360;

  aboutToAppear(): void {
    const context = getContext(this) as common.UIAbilityContext;
    this.largeText = (context.config.fontSizeScale ?? 1) > 1.3;
  }

  private unsupported(): void {
    promptAction.showToast({ message: $r('app.string.nearby_not_supported') });
  }

  @Builder
  action(label: Resource, id: string, primary: boolean, action: () => void) {
    Button(label, { type: ButtonType.Normal })
      .id(id)
      .width('100%')
      .constraintSize({ minHeight: 48 })
      .padding({ left: 16, right: 16, top: 8, bottom: 8 })
      .border({ width: 1, color: primary ? '#FF6B5E' : '#393B40' })
      .borderRadius(12)
      .labelStyle({ maxLines: 2 }).fontFamily('HarmonyOS Sans').fontSize(NEARBY_TYPE.action.size)
      .fontWeight(primary ? FontWeight.Medium : FontWeight.Normal)
      .fontColor(primary ? '#121316' : '#F4EFE6')
      .backgroundColor(primary ? '#FF6B5E' : '#1B1D22')
      .onClick(action)
  }

  build() {
    Column() {
      Row({ space: 8 }) {
        Button('‹', { type: ButtonType.Normal })
          .id('nearby_back').width(48).height(48)
          .fontFamily('HarmonyOS Sans').fontSize('26vp').fontColor('#F4EFE6').backgroundColor(Color.Transparent)
          .accessibilityText($r('app.string.nearby_back'))
          .onClick(() => { router.back(); })
        Text($r('app.string.nearby_title'))
          .fontFamily('HarmonyOS Sans').fontSize(NEARBY_TYPE.pageTitle.size).fontWeight(FontWeight.Medium)
          .fontColor('#F4EFE6').layoutWeight(1)
      }.constraintSize({ minHeight: 48 }).width('100%')
      Row({ space: 18 }) {
        Column({ space: 8 }) {
          if (!this.largeText && this.availableHeight > 330) {
            Text($r('app.string.nearby_entry_headline'))
              .id('nearby_entry_headline').fontFamily('HarmonyOS Sans').fontSize(NEARBY_TYPE.paneTitle.size)
              .fontWeight(FontWeight.Medium).fontColor('#F4EFE6').width('100%')
          }
          if (!this.largeText) {
            Text($r('app.string.nearby_entry_subtitle'))
              .id('nearby_entry_subtitle').fontFamily('HarmonyOS Sans').fontSize(NEARBY_TYPE.muted.size)
              .fontColor('#BEB8AE').width('100%')
          }
          Blank()
          this.action($r('app.string.nearby_action_create'), 'nearby_action_create', false,
            () => this.unsupported())
          this.action($r('app.string.nearby_action_enterCode'), 'nearby_action_enterCode', false,
            () => this.unsupported())
          this.action($r('app.string.nearby_action_scanQr'), 'nearby_action_scanQr', true,
            () => { void router.pushUrl({ url: 'pages/NearbyPairing', params: { mode: 'scan' } }); })
        }.width('44%').height('100%').alignItems(HorizontalAlign.Start)
        Column({ space: 12 }) {
          Row({ space: 8 }) {
            this.tabButton($r('app.string.nearby_tab_devices'), 'devices')
            this.tabButton($r('app.string.nearby_tab_friends'), 'friends')
          }.width('100%')
          Text(this.tab === 'friends' ? $r('app.string.nearby_friends_empty')
            : $r('app.string.nearby_devices_empty'))
            .fontFamily('HarmonyOS Sans').fontSize(NEARBY_TYPE.sectionTitle.size).fontWeight(FontWeight.Medium)
            .fontColor('#F4EFE6').width('100%')
          if (!this.largeText) {
            Text($r('app.string.nearby_entry_subtitle'))
              .fontFamily('HarmonyOS Sans').fontSize(NEARBY_TYPE.muted.size).fontColor('#BEB8AE').width('100%')
          }
          Blank()
          if (this.tab === 'friends') {
            this.action($r('app.string.nearby_friends_manage'), 'nearby_secondary_action', false,
              () => {
                void router.pushUrl({ url: 'pages/NearbyFriendsManage' });
              })
          } else {
            this.action($r('app.string.nearby_find_devices'), 'nearby_secondary_action', false,
              () => this.unsupported())
          }
        }.layoutWeight(1).height('100%').alignItems(HorizontalAlign.Start)
          .padding({ left: 18 }).border({ width: { left: 1 }, color: '#393B40' })
      }.width('100%').layoutWeight(1).padding(16).alignItems(VerticalAlign.Top)
    }.id('nearby_page').width('100%').height('100%').backgroundColor('#121316')
      .onAreaChange((_old: Area, area: Area) => { this.availableHeight = Number(area.height); })
  }

  @Builder
  tabButton(label: Resource, value: string) {
    Button(label, { type: ButtonType.Normal })
      .id(`nearby_tab_${value}`)
      .layoutWeight(1).constraintSize({ minHeight: 48 }).padding(8)
      .labelStyle({ maxLines: 2 }).fontFamily('HarmonyOS Sans').fontSize(NEARBY_TYPE.action.size).fontColor(this.tab === value ? '#F4EFE6' : '#BEB8AE')
      .borderRadius(0).backgroundColor(Color.Transparent)
      .border({ width: { bottom: 2 }, color: this.tab === value ? '#FF6B5E' : '#393B40' })
      .onClick(() => { this.tab = value; })
  }
}
