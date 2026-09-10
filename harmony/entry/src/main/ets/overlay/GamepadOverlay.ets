import nativePlay, { HitMapDto } from 'libentry.so';
import {
  NES_BTN_A,
  NES_BTN_B,
  NES_BTN_DOWN,
  NES_BTN_LEFT,
  NES_BTN_RIGHT,
  NES_BTN_SELECT,
  NES_BTN_START,
  NES_BTN_UP
} from '../service/PlayService';

interface HitControl {
  control: string;
  centerX: number;
  centerY: number;
  width: number;
  height: number;
  shape: string;
}

const MODE_JOYSTICK = 0;
const MODE_FIXED = 1;
const MODE_DPAD = 2;
const FACE_ORDER: string[] = ['A', 'B', 'SELECT', 'START'];
const LAYOUT_OPACITY = 0.52;

class FacePointer {
  id: number = -1;
  bits: number = 0;
  x: number = 0;
  y: number = 0;
}

class OverlayHitMath {
  hit: HitMapDto | undefined = undefined;
  density: number = 1;
  width: number = 0;
  height: number = 0;
  insetL: number = 0;
  insetR: number = 0;
  insetT: number = 0;
  insetB: number = 0;

  safeLeft(): number {
    return this.insetL;
  }

  safeRight(): number {
    return this.width - this.insetR;
  }

  safeTop(): number {
    return this.insetT;
  }

  safeBottom(): number {
    return this.height - this.insetB;
  }

  safeCenterX(): number {
    return (this.safeLeft() + this.safeRight()) / 2;
  }

  dpadCenterX(): number {
    if (this.hit === undefined) {
      return 0;
    }
    return (this.hit.dpadLeft + this.hit.dpadRight) / 2;
  }

  dpadCenterY(): number {
    if (this.hit === undefined) {
      return 0;
    }
    return (this.hit.dpadTop + this.hit.dpadBottom) / 2;
  }

  joystickMode(): boolean {
    return this.hit !== undefined && this.hit.directionMode !== MODE_DPAD;
  }

  followingJoystick(): boolean {
    return this.hit !== undefined && this.hit.directionMode === MODE_JOYSTICK;
  }

  fixedJoystick(): boolean {
    return this.hit !== undefined && this.hit.directionMode === MODE_FIXED;
  }

  controlNamed(name: string): HitControl | undefined {
    if (this.hit === undefined) {
      return undefined;
    }
    for (let index = 0; index < this.hit.controls.length; index++) {
      if (this.hit.controls[index].control === name) {
        return this.hit.controls[index] as HitControl;
      }
    }
    return undefined;
  }

  containsControl(name: string, x: number, y: number): boolean {
    const target = this.controlNamed(name);
    if (target === undefined) {
      return false;
    }
    const left = target.centerX - target.width / 2;
    const right = target.centerX + target.width / 2;
    const top = target.centerY - target.height / 2;
    const bottom = target.centerY + target.height / 2;
    return x >= left && x <= right && y >= top && y <= bottom;
  }

  buttonHit(x: number, y: number): string {
    for (let index = 0; index < FACE_ORDER.length; index++) {
      const name = FACE_ORDER[index];
      if (this.containsControl(name, x, y)) {
        return name;
      }
    }
    return 'NONE';
  }

  bitsFor(name: string): number {
    if (name === 'A') {
      return NES_BTN_A;
    }
    if (name === 'B') {
      return NES_BTN_B;
    }
    if (name === 'SELECT') {
      return NES_BTN_SELECT;
    }
    if (name === 'START') {
      return NES_BTN_START;
    }
    return 0;
  }

  canStartJoystick(x: number, y: number): boolean {
    if (this.hit === undefined) {
      return false;
    }
    if (this.followingJoystick()) {
      return x >= this.safeLeft() && x <= this.safeRight() && y >= this.safeTop() &&
        y <= this.safeBottom() && x < this.safeCenterX();
    }
    if (!this.fixedJoystick()) {
      return false;
    }
    const dx = x - this.dpadCenterX();
    const dy = y - this.dpadCenterY();
    const capture = this.hit.joystickRadius + 16 * this.density;
    return dx * dx + dy * dy <= capture * capture;
  }

