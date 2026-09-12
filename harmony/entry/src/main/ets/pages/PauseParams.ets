/** Parse RunGame router params for pause drawer title. */
export function parsePauseTitle(params: Record<string, Object> | undefined, fallback: string): string {
  if (params === undefined) {
    return fallback;
  }
  const raw = params['title'];
  if (typeof raw === 'string') {
    const title = (raw as string).trim();
    if (title.length > 0) {
      return title;
    }
  }
  return fallback;
}
