import { describe, it, expect } from '@ohos/hypium';
import nativeSession from 'libentry.so';

export default function nearbyMvpHostTest(): void {
  describe('NearbyMvpHost', () => {
    it('real_napi_host_publishes_a_shared_invite', 0, async () => {
      nativeSession.nearbyMvpCancel();
      expect(nativeSession.nearbyMvpHost(new Uint8Array(16))).assertTrue();
      let invite = '';
      const deadline = Date.now() + 8_000;
      while (invite.length === 0 && Date.now() < deadline) {
        invite = nativeSession.nearbyMvpInvite();
        if (invite.length === 0) {
          await new Promise<void>((resolve: () => void): void => { setTimeout(resolve, 20); });
        }
      }
      expect(invite.startsWith('flynes-lan-v1:')).assertTrue();
      expect(nativeSession.nearbyMvpSnapshot().role).assertEqual(1);
      nativeSession.nearbyMvpCancel();
    });
  });
}
