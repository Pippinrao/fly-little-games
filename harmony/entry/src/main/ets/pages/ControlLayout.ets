import { common } from '@kit.AbilityKit';
import { promptAction, router } from '@kit.ArkUI';
import nativeApp from 'libentry.so';
import {
  ControlLayoutWarningCode,
  ControlLayoutWarnings,
  ControlPlacement,
  UndoStack
} from './ControlLayoutWarnings';

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

class LayoutSnapshot {
  opacity: number = 0.52;
  elements: LayoutElement[] = [];
}

const ELEMENT_NAMES: string[] = ['D_PAD', 'A', 'B', 'SELECT', 'START'];
const MIN_OPACITY: number = 0.4;
const MAX_OPACITY: number = 1.0;
const MIN_SCALE: number = 0.5;
const MAX_SCALE: number = 1.8;
const COLOR_WARNING: string = '#FFB35E';

@Entry
@Component
struct ControlLayout {
  @State private layoutOpacity: number = 0.52;
  @State private selected: string = 'A';
  @State private pressed: string = '';
  @State private tryMode: boolean = false;
  @State private elements: LayoutElement[] = ControlLayout.defaultElements();
  @State private warningText: string = '';
  @State private stageW: number = 1;
  @State private stageH: number = 1;
  @State private status: string = '';
  private readonly undoStack: UndoStack<LayoutSnapshot> = new UndoStack<LayoutSnapshot>(20);
  private dragName: string = '';
  private dragStartX: number = 0;
  private dragStartY: number = 0;
  private fingerOriginX: number = 0;
  private fingerOriginY: number = 0;
  private pushedForGesture: boolean = false;

  aboutToAppear(): void {
    this.loadLayout();
  }

