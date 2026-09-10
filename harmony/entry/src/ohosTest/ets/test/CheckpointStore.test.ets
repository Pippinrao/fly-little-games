import { describe, it, expect } from '@ohos/hypium';
import { checkpointKey } from '../../../main/ets/service/CheckpointStore';

export default function checkpointStoreTest() {
  describe('CheckpointStore', () => {
    it('keeps_per_rom_keys_isolated_and_path_safe', 0, () => {
      expect(checkpointKey('builtin')).assertEqual('builtin');
      expect(checkpointKey('0123456789abcdef')).assertEqual('0123456789abcdef');
      const first = checkpointKey('folder/a.nes');
      const second = checkpointKey('folder/b.nes');
      expect(first === second).assertFalse();
      expect(first.indexOf('/') < 0).assertTrue();
      expect(second.indexOf('/') < 0).assertTrue();
    });
  });
}