  canStartDirection(x: number, y: number): boolean {
    if (this.hit === undefined) {
      return false;
    }
    if (this.joystickMode()) {
      return this.canStartJoystick(x, y);
    }
    const pad = 16 * this.density;
    return x >= this.hit.dpadLeft - pad && x <= this.hit.dpadRight + pad &&
      y >= this.hit.dpadTop - pad && y <= this.hit.dpadBottom + pad;
  }

  clampJoystickCenterX(x: number): number {
    if (this.hit === undefined) {
      return x;
    }
    const minimum = this.safeLeft() + this.hit.joystickRadius;
    const maximum = Math.max(minimum, this.safeCenterX() - this.hit.joystickRadius);
    return Math.max(minimum, Math.min(maximum, x));
  }

  clampJoystickCenterY(y: number): number {
    if (this.hit === undefined) {
      return y;
    }
    const minimum = this.safeTop() + this.hit.joystickRadius;
    const maximum = Math.max(minimum, this.safeBottom() - this.hit.joystickRadius);
    return Math.max(minimum, Math.min(maximum, y));
  }

  directionBits(x: number, y: number, previousBits: number, centerX: number, centerY: number): number {
    if (this.hit === undefined) {
      return 0;
    }
    if (this.joystickMode()) {
      return this.joystickDirectionBits(centerX, centerY, x, y, previousBits);
    }
    const release = previousBits === 0 ? 0 : 8 * this.density;
    if (x < this.hit.dpadLeft - release || x > this.hit.dpadRight + release ||
      y < this.hit.dpadTop - release || y > this.hit.dpadBottom + release) {
      return 0;
    }
    const dx = x - this.dpadCenterX();
    const dy = y - this.dpadCenterY();
    const threshold = (previousBits === 0 ? 18 : 12) * this.density;
    let bits = 0;
    if (dx <= -threshold) {
      bits |= NES_BTN_LEFT;
    } else if (dx >= threshold) {
      bits |= NES_BTN_RIGHT;
    }
    if (dy <= -threshold) {
      bits |= NES_BTN_UP;
    } else if (dy >= threshold) {
      bits |= NES_BTN_DOWN;
    }
    return bits;
  }

  joystickDirectionBits(centerX: number, centerY: number, x: number, y: number, previousBits: number): number {
    if (this.hit === undefined || this.hit.joystickRadius <= 0) {
      return 0;
    }
    const dx = x - centerX;
    const dy = y - centerY;
    const normalized = Math.sqrt(dx * dx + dy * dy) / this.hit.joystickRadius;
    const threshold = previousBits === 0 ? this.hit.deadZone : Math.max(0.08, this.hit.deadZone - 0.06);
    if (normalized < threshold) {
      return 0;
    }
    const ax = Math.abs(dx);
    const ay = Math.abs(dy);
    let bits = 0;
    if (ax >= ay * 0.55) {
      bits |= dx < 0 ? NES_BTN_LEFT : NES_BTN_RIGHT;
    }
    if (ay >= ax * 0.55) {
      bits |= dy < 0 ? NES_BTN_UP : NES_BTN_DOWN;
    }
    return bits;
  }
}

@Component
export struct GamepadOverlay {
  @Prop density: number = 1;
  @Prop insetL: number = 0;
  @Prop insetR: number = 0;
  @Prop insetT: number = 0;
  @Prop insetB: number = 0;
  @Prop @Watch('onLayoutUtf8Changed') layoutUtf8: string = '';
  @Prop directionMode: number = MODE_FIXED;
  @Prop deadZone: number = 0.18;
  @Prop pressedBits: number = 0;
  onButtons: (bits: number) => void = () => {};
  private readonly canvasContext: CanvasRenderingContext2D = new CanvasRenderingContext2D();
  private readonly math: OverlayHitMath = new OverlayHitMath();
  private readonly facePointers: FacePointer[] = [];
  private widthPx: number = 0;
  private heightPx: number = 0;
  private geometryKey: string = '';
  private directionId: number = -1;
  private directionBits: number = 0;
  private rawX: number = 0;
  private rawY: number = 0;
  private effectiveX: number = 0;
  private effectiveY: number = 0;
  private stickX: number = 0;
  private stickY: number = 0;
  private knobX: number = 0;
  private knobY: number = 0;

