import { common } from '@kit.AbilityKit';
import { router } from '@kit.ArkUI';
import nativeApp from 'libentry.so';

class LayoutElement {
  name: string = '';
  x: number = 0;
  y: number = 0;
  scale: number = 1;
}

class ParsedLayout {
  opacity: number = 0.52;
  elements: LayoutElement[] = [];
}

const ELEMENT_NAMES: string[] = ['D_PAD', 'A', 'B', 'SELECT', 'START'];
const MIN_OPACITY: number = 0.4;
const MAX_OPACITY: number = 1.0;
const MIN_SCALE: number = 0.5;
const MAX_SCALE: number = 1.8;

@Entry
@Component
struct ControlLayout {
  @State private layoutOpacity: number = 0.52;
  @State private selected: string = 'A';
  @State private elements: LayoutElement[] = ControlLayout.defaultElements();
  @State private stageW: number = 1;
  @State private stageH: number = 1;
  @State private status: string = '';
  private dragName: string = '';

  aboutToAppear(): void {
    this.loadLayout();
  }

  build() {
    Row() {
      Stack() {
        Column()
          .width('100%')
          .height('100%')
          .backgroundColor('#0D0E11')
        ForEach(this.elements, (element: LayoutElement) => {
          Text(element.name)
            .fontSize(element.name === 'SELECT' || element.name === 'START' ? 11 : 16)
            .fontColor('#F4EFE6')
            .textAlign(TextAlign.Center)
            .width(this.elementWidth(element))
            .height(this.elementHeight(element))
            .backgroundColor(element.name === this.selected ? '#8A25282F' : '#8425282F')
            .border({
              width: 2,
              color: element.name === this.selected ? '#FF6B5E' : '#BEB8AE'
            })
            .borderRadius(element.name === 'A' || element.name === 'B' ? 40 : 10)
            .opacity(this.layoutOpacity)
            .hitTestBehavior(HitTestMode.None)
            .markAnchor({ x: this.elementWidth(element) / 2, y: this.elementHeight(element) / 2 })
            .position({ x: element.x * this.stageW, y: element.y * this.stageH })
        }, (element: LayoutElement) => element.name)
      }
      .layoutWeight(1)
      .height('100%')
      .onAreaChange((_old: Area, area: Area) => {
        this.stageW = Number(area.width);
        this.stageH = Number(area.height);
      })
      .onTouch((event: TouchEvent) => {
        this.handleStageTouch(event);
      })

      Column({ space: 10 }) {
        Text($r('app.string.control_layout_title'))
          .fontSize(22)
          .fontWeight(FontWeight.Bold)
          .fontColor('#F4EFE6')
          .width('100%')
        Text($r('app.string.control_layout_hint'))
          .fontSize(14)
          .fontColor('#BEB8AE')
          .width('100%')
        if (this.status.length > 0) {
          Text(this.status)
            .fontSize(13)
            .fontColor('#FF6B5E')
            .width('100%')
        }
        Text($r('app.string.control_layout_scale'))
          .fontSize(13)
          .fontColor('#F4EFE6')
          .width('100%')
        Slider({
          value: this.selectedScale(),
          min: MIN_SCALE,
          max: MAX_SCALE,
          step: 0.01,
          style: SliderStyle.OutSet
        })
          .width('100%')
          .onChange((value: number) => {
            this.resizeSelected(value);
          })
        Text($r('app.string.control_layout_opacity_label'))
          .fontSize(13)
          .fontColor('#F4EFE6')
          .width('100%')
        Slider({
          value: this.layoutOpacity,
          min: MIN_OPACITY,
          max: MAX_OPACITY,
          step: 0.01,
          style: SliderStyle.OutSet
        })
          .width('100%')
          .onChange((value: number) => {
            this.layoutOpacity = Math.max(MIN_OPACITY, Math.min(MAX_OPACITY, value));
          })
        Blank()
        Button($r('app.string.control_layout_recommended'))
          .width('100%')
          .height(48)
          .onClick(() => {
            this.resetRecommended();
          })
        Button($r('app.string.control_layout_save'))
          .width('100%')
          .height(52)
          .onClick(() => {
            this.saveLayout();
          })
        Button($r('app.string.control_layout_discard'))
          .width('100%')
          .height(48)
          .onClick(() => {
            router.back();
          })
      }
      .width(320)
      .height('100%')
      .padding(20)
      .backgroundColor('#1B1D22')
    }
    .width('100%')
    .height('100%')
    .backgroundColor('#121316')
  }

  private handleStageTouch(event: TouchEvent): void {
    if (event.touches.length === 0 && event.changedTouches.length === 0) {
      return;
    }
    const touch = event.type === TouchType.Up || event.type === TouchType.Cancel
      ? event.changedTouches[0] : event.touches[0];
    const nx = touch.x / Math.max(1, this.stageW);
    const ny = touch.y / Math.max(1, this.stageH);
    if (event.type === TouchType.Down) {
      const hit = this.hitElement(nx, ny);
      if (hit.length > 0) {
        this.selected = hit;
        this.dragName = hit;
      }
    }
    if (event.type === TouchType.Move && this.dragName.length > 0) {
      this.selected = this.dragName;
      this.moveSelected(nx, ny);
    }
    if (event.type === TouchType.Up || event.type === TouchType.Cancel) {
      this.dragName = '';
    }
  }

  private hitElement(nx: number, ny: number): string {
    for (let index = this.elements.length - 1; index >= 0; index--) {
      const element = this.elements[index];
      const halfW = this.elementWidth(element) / (2 * Math.max(1, this.stageW));
      const halfH = this.elementHeight(element) / (2 * Math.max(1, this.stageH));
      if (Math.abs(nx - element.x) <= halfW && Math.abs(ny - element.y) <= halfH) {
        return element.name;
      }
    }
    return '';
  }

