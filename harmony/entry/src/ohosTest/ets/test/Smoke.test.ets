import { describe, it, expect } from '@ohos/hypium';

export default function smokeTest() {
  describe('Smoke', () => {
    it('hypium_scaffold_runs', 0, () => {
      expect('flynes').assertEqual('flynes');
    });
  });
}