  onLayoutUtf8Changed(): void {
    this.geometryKey = '';
    this.rebuildHitMap();
    this.redraw();
  }

  private overlayOpacity(): number {
    const fields = this.layoutUtf8.split('|');
    if (fields.length >= 2) {
      const parsed = Number(fields[1]);
      if (parsed >= 0.4 && parsed <= 1) {
        return parsed;
      }
    }
    return LAYOUT_OPACITY;
  }

  build() {
    Canvas(this.canvasContext)
      .width('100%')
      .height('100%')
      .hitTestBehavior(HitTestMode.Default)
      .onReady(() => {
        this.redraw();
      })
      .onAreaChange((_old: Area, area: Area) => {
        this.widthPx = Math.round(this.getUIContext().vp2px(Number(area.width)));
        this.heightPx = Math.round(this.getUIContext().vp2px(Number(area.height)));
        this.rebuildHitMap();
        this.redraw();
      })
      .onTouch((event: TouchEvent) => {
        this.handleTouch(event);
      })
  }

  private handleTouch(event: TouchEvent): void {
    const ui = this.getUIContext();
    if (event.type === TouchType.Down && event.touches.length === 1) {
      this.cancelAll();
    }
    if (event.type === TouchType.Down) {
      for (let index = 0; index < event.changedTouches.length; index++) {
        const touch = event.changedTouches[index];
        this.pointerDown(touch.id, ui.vp2px(touch.x), ui.vp2px(touch.y));
      }
    } else if (event.type === TouchType.Move) {
      for (let index = 0; index < event.touches.length; index++) {
        const touch = event.touches[index];
        this.pointerMove(touch.id, ui.vp2px(touch.x), ui.vp2px(touch.y));
      }
    } else if (event.type === TouchType.Up || event.type === TouchType.Cancel) {
      if (event.type === TouchType.Cancel) {
        this.cancelAll();
      } else {
        for (let index = 0; index < event.changedTouches.length; index++) {
          this.pointerUp(event.changedTouches[index].id);
        }
      }
    }
    this.publish();
    this.redraw();
  }

  private pointerDown(id: number, x: number, y: number): void {
    const hit = this.math.buttonHit(x, y);
    if (hit !== 'NONE') {
      const pointer = new FacePointer();
      pointer.id = id;
      pointer.bits = this.math.bitsFor(hit);
      pointer.x = x;
      pointer.y = y;
      this.facePointers.push(pointer);
      return;
    }
    if (this.directionId !== -1 || !this.math.canStartDirection(x, y)) {
      return;
    }
    this.directionId = id;
    this.rawX = x;
    this.rawY = y;
    if (this.math.followingJoystick()) {
      this.stickX = this.math.clampJoystickCenterX(x);
      this.stickY = this.math.clampJoystickCenterY(y);
      this.effectiveX = this.stickX;
      this.effectiveY = this.stickY;
    } else {
      this.stickX = this.math.dpadCenterX();
      this.stickY = this.math.dpadCenterY();
      this.effectiveX = x;
      this.effectiveY = y;
    }
    this.refreshKnob();
    this.directionBits = this.math.directionBits(this.effectiveX, this.effectiveY, 0, this.stickX, this.stickY);
  }

