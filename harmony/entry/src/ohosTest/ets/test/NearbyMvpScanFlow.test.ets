import { describe, it, expect } from '@ohos/hypium';
import { NearbyMvpPort, NearbyMvpScanFlow, NearbyMvpSnapshot } from
  '../../../main/ets/service/NearbyMvpScanFlow';

class FakeMvpPort implements NearbyMvpPort {
  joinCalls: number = 0;
  cancelCalls: number = 0;
  joinResult: boolean = true;
  value: NearbyMvpSnapshot = { state: 0, reason: 0, transportResult: 0, transportOperation: 0 };

  join(_: string): boolean {
    this.joinCalls++;
    if (this.joinResult) this.value = { state: 2, reason: 0, transportResult: 0, transportOperation: 4 };
    return this.joinResult;
  }

  snapshot(): NearbyMvpSnapshot { return this.value; }

  cancel(): boolean {
    this.cancelCalls++;
    this.value = { state: 4, reason: 5, transportResult: 0, transportOperation: 0 };
    return true;
  }
}

interface PendingScan {
  promise: Promise<string>;
  resolve: (value: string) => void;
}

function pendingScan(): PendingScan {
  let complete: (value: string) => void = (_: string): void => {};
  const promise = new Promise<string>((resolve: (value: string) => void): void => { complete = resolve; });
  return { promise: promise, resolve: complete };
}

export default function nearbyMvpScanFlowTest(): void {
  describe('NearbyMvpScanFlow', () => {
    it('valid_result_uses_real_port_and_publishes_joining', 0, async () => {
      const port = new FakeMvpPort();
      const flow = new NearbyMvpScanFlow(port);
      expect(await flow.scan(async (): Promise<string> => 'flynes-lan-v1:test')).assertTrue();
      expect(port.joinCalls).assertEqual(1);
      expect(flow.snapshot().state).assertEqual(2);
    });

    it('invalid_result_does_not_create_connection_and_can_retry', 0, async () => {
      const port = new FakeMvpPort();
      port.joinResult = false;
      const flow = new NearbyMvpScanFlow(port);
      expect(await flow.scan(async (): Promise<string> => 'invalid')).assertFalse();
      expect(flow.snapshot().state).assertEqual(4);
      port.joinResult = true;
      expect(await flow.scan(async (): Promise<string> => 'flynes-lan-v1:retry')).assertTrue();
      expect(port.joinCalls).assertEqual(2);
    });

    it('hidden_page_resumes_polling_the_same_owner', 0, () => {
      const port = new FakeMvpPort();
      const flow = new NearbyMvpScanFlow(port);
      flow.hideForScanner();
      port.value = { state: 3, reason: 0, transportResult: 0, transportOperation: 7 };
      flow.poll();
      expect(flow.snapshot().state).assertEqual(0);
      flow.appear();
      flow.poll();
      expect(flow.snapshot().state).assertEqual(3);
      expect(port.cancelCalls).assertEqual(0);
    });

    it('explicit_leave_discards_late_scan_result', 0, async () => {
      const port = new FakeMvpPort();
      const flow = new NearbyMvpScanFlow(port);
      const pending = pendingScan();
      const result = flow.scan(async (): Promise<string> => pending.promise);
      flow.leave();
      pending.resolve('flynes-lan-v1:late');
      expect(await result).assertFalse();
      expect(port.joinCalls).assertEqual(0);
      expect(port.cancelCalls).assertEqual(1);
    });

    it('double_tap_starts_only_one_scanner', 0, async () => {
      const port = new FakeMvpPort();
      const flow = new NearbyMvpScanFlow(port);
      const pending = pendingScan();
      let scannerCalls = 0;
      const first = flow.scan(async (): Promise<string> => { scannerCalls++; return pending.promise; });
      expect(await flow.scan(async (): Promise<string> => { scannerCalls++; return 'duplicate'; })).assertFalse();
      pending.resolve('flynes-lan-v1:one');
      expect(await first).assertTrue();
      expect(scannerCalls).assertEqual(1);
      expect(port.joinCalls).assertEqual(1);
    });
  });
}
