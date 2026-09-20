// Test-only ownership for one sequential gameplay recipe. Hypium's watchdog
// rejects its wrapper without cancelling the test body or a native UiTest call.
const LOCAL_DEADLINE_MS: number = 4500; // Below the unchanged 5000ms Hypium budget.

export class GameplayLifecycle {
  private static owner: GameplayLifecycle | undefined = undefined;
  private static suiteFailure: string = '';
  private busy: boolean = false;
  private failure: string = '';
  private deadline: number = 0;
  private recovering: boolean = false;
  private started: boolean = false;
  private finished: boolean = false;
  private pending: Promise<void> = Promise.resolve();
  private recovery: Promise<void> | undefined = undefined;
  private onFailure: (message: string) => void;

  constructor(onFailure: (message: string) => void) { this.onFailure = onFailure; }

  private fail(message: string): void {
    if (this.failure.length === 0) {
      this.failure = message;
      GameplayLifecycle.suiteFailure = message;
      this.onFailure(message);
    }
  }

  checkActive(): void {
    // Recovery starts only after the complete old body has settled. Its native
    // calls may also return late; no other fixture can acquire this device.
    if (this.recovering) { return; }
    if (this.busy && Date.now() >= this.deadline) {
      this.fail('Gameplay local deadline expired; recovery pending native operation settlement');
    }
    if (!this.busy || this.failure.length > 0) {
      throw new Error(this.failure || 'No active gameplay step');
    }
  }

  async run(body: () => Promise<void>): Promise<void> {
    if (GameplayLifecycle.suiteFailure.length > 0 || this.finished || this.busy
      || (GameplayLifecycle.owner !== undefined && GameplayLifecycle.owner !== this)) {
      throw new Error(GameplayLifecycle.suiteFailure || 'Gameplay device is already owned or fixture is finished');
    }
    GameplayLifecycle.owner = this;
    this.started = true;
    this.busy = true;
    this.deadline = Date.now() + LOCAL_DEADLINE_MS;
    this.pending = (async () => {
      try {
        await body();
        this.checkActive();
      } catch (error) {
        this.fail(`${error}`);
        throw error as Error;
      } finally {
        this.busy = false;
      }
    })();
    await this.bounded(this.pending, 'Gameplay local deadline expired; recovery pending native operation settlement');
  }

  private async bounded(task: Promise<void>, message: string): Promise<void> {
    await new Promise<void>((resolve, reject) => {
      const timer = setTimeout(() => {
        this.fail(message);
        reject(new Error(message));
      }, LOCAL_DEADLINE_MS);
      task.then(() => { clearTimeout(timer); resolve(); }, (error: Error) => {
        clearTimeout(timer);
        reject(error);
      });
    });
  }

  async cleanup(body: () => Promise<void>): Promise<void> {
    if (!this.started) { return; }
    if (this.recovery === undefined) {
      // Keep the real Promise, not the timed-out wrapper. Never overlap an
      // uninterruptible UiTest call with route changes or settings restoration.
      this.recovery = this.pending.catch(() => {}).then(async () => {
        this.recovering = true;
        try {
          await body();
          this.finished = true;
          GameplayLifecycle.owner = undefined;
        } catch (error) {
          this.fail(`Gameplay recovery failed: ${error}`);
          throw error as Error;
        } finally {
          this.recovering = false;
        }
      });
    }
    await this.bounded(this.recovery, 'Gameplay recovery pending; retain recovery evidence and stop device reuse');
  }
}
