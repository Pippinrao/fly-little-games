/** Pure layout placement used by ControlLayoutWarnings (no native imports). */
export class ControlPlacement {
  centerX: number = 0.5;
  centerY: number = 0.5;
  scale: number = 1;

  constructor(centerX: number = 0.5, centerY: number = 0.5, scale: number = 1) {
    this.centerX = centerX;
    this.centerY = centerY;
    this.scale = scale;
  }
}

export enum ControlLayoutWarningCode {
  TOO_SMALL = 'TOO_SMALL',
  OVERLAP = 'OVERLAP',
  GESTURE_ZONE = 'GESTURE_ZONE',
  CENTRAL_PROTECTION = 'CENTRAL_PROTECTION'
}

/** Thresholds mirror Android ControlLayoutWarnings.java. */
export class ControlLayoutWarnings {
  private readonly codes: string[] = [];

  constructor(codes: string[]) {
    this.codes = codes.slice();
  }

  contains(code: ControlLayoutWarningCode): boolean {
    return this.codes.indexOf(code) >= 0;
  }

  isEmpty(): boolean {
    return this.codes.length === 0;
  }

  codeList(): string[] {
    return this.codes.slice();
  }

  static inspect(placements: ControlPlacement[]): ControlLayoutWarnings {
    const result: string[] = [];
    const add = (code: string): void => {
      if (result.indexOf(code) < 0) {
        result.push(code);
      }
    };
    for (let index = 0; index < placements.length; index++) {
      const p = placements[index];
      if (p.scale < 0.67) {
        add(ControlLayoutWarningCode.TOO_SMALL);
      }
      if (p.centerX < 0.04 || p.centerX > 0.96 || p.centerY < 0.06 || p.centerY > 0.94) {
        add(ControlLayoutWarningCode.GESTURE_ZONE);
      }
      if (p.centerX > 0.15 && p.centerX < 0.85) {
        add(ControlLayoutWarningCode.CENTRAL_PROTECTION);
      }
    }
    for (let i = 0; i < placements.length; i++) {
      for (let j = i + 1; j < placements.length; j++) {
        const a = placements[i];
        const b = placements[j];
        const dx = a.centerX - b.centerX;
        const dy = a.centerY - b.centerY;
        const radius = 0.055 * (a.scale + b.scale);
        if (dx * dx + dy * dy < radius * radius) {
          add(ControlLayoutWarningCode.OVERLAP);
        }
      }
    }
    return new ControlLayoutWarnings(result);
  }
}

/** Undo stack aligned with Android ArrayDeque limit of 20. */
export class UndoStack<T> {
  private readonly maxSize: number;
  private readonly items: T[] = [];

  constructor(maxSize: number = 20) {
    this.maxSize = maxSize;
  }

  push(snapshot: T): void {
    if (this.items.length >= this.maxSize) {
      this.items.pop();
    }
    this.items.unshift(snapshot);
  }

  undo(): T | undefined {
    if (this.items.length === 0) {
      return undefined;
    }
    return this.items.shift();
  }

  size(): number {
    return this.items.length;
  }

  clear(): void {
    this.items.splice(0, this.items.length);
  }
}
