import { describe, it, expect } from '@ohos/hypium';
import { parsePauseTitle } from '../../../main/ets/pages/PauseParams';

export default function pauseParamsTest() {
  describe('PauseParams', () => {
    it('uses_title_param_when_present', 0, () => {
      const params: Record<string, Object> = { 'title': 'Thwaite' };
      expect(parsePauseTitle(params, 'Paused')).assertEqual('Thwaite');
    });

    it('falls_back_when_title_missing_or_blank', 0, () => {
      expect(parsePauseTitle(undefined, 'Paused')).assertEqual('Paused');
      const blank: Record<string, Object> = { 'title': '   ' };
      expect(parsePauseTitle(blank, 'Paused')).assertEqual('Paused');
    });
  });
}
