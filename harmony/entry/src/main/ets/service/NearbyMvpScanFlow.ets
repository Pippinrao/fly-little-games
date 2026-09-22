export interface NearbyMvpSnapshot {
  state: number;
  reason: number;
  transportResult: number;
  transportOperation: number;
}

export interface NearbyMvpPort {
  join(qrText: string): boolean;
  snapshot(): NearbyMvpSnapshot;
  cancel(): boolean;
}

export type NearbyScanResult = () => Promise<string>;

const IDLE: NearbyMvpSnapshot = {
  state: 0, reason: 0, transportResult: 0, transportOperation: 0
};

export class NearbyMvpScanFlow {
  private readonly port: NearbyMvpPort;
  private visible: boolean = true;
  private disposed: boolean = false;
  private scanning: boolean = false;
  private current: NearbyMvpSnapshot = IDLE;

  constructor(port: NearbyMvpPort) { this.port = port; }

  appear(): void {
    if (!this.disposed) this.visible = true;
  }

  hideForScanner(): void {
    this.visible = false;
  }

  leave(): void {
    if (this.disposed) return;
    this.disposed = true;
    this.visible = false;
    this.port.cancel();
    this.current = { state: 4, reason: 5, transportResult: 0, transportOperation: 0 };
  }

  isScanning(): boolean { return this.scanning; }

  snapshot(): NearbyMvpSnapshot { return this.current; }

  poll(): void {
    if (!this.visible || this.disposed) return;
    this.current = this.port.snapshot();
  }

  async scan(readResult: NearbyScanResult): Promise<boolean> {
    if (this.scanning || this.disposed) return false;
    this.scanning = true;
    try {
      const qrText = await readResult();
      if (this.disposed) return false;
      if (!this.port.join(qrText)) {
        this.current = { state: 4, reason: 1, transportResult: 0, transportOperation: 0 };
        return false;
      }
      this.current = this.port.snapshot();
      if (this.current.state === 0) {
        this.current = { state: 2, reason: 0, transportResult: 0, transportOperation: 4 };
      }
      return true;
    } catch {
      if (!this.disposed) {
        this.current = { state: 4, reason: 1, transportResult: 0, transportOperation: 0 };
      }
      return false;
    } finally {
      this.scanning = false;
    }
  }
}