  private pointerMove(id: number, x: number, y: number): void {
    if (this.directionId === id) {
      const dx = x - this.rawX;
      const dy = y - this.rawY;
      this.rawX = x;
      this.rawY = y;
      this.effectiveX += dx;
      this.effectiveY += dy;
      if (this.math.followingJoystick() && (dx !== 0 || dy !== 0)) {
        this.followBase();
      }
      this.refreshKnob();
      this.directionBits = this.math.directionBits(
        this.effectiveX, this.effectiveY, this.directionBits, this.stickX, this.stickY);
      return;
    }
    const pointer = this.facePointer(id);
    if (pointer === undefined) {
      return;
    }
    pointer.x = x;
    pointer.y = y;
    const current = this.math.bitsFor(this.math.buttonHit(x, y));
    if ((pointer.bits & (NES_BTN_A | NES_BTN_B)) !== 0 && (current & (NES_BTN_A | NES_BTN_B)) !== 0) {
      pointer.bits = pointer.bits | current;
    } else if (current === 0) {
      this.removeFace(id);
    }
  }

  private pointerUp(id: number): void {
    if (this.directionId === id) {
      this.clearDirection();
      return;
    }
    this.removeFace(id);
  }

  private cancelAll(): void {
    this.facePointers.splice(0, this.facePointers.length);
    this.clearDirection();
  }

  private clearDirection(): void {
    this.directionId = -1;
    this.directionBits = 0;
    this.stickX = this.math.dpadCenterX();
    this.stickY = this.math.dpadCenterY();
    this.knobX = this.stickX;
    this.knobY = this.stickY;
  }

  private followBase(): void {
    if (this.math.hit === undefined) {
      return;
    }
    this.stickX = this.math.clampJoystickCenterX(this.stickX);
    this.stickY = this.math.clampJoystickCenterY(this.stickY);
    const dx = this.effectiveX - this.stickX;
    const dy = this.effectiveY - this.stickY;
    const distance = Math.sqrt(dx * dx + dy * dy);
    const travel = this.math.hit.joystickTravelRadius;
    if (distance <= travel || distance === 0) {
      return;
    }
    const follow = (distance - travel) / distance;
    this.stickX = this.math.clampJoystickCenterX(this.stickX + dx * follow);
    this.stickY = this.math.clampJoystickCenterY(this.stickY + dy * follow);
  }

  private refreshKnob(): void {
    if (this.math.hit === undefined) {
      return;
    }
    const dx = this.effectiveX - this.stickX;
    const dy = this.effectiveY - this.stickY;
    const distance = Math.sqrt(dx * dx + dy * dy);
    const travel = this.math.hit.joystickTravelRadius;
    const scale = distance > travel && distance > 0 ? travel / distance : 1;
    this.knobX = this.stickX + dx * scale;
    this.knobY = this.stickY + dy * scale;
  }

  private facePointer(id: number): FacePointer | undefined {
    for (let index = 0; index < this.facePointers.length; index++) {
      if (this.facePointers[index].id === id) {
        return this.facePointers[index];
      }
    }
    return undefined;
  }

  private removeFace(id: number): void {
    for (let index = this.facePointers.length - 1; index >= 0; index--) {
      if (this.facePointers[index].id === id) {
        this.facePointers.splice(index, 1);
      }
    }
  }

  private publish(): void {
    let bits = this.directionBits;
    for (let index = 0; index < this.facePointers.length; index++) {
      bits |= this.facePointers[index].bits;
    }
    this.onButtons(bits);
  }

  private rebuildHitMap(): void {
    if (this.widthPx <= 0 || this.heightPx <= 0) {
      return;
    }
    const layout = this.layoutUtf8.length > 0 ? this.layoutUtf8 : nativePlay.controlLayoutRecommended();
    const key =
      `${this.widthPx}|${this.heightPx}|${this.density}|${this.insetL}|${this.insetR}|${this.insetT}|${this.insetB}|${layout}|${this.directionMode}|${this.deadZone}`;
    if (key === this.geometryKey && this.math.hit !== undefined) {
      return;
    }
    try {
      this.math.hit = nativePlay.hitMapFromLayout(
        this.widthPx,
        this.heightPx,
        this.density,
        this.insetL,
        this.insetR,
        this.insetT,
        this.insetB,
        layout,
        this.directionMode,
        this.deadZone);
      this.math.density = this.density;
      this.math.width = this.widthPx;
      this.math.height = this.heightPx;
      this.math.insetL = this.insetL;
      this.math.insetR = this.insetR;
      this.math.insetT = this.insetT;
      this.math.insetB = this.insetB;
      this.geometryKey = key;
      if (this.directionId === -1) {
        this.stickX = this.math.dpadCenterX();
        this.stickY = this.math.dpadCenterY();
        this.knobX = this.stickX;
        this.knobY = this.stickY;
      }
    } catch (error) {
      const detail = error instanceof Error ? error.message : 'hit map failed';
      console.error(`FlyNES hit map failed: ${detail}`);
    }
  }

