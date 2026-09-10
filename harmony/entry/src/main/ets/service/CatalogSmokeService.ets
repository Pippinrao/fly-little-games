import { common } from '@kit.AbilityKit';
import nativeCatalog from 'libentry.so';

export interface CatalogSmokeViewModel {
  status: string;
  generation: string;
  count: string;
  detail: string;
}

export function runCatalogSmoke(context: common.UIAbilityContext): CatalogSmokeViewModel {
  try {
    const result = nativeCatalog.catalogSmoke(context.filesDir, context.cacheDir);
    return {
      status: 'PASS',
      generation: result.generation,
      count: result.count,
      detail: 'create → snapshot → destroy → query → release'
    };
  } catch (error) {
    const detail = error instanceof Error ? error.message : 'Native catalog smoke failed';
    return {
      status: 'FAIL',
      generation: '-',
      count: '-',
      detail: detail
    };
  }
}
