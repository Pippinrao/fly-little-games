import { router } from '@kit.ArkUI';

/** Stub sources page so Game Center can route. Task 9 replaces this. */
@Entry
@Component
struct Sources {
  build() {
    Column() {
      Button($r('app.string.game_center_sources'))
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
