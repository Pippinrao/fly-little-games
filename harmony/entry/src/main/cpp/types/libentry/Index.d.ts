export interface CatalogSmokeResult {
  generation: string;
  count: string;
}

export const catalogSmoke: (dataRoot: string, cacheRoot: string) => CatalogSmokeResult;
