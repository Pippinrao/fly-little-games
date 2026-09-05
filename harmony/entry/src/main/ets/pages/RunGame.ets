import { router } from '@kit.ArkUI';

/** Stub run page so Game Center can route with canonicalId. Task 8 replaces this overlay. */
@Entry
@Component
struct RunGame {
  @State private canonicalId: string = '';

  aboutToAppear(): void {
    const params = router.getParams() as Record<string, Object>;
    if (params !== undefined && params['canonicalId'] !== undefined) {
      this.canonicalId = params['canonicalId'] as string;
    }
  }

  build() {
    Column() {
      Text(this.canonicalId)
        .fontSize(18)
        .fontColor('#F2F4F7')
    }
    .width('100%')
    .height('100%')
    .justifyContent(FlexAlign.Center)
    .backgroundColor('#12141A')
  }
}
