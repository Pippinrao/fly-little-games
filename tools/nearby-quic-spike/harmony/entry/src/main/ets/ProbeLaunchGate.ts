export class ProbeRequestDecision {
  id: number;
  error: string;
  constructor(id: number, error: string) { this.id = id; this.error = error; }
}

// Shared for the JS runtime lifetime, including page remounts and new Ability launches.
// No networking here; this is the ownership gate for the one native async job.
export class ProbeLaunchGate {
  private sequence: number = 0;
  private pending: number = 0;
  private busy: boolean = false;

  beginLaunch(): void { this.pending = 0; }
  isRunning(): boolean { return this.busy; }
  request(explicit: boolean, valid: boolean): ProbeRequestDecision {
    if (!explicit) { return new ProbeRequestDecision(0, ''); }
    if (!valid) {
      return new ProbeRequestDecision(0, 'ERROR explicit runProbe requires string bind, peer and pin parameters');
    }
    if (this.busy) {
      return new ProbeRequestDecision(0, 'ERROR probe already running; request rejected without queuing');
    }
    this.sequence += 1;
    this.pending = this.sequence;
    return new ProbeRequestDecision(this.pending, '');
  }
  consume(id: number): boolean {
    if (id === 0 || id !== this.pending || this.busy) { return false; }
    this.pending = 0;
    this.busy = true;
    return true;
  }
  startManual(): boolean {
    if (this.busy) { return false; }
    this.pending = 0;
    this.busy = true;
    return true;
  }
  finish(): void { this.busy = false; }
}

export const probeLaunchGate: ProbeLaunchGate = new ProbeLaunchGate();