  build() {
    Row() {
      Stack() {
        Button()
          .width('100%')
          .height('100%')
          .backgroundColor('#0D0E11')
          .stateEffect(false)
          .enabled(!this.tryMode)
          .onClick((event: ClickEvent) => {
            if (this.tryMode) {
              return;
            }
            this.pushUndo();
            const nx = event.x / Math.max(1, this.stageW);
            const ny = event.y / Math.max(1, this.stageH);
            this.moveSelected(nx, ny);
            this.refreshWarnings();
          })
        ForEach(this.elements, (element: LayoutElement) => {
          Text(element.name)
            .fontSize(element.name === 'SELECT' || element.name === 'START' ? 11 : 16)
            .fontColor('#F4EFE6')
            .textAlign(TextAlign.Center)
            .width(this.elementWidth(element))
            .height(this.elementHeight(element))
            .backgroundColor(this.controlFill(element.name))
            .border({
              width: 2,
              color: this.controlBorder(element.name)
            })
            .borderRadius(element.name === 'A' || element.name === 'B' ? 40 : 10)
            .opacity(this.layoutOpacity)
            .position({
              x: element.x * this.stageW - this.elementWidth(element) / 2,
              y: element.y * this.stageH - this.elementHeight(element) / 2
            })
            .onTouch((event: TouchEvent) => {
              this.handleControlTouch(element.name, event);
            })
            .gesture(
              PanGesture({ fingers: 1, direction: PanDirection.All, distance: 1 })
                .onActionStart(() => {
                  if (this.tryMode) {
                    return;
                  }
                  this.beginDrag(element.name);
                })
                .onActionEnd((event: GestureEvent) => {
                  if (this.tryMode) {
                    return;
                  }
                  this.updateDrag(element.name, event.offsetX, event.offsetY);
                  this.dragName = '';
                  this.pushedForGesture = false;
                  this.refreshWarnings();
                })
                .onActionCancel(() => {
                  this.dragName = '';
                  this.pushedForGesture = false;
                  this.pressed = '';
                })
            )
        }, (element: LayoutElement) => `${element.name}:${element.x.toFixed(4)}:${element.y.toFixed(4)}:${element.scale.toFixed(4)}`)
      }
      .layoutWeight(1)
      .height('100%')
      .onAreaChange((_old: Area, area: Area) => {
        this.stageW = Number(area.width);
        this.stageH = Number(area.height);
      })

      Column({ space: 10 }) {
        Scroll() {
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
        if (this.warningText.length > 0) {
          Text(this.warningText)
            .fontSize(13)
            .fontColor(COLOR_WARNING)
            .width('100%')
        }
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
          .enabled(!this.tryMode)
          .onChange((value: number) => {
            this.pushUndo();
            this.resizeSelected(value);
            this.refreshWarnings();
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
          .enabled(!this.tryMode)
          .onChange((value: number) => {
            this.pushUndo();
            this.layoutOpacity = Math.max(MIN_OPACITY, Math.min(MAX_OPACITY, value));
          })
            Row() {
              Text($r('app.string.control_layout_test'))
            .fontSize(14)
            .fontColor('#F4EFE6')
            .layoutWeight(1)
              Toggle({ type: ToggleType.Switch, isOn: this.tryMode })
            .selectedColor('#FF6B5E')
            .onChange((isOn: boolean) => {
              this.tryMode = isOn;
              this.pressed = '';
            })
            }
            .width('100%')
            .height(48)
          }
          .width('100%')
          .alignItems(HorizontalAlign.Start)
        }
        .width('100%')
        .layoutWeight(1)
        .scrollBar(BarState.Auto)
        Column({ space: 8 }) {
          Row({ space: 8 }) {
            Button($r('app.string.control_layout_undo'))
            .layoutWeight(1)
            .height(48)
            .enabled(!this.tryMode)
            .onClick(() => {
              this.undo();
            })
            Button($r('app.string.control_layout_recommended'))
            .layoutWeight(1.4)
            .height(48)
            .enabled(!this.tryMode)
            .onClick(() => {
              this.pushUndo();
              this.resetRecommended();
              this.refreshWarnings();
            })
          }
          .width('100%')
          Button($r('app.string.control_layout_save'))
            .id('control-layout-save')
            .width('100%')
            .height(52)
            .onClick(() => {
              this.requestSave();
            })
          Button($r('app.string.control_layout_discard'))
            .id('control-layout-discard')
            .width('100%')
            .height(48)
            .onClick(() => {
              router.back();
            })
        }
        .id('control-layout-actions')
        .width('100%')
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

  private controlFill(name: string): string {
    if (this.pressed === name) {
      return '#C8FF6B5E';
    }
    return name === this.selected ? '#8A25282F' : '#8425282F';
  }

  private controlBorder(name: string): string {
    if (this.pressed === name) {
      return '#FFB35E';
    }
    return name === this.selected ? '#FF6B5E' : '#BEB8AE';
  }

  private handleControlTouch(name: string, event: TouchEvent): void {
    const touch = event.type === TouchType.Up || event.type === TouchType.Cancel
      ? (event.changedTouches.length > 0 ? event.changedTouches[0] : undefined)
      : (event.touches.length > 0 ? event.touches[0] : undefined);
    if (touch === undefined) {
      return;
    }
    if (event.type === TouchType.Down) {
      this.selected = name;
      if (this.tryMode) {
        this.pressed = name;
        return;
      }
      this.beginDrag(name);
      this.fingerOriginX = touch.windowX;
      this.fingerOriginY = touch.windowY;
    } else if ((event.type === TouchType.Up || event.type === TouchType.Cancel)) {
      this.pressed = '';
      if (this.tryMode) {
        return;
      }
      if (this.dragName === name) {
        this.updateDrag(name, touch.windowX - this.fingerOriginX, touch.windowY - this.fingerOriginY);
        this.dragName = '';
        this.pushedForGesture = false;
        this.refreshWarnings();
      }
    }
  }

  private beginDrag(name: string): void {
    this.selected = name;
    this.dragName = name;
    if (!this.pushedForGesture) {
      this.pushUndo();
      this.pushedForGesture = true;
    }
    this.dragStartX = 0;
    this.dragStartY = 0;
    for (let index = 0; index < this.elements.length; index++) {
      if (this.elements[index].name === name) {
        this.dragStartX = this.elements[index].x;
        this.dragStartY = this.elements[index].y;
        break;
      }
    }
  }

  private updateDrag(name: string, offsetX: number, offsetY: number): void {
    this.selected = name;
    this.dragName = name;
    const nx = this.dragStartX + offsetX / Math.max(1, this.stageW);
    const ny = this.dragStartY + offsetY / Math.max(1, this.stageH);
    this.moveSelected(nx, ny);
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
      this.refreshWarnings();
      return;
    }
    const context = host as common.UIAbilityContext;
    try {
      nativeApp.appOpen(context.filesDir, context.cacheDir);
      this.applyEncoded(nativeApp.controlLayoutGet());
      this.status = '';
      this.refreshWarnings();
    } catch (error) {
      this.resetRecommended();
      this.status = error instanceof Error ? error.message : 'layout load failed';
      this.refreshWarnings();
    }
  }

  private requestSave(): void {
    this.refreshWarnings();
    const warnings = this.inspectWarnings();
    if (warnings.isEmpty()) {
      this.saveLayout();
      return;
    }
    promptAction.showDialog({
      message: $r('app.string.control_layout_warning_save'),
      buttons: [
        { text: $r('app.string.control_layout_keep_editing'), color: '#A8B0BD' },
        { text: $r('app.string.control_layout_save'), color: '#FF6B5E' }
      ]
    }).then((result) => {
      if (result.index === 1) {
        this.saveLayout();
      }
    }).catch(() => {});
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
      const applied: string = nativeApp.controlLayoutApply(this.encode());
      this.applyEncoded(applied);
      this.status = '';
      router.back();
    } catch (error) {
      this.status = error instanceof Error ? error.message : 'layout save failed';
    }
  }

  private applyEncoded(encoded: string): void {
    const decoded: string = nativeApp.controlLayoutDecodeOrRecommended(encoded);
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

  private pushUndo(): void {
    const snapshot = new LayoutSnapshot();
    snapshot.opacity = this.layoutOpacity;
    snapshot.elements = this.cloneElements();
    this.undoStack.push(snapshot);
  }

  private undo(): void {
    const snapshot = this.undoStack.undo();
    if (snapshot === undefined) {
      return;
    }
    this.layoutOpacity = snapshot.opacity;
    this.elements = snapshot.elements;
    this.refreshWarnings();
  }

  private inspectWarnings(): ControlLayoutWarnings {
    const placements: ControlPlacement[] = [];
    for (let index = 0; index < this.elements.length; index++) {
      const element = this.elements[index];
      placements.push(new ControlPlacement(element.x, element.y, element.scale));
    }
    return ControlLayoutWarnings.inspect(placements);
  }

  private refreshWarnings(): void {
    const warnings = this.inspectWarnings();
    if (warnings.isEmpty()) {
      this.warningText = '';
      return;
    }
    const lines: string[] = [];
    if (warnings.contains(ControlLayoutWarningCode.TOO_SMALL)) {
      lines.push('Some controls are too small.');
    }
    if (warnings.contains(ControlLayoutWarningCode.OVERLAP)) {
      lines.push('Some controls overlap.');
    }
    if (warnings.contains(ControlLayoutWarningCode.GESTURE_ZONE)) {
      lines.push('A control enters the system gesture area.');
    }
    if (warnings.contains(ControlLayoutWarningCode.CENTRAL_PROTECTION)) {
      lines.push('A control covers the central game area.');
    }
    this.warningText = lines.join('\n');
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
