/** Explicit debug-only HTTP endpoint reached through an hdc reverse port. */
export function debugSideloadUrls(): string[] {
  return ['http://127.0.0.1:18080'];
}

export function shouldDownloadSideloadFile(destinationExists: boolean): boolean {
  return !destinationExists;
}

/** Identifies only disposable files created by the emulator TDD sideload fixture. */
export function isDebugSideloadFixture(name: string): boolean {
  if (name === 'slow-2048-roms.zip') {
    return true;
  }
  return name.startsWith('flynes-tdd-slow-') && name.endsWith('.zip');
}
