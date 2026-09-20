import { afterAll, beforeAll, describe, it } from '@ohos/hypium';
import { common } from '@kit.AbilityKit';
import { abilityDelegatorRegistry } from '@kit.TestKit';
import { display, window } from '@kit.ArkUI';
import { fileIo } from '@kit.CoreFileKit';
import { ON } from '@ohos.UiTest';
import nativePlay from 'libentry.so';
import { BuiltinGame, BuiltinGames } from '../../../main/ets/service/BuiltinGames';
import { BuiltinPlayFixture } from './BuiltinPlaySmoke.test';
import { GameplayLifecycle } from './GameplayLifecycle';

// Test-only recipes previously visually accepted on Android. Screenshots must
// still be inspected on Harmony; passing input/frame assertions is not gameplay.
function recipe(id: string): string[] {
  switch (id) {
    case 'builtin:super-tilt-bro': return ['START', 'START', 'START', 'WAIT:2000', 'A', 'START',
      'WAIT:1000', 'A', 'WAIT:2500', 'RIGHT:600', 'UP', 'A', 'B', 'LEFT:500'];
    case 'builtin:twin-dragons': return ['START', 'WAIT:1500', 'RIGHT:600', 'A', 'RIGHT:600', 'B', 'LEFT:400'];
    case 'builtin:rhde': return ['START', 'A', 'START', 'A', 'WAIT:2000', 'WAIT:2500', 'WAIT:2500',
      'RIGHT:600', 'A', 'B', 'A', 'RIGHT:200', 'B'];
    case 'builtin:zap-ruder': return ['START', 'DOWN', 'DOWN', 'DOWN', 'DOWN', 'A', 'A', 'A', 'A',
      'WAIT:1500', 'UP:500', 'DOWN:600', 'A', 'WAIT:2000'];
    case 'builtin:concentration-room': return ['START', 'A', 'WAIT:800', 'UP:200', 'DOWN:50',
      'A', 'RIGHT:80', 'A', 'WAIT:1000', 'DOWN:200', 'A'];
    case 'builtin:thwaite': return ['START', 'A', 'START', 'WAIT:1000', 'RIGHT:500', 'A', 'B', 'A', 'A', 'WAIT:2000'];
    case 'builtin:dabg': return ['START', 'A', 'A', 'A', 'A', 'A', 'WAIT:2000', 'RIGHT:600', 'A', 'B', 'LEFT:400'];
    default: throw new Error(`No visually reviewed recipe for ${id}`);
  }
}

function check(value: boolean, message: string): void {
  if (!value) { throw new Error(message); }
}

async function delay(ms: number): Promise<void> {
  await new Promise<void>((resolve) => { setTimeout(resolve, ms); });
}

class GameplayFixture extends BuiltinPlayFixture {
  directory: string = '';
  private observer: number = -1;
  private lifecycle: GameplayLifecycle = new GameplayLifecycle((message: string) => {
    this.stopObserver();
    this.primaryFailure = message;
    console.error(`FlyNES actual gameplay stopped: ${message}; recovery=${this.evidencePath}`);
  });

  protected checkActive(): void { this.lifecycle.checkActive(); }

  protected async cleanupAfterFailure(): Promise<void> {
    // launch() is still part of the pending body. afterAll queues recovery;
    // waiting for that body from inside itself would deadlock.
  }

  private stopObserver(): void {
    if (this.observer !== -1) { clearInterval(this.observer); this.observer = -1; }
  }

  async pause(): Promise<void> {
    await super.pause();
    this.checkActive();
    await this.capture('paused');
  }

  async capture(label: string): Promise<void> {
    this.checkActive();
    const path = `${this.directory}/${label.replace(/:/g, '_')}.png`;
    check(await this.driver.screenCap(path), `Screenshot failed: ${path}`);
    this.checkActive();
    console.info(`FlyNES actual gameplay screenshot=${path}`
      + (this.ownsRoute ? ` frames=${nativePlay.playRuntimeStatus().sourceFrames}` : ' returnedToLibrary=true'));
  }

  async prepare(game: BuiltinGame): Promise<void> {
    await super.prepare(game);
    this.checkActive();
    this.directory = `${this.context!.filesDir}/builtin-actual-gameplay/${Date.now()}-${game.canonicalId.replace(/:/g, '-')}`;
    fileIo.mkdirSync(this.directory, true);
  }

  async captureInitialScene(): Promise<void> {
    await delay(1000);
    this.checkActive();
    await this.capture('00-before');
  }

