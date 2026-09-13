import probe from 'libnearbyprobe.so';
import { hilog } from '@kit.PerformanceAnalysisKit';
import { probeLaunchGate } from '../ProbeLaunchGate';

@Entry
@Component
struct Index {
  @StorageLink('probeBind') bind: string = '127.0.0.1:0';
  @StorageLink('probePeer') peer: string = '';
  @StorageLink('probePin') pin: string = '';
  @StorageLink('probeRequest') @Watch('acceptRequest') request: number = 0;
  @StorageLink('probeBusy') busy: boolean = false;
  @StorageLink('probeResult') result: string = 'Ready. Supply numeric LAN addresses and the listener full DER-SPKI hex.';
  @StorageLink('probeRequestError') requestError: string = '';

  aboutToAppear(): void { this.acceptRequest(); }
  private acceptRequest(): void {
    if (probeLaunchGate.consume(this.request)) { this.runClient(); }
  }
  private start(): void {
    if (probeLaunchGate.startManual()) { this.requestError = ''; this.runClient(); }
  }
  private async runClient(): Promise<void> {
    this.busy = true;
    this.result = 'Running (20-second network deadline)…';
    try {
      this.result = await probe.runClient(this.bind, this.peer, this.pin);
    } catch (error) {
      this.result = error instanceof Error ? error.message : 'ERROR native probe failed';
    } finally {
      probeLaunchGate.finish();
      this.busy = false;
      hilog.info(0x0000, 'NearbyQuicProbe', '%{public}s', this.result);
    }
  }
  build() {
    Scroll() {
      Column({ space: 14 }) {
        Text('Nearby QUIC transport experiment').fontSize(24).fontWeight(FontWeight.Bold)
        Text('Isolated foreground probe. Not a game lobby.').fontSize(14)
        if (this.requestError.length > 0) {
          Text(this.requestError).fontSize(14).fontColor('#A32020').textSelectable(TextSelectableMode.SELECTABLE_FOCUSABLE)
        }
        TextInput({ text: this.bind, placeholder: 'Bind IP:port (local LAN IP:0)' })
          .onChange((value: string) => { this.bind = value; }).maxLength(128).enabled(!this.busy)
        TextInput({ text: this.peer, placeholder: 'Peer IP:port' })
          .onChange((value: string) => { this.peer = value; }).maxLength(128).enabled(!this.busy)
        TextArea({ text: this.pin, placeholder: 'Full DER-SPKI: 182 hexadecimal digits' })
          .onChange((value: string) => { this.pin = value; }).maxLength(182).height(140).enabled(!this.busy)
        Button(this.busy ? 'Running…' : 'Start probe').onClick(() => { this.start(); }).enabled(!this.busy)
        Text(this.result).fontSize(14).fontFamily('monospace').textSelectable(TextSelectableMode.SELECTABLE_FOCUSABLE).width('100%')
      }.width('100%').padding(24)
    }.width('100%').height('100%').backgroundColor('#F4F7FA')
  }
}