  private applyCanvasPixelScale(): void {
    const bufferW = this.canvasContext.width;
    const scale = bufferW > 0 && this.widthPx > 0 ? bufferW / this.widthPx : 1;
    this.canvasContext.setTransform(scale, 0, 0, scale, 0, 0);
  }

  private redraw(): void {
    this.rebuildHitMap();
    const ctx = this.canvasContext;
    const hit = this.math.hit;
    if (hit === undefined || this.widthPx <= 0 || this.heightPx <= 0) {
      return;
    }
    this.applyCanvasPixelScale();
    ctx.clearRect(0, 0, this.widthPx, this.heightPx);
    if (this.math.joystickMode()) {
      this.drawJoystick(ctx, hit);
    } else {
      this.drawDpad(ctx, hit);
    }
    this.drawFace(ctx, 'B', 'B');
    this.drawFace(ctx, 'A', 'A');
    this.drawFace(ctx, 'SELECT', 'SELECT');
    this.drawFace(ctx, 'START', 'START');
  }

  private drawDpad(ctx: CanvasRenderingContext2D, hit: HitMapDto): void {
    const up = this.math.controlNamed('UP');
    const arm = up === undefined ? 48 * this.density : up.width;
    const cx = this.math.dpadCenterX();
    const cy = this.math.dpadCenterY();
    const path = new Path2D();
    path.rect(cx - arm / 2, hit.dpadTop, arm, hit.dpadBottom - hit.dpadTop);
    path.rect(hit.dpadLeft, cy - arm / 2, hit.dpadRight - hit.dpadLeft, arm);
    ctx.fillStyle = this.fillColor((this.pressedBits & (NES_BTN_UP | NES_BTN_DOWN | NES_BTN_LEFT | NES_BTN_RIGHT)) !== 0);
    ctx.fill(path);
    ctx.strokeStyle = this.strokeColor(this.directionId !== -1);
    ctx.lineWidth = (this.directionId !== -1 ? 3 : 2) * this.density;
    ctx.stroke(path);
    this.drawDirectionArm(ctx, 'UP', NES_BTN_UP);
    this.drawDirectionArm(ctx, 'DOWN', NES_BTN_DOWN);
    this.drawDirectionArm(ctx, 'LEFT', NES_BTN_LEFT);
    this.drawDirectionArm(ctx, 'RIGHT', NES_BTN_RIGHT);
  }

  private drawDirectionArm(ctx: CanvasRenderingContext2D, name: string, bit: number): void {
    if ((this.pressedBits & bit) === 0 && (this.directionBits & bit) === 0) {
      return;
    }
    const target = this.math.controlNamed(name);
    if (target === undefined) {
      return;
    }
    ctx.fillStyle = `rgba(244, 239, 230, ${Math.max(0.88, this.overlayOpacity())})`;
    ctx.fillRect(target.centerX - target.width / 2, target.centerY - target.height / 2, target.width, target.height);
  }

  private drawJoystick(ctx: CanvasRenderingContext2D, hit: HitMapDto): void {
    const owned = this.directionId !== -1;
    const cx = owned ? this.stickX : this.math.dpadCenterX();
    const cy = owned ? this.stickY : this.math.dpadCenterY();
    const kx = owned ? this.knobX : cx;
    const ky = owned ? this.knobY : cy;
    ctx.beginPath();
    ctx.arc(cx, cy, hit.joystickRadius, 0, Math.PI * 2);
    ctx.fillStyle = this.fillColor(false);
    ctx.fill();
    ctx.strokeStyle = this.strokeColor(owned);
    ctx.lineWidth = (owned ? 3 : 2) * this.density;
    ctx.stroke();
    ctx.beginPath();
    ctx.arc(kx, ky, hit.joystickRadius * 0.4375, 0, Math.PI * 2);
    ctx.fillStyle = this.fillColor(owned && this.directionBits !== 0);
    ctx.fill();
    ctx.strokeStyle = this.strokeColor(owned);
    ctx.stroke();
  }