  async input(token: string): Promise<void> {
    this.checkActive();
    const parts = token.split(':');
    const control = parts[0];
    if (control === 'WAIT') { await delay(Number(parts[1])); this.checkActive(); return; }
    const controls: string[] = ['A', 'B', 'SELECT', 'START', 'UP', 'DOWN', 'LEFT', 'RIGHT'];
    const bit = 1 << controls.indexOf(control);
    const canvas = await this.driver.waitForComponent(ON.type('Canvas'), 700);
    this.checkActive();
    check(canvas !== undefined && canvas !== null, 'Visible gamepad Canvas required');
    const rect = this.mainWindow!.getWindowProperties().windowRect;
    const avoid = this.mainWindow!.getWindowAvoidArea(window.AvoidAreaType.TYPE_SYSTEM);
    const settings = nativePlay.settingsGet();
    const hit = nativePlay.hitMapFromLayout(rect.width, rect.height, display.getDefaultDisplaySync().densityDPI / 160,
      avoid.leftRect.width, avoid.rightRect.width, avoid.topRect.height, avoid.bottomRect.height,
      nativePlay.controlLayoutGet(), Math.max(0, Math.min(2, settings.directionMode - 1)), settings.deadZone);
    const target = hit.controls.find((candidate) => candidate.control === control);
    check(target !== undefined, `Missing ${control} hit region`);
    const x = Math.round(rect.left + target!.centerX);
    const y = Math.round(rect.top + target!.centerY);
    let observed = 0;
    let observerFailure = '';
    const before = nativePlay.playRuntimeStatus().sourceFrames;
    this.observer = setInterval(() => {
      try { this.checkActive(); observed |= nativePlay.playStep().appliedButtons; }
      catch (error) { observerFailure = `${error}`; }
    }, 10);
    try {
      if (parts.length > 1 && Number(parts[1]) >= 200) {
        // A real short swipe stays within the same directional arm. Speed is
        // 200 px/s; the requested duration determines its path length.
        const distance = Math.min(Number(parts[1]) / 5, 100);
        if (control === 'UP' || control === 'DOWN') {
          await this.driver.swipe(x, Math.round(y - distance / 2), x, Math.round(y + distance / 2), 200);
        } else {
          await this.driver.swipe(Math.round(x - distance / 2), y, Math.round(x + distance / 2), y, 200);
        }
      } else {
        await this.driver.click(x, y);
      }
    } finally { this.stopObserver(); }
    this.checkActive();
    check(observerFailure.length === 0, `Observer error: ${observerFailure}`);
    check((observed & bit) !== 0, `UI ${token} not consumed: mask=${observed}`);
    await delay(100);
    this.checkActive();
    check(nativePlay.playStep().appliedButtons === 0, `UI ${token} did not release`);
    check(nativePlay.playRuntimeStatus().sourceFrames > before, 'Frames did not advance during UI touch');
    console.info(`FlyNES actual gameplay input=${token} observedMask=${observed} releasedMask=0 frames=${before}->${nativePlay.playRuntimeStatus().sourceFrames}`);
    await delay(300);
    this.checkActive();
  }

  async step(body: () => Promise<void>): Promise<void> {
    await this.lifecycle.run(body);
  }

  async returnToLibrary(): Promise<void> {
    this.checkActive();
    await super.cleanup();
    this.checkActive();
  }

  async cleanup(): Promise<void> {
    await this.lifecycle.cleanup(async () => {
      await super.cleanup();
      check(this.cleanupFailure.length === 0,
        `Recovery incomplete: ${this.cleanupFailure}; recovery=${this.evidencePath}`);
      check(!this.ownsRoute && !this.settingsChanged, `Recovery incomplete: ${this.evidencePath}`);
      console.info(`FlyNES actual gameplay recovery complete=${this.evidencePath}`);
    });
  }
}

export default function builtinGameplayAcceptanceTest(): void {
  const context = abilityDelegatorRegistry.getAbilityDelegator().getAppContext() as common.UIAbilityContext;
  const games = BuiltinGames.loadSync(context).all();
  for (let index = 0; index < games.length; index++) {
    const game = games[index];
    const actions = recipe(game.canonicalId);
    describe(`BuiltinGameplay_${index + 1}`, () => {
      const fixture = new GameplayFixture();
      beforeAll(async () => { await fixture.step(async () => { await fixture.prepare(game); }); });
      beforeAll(async () => { await fixture.step(async () => { await fixture.launch(game); }); });
      beforeAll(async () => { await fixture.step(async () => { await fixture.captureInitialScene(); }); });
      afterAll(async () => { await fixture.cleanup(); });
      actions.forEach((action: string, stepIndex: number) => {
        it(`${stepIndex + 1}_${action.replace(/:/g, '_')}`, 0, async () => {
          await fixture.step(async () => {
            await fixture.input(action);
            await fixture.capture(`${stepIndex + 1}-${action}`);
          });
        });
      });
      it('actual_scene_pause_resume_and_normal_return', 0, async () => {
        await fixture.step(async () => {
          await fixture.capture('playing');
          await fixture.assertPauseResume();
          await fixture.capture('resumed');
          await fixture.returnToLibrary();
          check(!fixture.ownsRoute && !fixture.settingsChanged, 'Cleanup incomplete');
          await fixture.capture('library');
        });
      });
    });
  }
}