  private moveSelected(x: number, y: number): void {
    const next = this.cloneElements();
    for (let index = 0; index < next.length; index++) {
      if (next[index].name === this.selected) {
        next[index].x = Math.max(0, Math.min(1, x));
        next[index].y = Math.max(0, Math.min(1, y));
      }
    }
    this.elements = next;
  }

  private resizeSelected(scale: number): void {
    const clamped = Math.max(MIN_SCALE, Math.min(MAX_SCALE, scale));
    const next = this.cloneElements();
    for (let index = 0; index < next.length; index++) {
      if (next[index].name === this.selected) {
        next[index].scale = clamped;
      }
    }
    this.elements = next;
  }

  private selectedScale(): number {
    for (let index = 0; index < this.elements.length; index++) {
      if (this.elements[index].name === this.selected) {
        return this.elements[index].scale;
      }
    }
    return 1;
  }

  private elementWidth(element: LayoutElement): number {
    const base = element.name === 'SELECT' || element.name === 'START' ? 72 : 64;
    return base * element.scale;
  }

  private elementHeight(element: LayoutElement): number {
    const base = element.name === 'SELECT' || element.name === 'START' ? 28 : 64;
    return base * element.scale;
  }

  private resetRecommended(): void {
    this.applyEncoded(nativeApp.controlLayoutRecommended());
  }

  private loadLayout(): void {
    const host = this.getUIContext().getHostContext();
    if (host === undefined) {
      this.status = 'UIAbility context is unavailable';
      this.resetRecommended();
      return;
    }
    const context = host as common.UIAbilityContext;
    try {
      nativeApp.appOpen(context.filesDir, context.cacheDir);
      this.applyEncoded(nativeApp.controlLayoutGet());
      this.status = '';
    } catch (error) {
      this.resetRecommended();
      this.status = error instanceof Error ? error.message : 'layout load failed';
    }
  }

  private saveLayout(): void {
    const host = this.getUIContext().getHostContext();
    if (host === undefined) {
      this.status = 'UIAbility context is unavailable';
      return;
    }
    const context = host as common.UIAbilityContext;
    try {
      nativeApp.appOpen(context.filesDir, context.cacheDir);
      const applied = nativeApp.controlLayoutApply(this.encode());
      this.applyEncoded(applied);
      this.status = '';
      router.back();
    } catch (error) {
      this.status = error instanceof Error ? error.message : 'layout save failed';
    }
  }

  private applyEncoded(encoded: string): void {
    const decoded = nativeApp.controlLayoutDecodeOrRecommended(encoded);
    const parsed = ControlLayout.parseWire(decoded);
    this.layoutOpacity = parsed.opacity;
    this.elements = parsed.elements;
  }

  private encode(): string {
    let out = `v2|${this.layoutOpacity.toFixed(4)}|LANDSCAPE`;
    for (let index = 0; index < this.elements.length; index++) {
      const element = this.elements[index];
      out += `|${element.name},${element.x.toFixed(4)},${element.y.toFixed(4)},${element.scale.toFixed(4)}`;
    }
    return out;
  }

  private cloneElements(): LayoutElement[] {
    const copy: LayoutElement[] = [];
    for (let index = 0; index < this.elements.length; index++) {
      const source = this.elements[index];
      const next = new LayoutElement();
      next.name = source.name;
      next.x = source.x;
      next.y = source.y;
      next.scale = source.scale;
      copy.push(next);
    }
    return copy;
  }

  private static defaultElements(): LayoutElement[] {
    return ControlLayout.parseWire(
      'v2|0.5200|LANDSCAPE|D_PAD,0.1000,0.7600,1.0000|A,0.9400,0.6400,1.0000|B,0.8700,0.8600,1.0000|SELECT,0.0900,0.2800,1.0000|START,0.9400,0.2800,1.0000'
    ).elements;
  }

  private static parseWire(encoded: string): ParsedLayout {
    const parts = encoded.split('|');
    const parsed = new ParsedLayout();
    if (parts.length >= 8 && parts[0] === 'v2') {
      parsed.opacity = Number(parts[1]);
      if (!(parsed.opacity >= MIN_OPACITY && parsed.opacity <= MAX_OPACITY)) {
        parsed.opacity = 0.52;
      }
      for (let index = 3; index < parts.length; index++) {
        const fields = parts[index].split(',');
        if (fields.length === 4) {
          const element = new LayoutElement();
          element.name = fields[0];
          element.x = Number(fields[1]);
          element.y = Number(fields[2]);
          element.scale = Number(fields[3]);
          parsed.elements.push(element);
        }
      }
    }
    if (parsed.elements.length !== ELEMENT_NAMES.length) {
      parsed.opacity = 0.52;
      parsed.elements = [
        ControlLayout.elementOf('D_PAD', 0.10, 0.76, 1),
        ControlLayout.elementOf('A', 0.94, 0.64, 1),
        ControlLayout.elementOf('B', 0.87, 0.86, 1),
        ControlLayout.elementOf('SELECT', 0.09, 0.28, 1),
        ControlLayout.elementOf('START', 0.94, 0.28, 1)
      ];
    }
    return parsed;
  }

  private static elementOf(name: string, x: number, y: number, scale: number): LayoutElement {
    const element = new LayoutElement();
    element.name = name;
    element.x = x;
    element.y = y;
    element.scale = scale;
    return element;
  }
}
