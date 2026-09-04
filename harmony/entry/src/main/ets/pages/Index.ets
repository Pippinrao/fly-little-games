import { common } from '@kit.AbilityKit';
import { CatalogSmokeViewModel, runCatalogSmoke } from '../service/CatalogSmokeService';

@Entry
@Component
struct Index {
  @State private status: string = 'RUNNING';
  @State private generation: string = '-';
  @State private count: string = '-';
  @State private detail: string = 'Starting native app/catalog validation';

  aboutToAppear(): void {
    const hostContext = this.getUIContext().getHostContext();
    if (hostContext === undefined) {
      this.status = 'FAIL';
      this.detail = 'UIAbility context is unavailable';
      return;
    }

    const result: CatalogSmokeViewModel =
      runCatalogSmoke(hostContext as common.UIAbilityContext);
    this.status = result.status;
    this.generation = result.generation;
    this.count = result.count;
    this.detail = result.detail;
  }

  build() {
    Column({ space: 18 }) {
      Text($r('app.string.validation_title'))
        .fontSize(28)
        .fontWeight(FontWeight.Bold)

      Text(this.status)
        .fontSize(24)
        .fontWeight(FontWeight.Bold)
        .fontColor(this.status === 'PASS' ? '#16823B' : '#B42318')

      Text(`Generation: ${this.generation}`)
        .fontSize(18)

      Text(`Count: ${this.count}`)
        .fontSize(18)

      Text(this.detail)
        .fontSize(14)
        .fontColor('#5E6570')
        .textAlign(TextAlign.Center)
    }
    .width('100%')
    .height('100%')
    .justifyContent(FlexAlign.Center)
    .padding(24)
  }
}
