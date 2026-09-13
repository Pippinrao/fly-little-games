/** One real probe on a native worker; fixed 20s network deadline. */
export const runClient: (bind: string, peer: string, pinHex: string) => Promise<string>;