  private drawFace(ctx: CanvasRenderingContext2D, name: string, label: string): void {
    const target = this.math.controlNamed(name);
    if (target === undefined) {
      return;
    }
    const bit = this.math.bitsFor(name);
    const pressed = (this.pressedBits & bit) !== 0;
    let width = target.width;
    let height = target.height;
    if (target.shape === 'PILL') {
      height = 28 * this.density;
    } else if (pressed) {
      width = Math.max(8, width - 4 * this.density);
      height = Math.max(8, height - 4 * this.density);
    }
    const left = target.centerX - width / 2;
    const top = target.centerY - height / 2;
    ctx.fillStyle = this.fillColor(pressed);
    ctx.strokeStyle = name === 'A' ? `rgba(255, 107, 94, ${this.overlayOpacity()})` : this.strokeColor(false);
    ctx.lineWidth = (name === 'A' ? 3 : 2) * this.density;
    ctx.beginPath();
    if (target.shape === 'CIRCLE') {
      ctx.arc(target.centerX, target.centerY, width / 2, 0, Math.PI * 2);
    } else {
      const radius = target.shape === 'PILL' ? height / 2 : 12 * this.density;
      this.addRoundRect(ctx, left, top, width, height, radius);
    }
    ctx.fill();
    ctx.stroke();
    ctx.fillStyle = pressed ? 'rgba(18, 19, 22, 1)' : `rgba(255, 255, 255, ${this.overlayOpacity()})`;
    // Android GamepadView draws labels with fontPx = (PILL?11:22) * density, giving a
    // glyph ~22% of the button size. On OHOS the canvas font value does not follow the
    // same pipeline as geometry: measured on the emulator, the rendered glyph equals
    // fontValue / density, so the font value must be button-relative AND multiplied by
    // density again. fontValue = button * ratio * density => glyph ≈ button * ratio * 0.72.
    const labelPx = target.shape === 'PILL'
      ? Math.max(28, Math.round(height * 0.42 * this.density))
      : Math.max(28, Math.round(Math.min(width, height) * 0.30 * this.density));
    ctx.font = `bold ${labelPx}px sans-serif`;
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText(label, target.centerX, target.centerY);
  }

  private addRoundRect(ctx: CanvasRenderingContext2D, left: number, top: number,
    width: number, height: number, radius: number): void {
    const cap = Math.min(radius, width / 2, height / 2);
    const right = left + width;
    const bottom = top + height;
    ctx.moveTo(left + cap, top);
    ctx.lineTo(right - cap, top);
    ctx.arc(right - cap, top + cap, cap, -Math.PI / 2, 0);
    ctx.lineTo(right, bottom - cap);
    ctx.arc(right - cap, bottom - cap, cap, 0, Math.PI / 2);
    ctx.lineTo(left + cap, bottom);
    ctx.arc(left + cap, bottom - cap, cap, Math.PI / 2, Math.PI);
    ctx.lineTo(left, top + cap);
    ctx.arc(left + cap, top + cap, cap, Math.PI, Math.PI * 3 / 2);
    ctx.closePath();
  }

  private fillColor(pressed: boolean): string {
    if (pressed) {
      return `rgba(244, 239, 230, ${Math.max(0.88, this.overlayOpacity())})`;
    }
    return `rgba(37, 40, 47, ${this.overlayOpacity()})`;
  }

  private strokeColor(owned: boolean): string {
    if (owned) {
      return `rgba(255, 107, 94, ${Math.max(0.88, this.overlayOpacity())})`;
    }
    return `rgba(190, 184, 174, ${this.overlayOpacity()})`;
  }
}
