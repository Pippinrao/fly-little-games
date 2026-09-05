import { router } from '@kit.ArkUI';

/** Stub settings page so Game Center can route. Task 9 replaces this. */
@Entry
@Component
struct Settings {
  build() {
    Column() {
      Button($r('app.string.game_center_settings'))
        .onClick(() => {
          router.back();
        })
    }
    .width('100%')
    .height('100%')
    .justifyContent(FlexAlign.Center)
    .backgroundColor('#12141A')
  }
}
