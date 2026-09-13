import { router } from '@kit.ArkUI';

// Android/game-center parity: existing shell colors from pages/GameCenter.ets and
// resources/base/element/color.json. No new palette is introduced here.
const COLOR_SURFACE: string = '#1B1F28';
const COLOR_ON_SURFACE: string = '#F2F4F7';
const COLOR_ON_SURFACE_MUTED: string = '#A8B0BD';
const COLOR_NOT_REACHED: string = '#3A404C';
const COLOR_BLOCKED: string = '#FF6B5E';

const TAB_FRIENDS: string = 'friends';
const TAB_DEVICES: string = 'devices';

// Design §22.2 pairing pipeline. Display order is pipeline order (decision D5).
const STAGE_PERMISSION: string = 'permission';
const STAGE_DISCOVERY: string = 'discovery';
const STAGE_AUTH: string = 'auth';
const STAGE_WIFI: string = 'wifi';
const STAGE_QUIC: string = 'quic';
const STAGE_VERSION: string = 'version';
const STAGE_CODEC: string = 'codec';

const STAGE_ORDER: string[] = [
  STAGE_PERMISSION,
  STAGE_DISCOVERY,
  STAGE_AUTH,
  STAGE_WIFI,
  STAGE_QUIC,
  STAGE_VERSION,
  STAGE_CODEC
];

// Friend identities are not in the ABI yet (§3), so no saved friend can be proven
// to exist. This returns the friend-store count once that capability lands.
function savedFriendCount(): number {
  return 0;
}

// §4 initial-tab rule: open on 附近设备 while no friend is saved, and on 好友 once
// at least one friend is saved. The last-used tab is deliberately not persisted.
function initialNearbyTab(savedFriends: number): string {
  return savedFriends > 0 ? TAB_FRIENDS : TAB_DEVICES;
}

@Entry
@Component
struct NearbyFriends {
  // As long as no friend is saved (every build today, see savedFriendCount), the
  // page lands on 附近设备 — its one actionable path. Once the friend store exists
  // the same rule opens returning users on 好友. Do not hardcode this back to 好友
  // and do not persist the last-used tab.
  @State private tab: string = initialNearbyTab(savedFriendCount());

  build() {
    Column() {
      this.topBar()
      this.tabToggles()
      if (this.tab === TAB_FRIENDS) {
        this.friendsTab()
      } else {
        this.devicesTab()
      }
    }
    .width('100%')
    .height('100%')
    .backgroundColor($r('app.color.start_window_background'))
  }

  @Builder
  topBar() {
    Row() {
      Text('‹')
        .fontSize(26)
        .fontWeight(FontWeight.Bold)
        .fontColor(COLOR_ON_SURFACE)
        .width(44)
        .height(44)
        .textAlign(TextAlign.Center)
        .onClick(() => {
          router.back();
        })
      Text($r('app.string.nearby_title'))
        .fontSize(20)
        .fontWeight(FontWeight.Bold)
        .fontColor(COLOR_ON_SURFACE)
        .layoutWeight(1)
    }
    .width('100%')
    .height(56)
    .padding({ left: 4, right: 16 })
    .backgroundColor(COLOR_SURFACE)
  }

  @Builder
  tabToggles() {
    Row({ space: 8 }) {
      Toggle({ type: ToggleType.Button, isOn: this.tab === TAB_FRIENDS }) {
        Text($r('app.string.nearby_tab_friends'))
          .fontSize(16)
          .fontWeight(FontWeight.Medium)
          .fontColor(COLOR_ON_SURFACE)
      }
      .height(48)
      .padding({ left: 16, right: 16 })
      .selectedColor(COLOR_NOT_REACHED)
      .onChange((isOn: boolean) => {
        if (isOn) {
          this.tab = TAB_FRIENDS;
        }
      })
      Toggle({ type: ToggleType.Button, isOn: this.tab === TAB_DEVICES }) {
        Text($r('app.string.nearby_tab_devices'))
          .fontSize(16)
          .fontWeight(FontWeight.Medium)
          .fontColor(COLOR_ON_SURFACE)
      }
      .height(48)
      .padding({ left: 16, right: 16 })
      .selectedColor(COLOR_NOT_REACHED)
      .onChange((isOn: boolean) => {
        if (isOn) {
          this.tab = TAB_DEVICES;
        }
      })
    }
    .width('100%')
    .height(56)
    .padding({ left: 16, right: 16 })
    .alignItems(VerticalAlign.Center)
  }

  // Truthful placeholder contract: no saved-friend row is ever invented. The tab
  // shows the empty state plus the reason the list cannot exist yet.
  @Builder
  friendsTab() {
    Scroll() {
      Column({ space: 8 }) {
        Text($r('app.string.nearby_friends_empty'))
          .fontSize(18)
          .fontColor(COLOR_ON_SURFACE)
          .width('100%')
          .textAlign(TextAlign.Center)
          .margin({ top: 24 })
        Text($r('app.string.nearby_blocked_friend_store'))
          .fontSize(14)
          .fontColor(COLOR_ON_SURFACE_MUTED)
          .width('100%')
          .textAlign(TextAlign.Center)
      }
      .width('100%')
      .padding(16)
    }
    .width('100%')
    .layoutWeight(1)
  }

  @Builder
  devicesTab() {
    Scroll() {
      Column({ space: 8 }) {
        ForEach(STAGE_ORDER, (kind: string) => {
          this.stageRow(kind)
        }, (kind: string) => kind)

        // Discovery capability row: the stage name plus the reason it cannot run.
        Row({ space: 8 }) {
          Circle()
            .width(8)
            .height(8)
            .fill(COLOR_BLOCKED)
          Text($r('app.string.nearby_stage_discovery'))
            .fontSize(16)
            .fontWeight(FontWeight.Bold)
            .fontColor(COLOR_ON_SURFACE)
            .layoutWeight(1)
        }
        .width('100%')
        .height(32)
        .padding({ left: 12, right: 12 })
        .alignItems(VerticalAlign.Center)
        Text($r('app.string.nearby_stage_discovery_reason'))
          .fontSize(14)
          .fontColor(COLOR_BLOCKED)
          .width('100%')
          .padding({ left: 12, right: 12 })

        // Both discovery actions cannot act yet: present, disabled, and each with
        // its own visible reason (§4).
        Button($r('app.string.nearby_find_devices'))
          .width('100%')
          .height(48)
          .fontSize(16)
          .enabled(false)
        Text($r('app.string.nearby_blocked_discovery'))
          .fontSize(14)
          .fontColor(COLOR_ON_SURFACE_MUTED)
          .width('100%')
          .textAlign(TextAlign.Center)
        Button($r('app.string.nearby_scan_host_qr'))
          .width('100%')
          .height(48)
          .fontSize(16)
          .enabled(false)
        Text($r('app.string.nearby_blocked_discovery'))
          .fontSize(14)
          .fontColor(COLOR_ON_SURFACE_MUTED)
          .width('100%')
          .textAlign(TextAlign.Center)

        Text($r('app.string.nearby_devices_empty'))
          .fontSize(14)
          .fontColor(COLOR_ON_SURFACE_MUTED)
          .width('100%')
          .textAlign(TextAlign.Center)
          .margin({ top: 8 })

        // §4 permits exactly one kind of navigate-only entry: into 配对, because
        // that page must exist to display the blocking stage. Everything else on
        // this tab stays disabled with its own reason above. The label is the
        // destination page's own name — nearby.title names the whole feature, so
        // reusing it on a child page would be a real defect.
        Button($r('app.string.nearby_pairing_title'))
          .width('100%')
          .height(48)
          .fontSize(16)
          .onClick(() => {
            void router.pushUrl({ url: 'pages/NearbyPairing' });
          })
      }
      .width('100%')
      .padding(16)
    }
    .width('100%')
    .layoutWeight(1)
  }

  // One row per pairing stage, in pipeline order. The first failing stage is
  // marked and carries its reason; earlier stages read as passed; later stages
  // keep the neutral not-yet-reached treatment with no reason (decision D5).
  @Builder
  stageRow(kind: string) {
    Column({ space: 4 }) {
      Row({ space: 8 }) {
        Circle()
          .width(8)
          .height(8)
          .fill(this.stageMarkerColor(kind))
        Text(this.stageLabel(kind))
          .fontSize(16)
          .fontColor(this.stageTextColor(kind))
          .fontWeight(kind === this.currentStage() ? FontWeight.Bold : FontWeight.Normal)
          .layoutWeight(1)
        // The stage state must never be colour-only: the same key that names the
        // state in the accessibility tree is also rendered as visible text.
        Text(this.stageStatus(kind))
          .fontSize(12)
          .fontColor(COLOR_ON_SURFACE_MUTED)
      }
      .width('100%')
      .height(32)
      .alignItems(VerticalAlign.Center)
      if (kind === this.currentStage()) {
        // D5/D11: only the first failing stage carries a reason key. The
        // not-yet-reached stages below render neither a reason nor a blocked key,
        // so their reason keys land with the slice that reports them.
        Text($r('app.string.nearby_stage_permission_reason'))
          .fontSize(14)
          .fontColor(COLOR_BLOCKED)
          .width('100%')
      }
    }
    .width('100%')
    .padding({ left: 12, right: 12, top: 4, bottom: 4 })
    .backgroundColor(COLOR_SURFACE)
    .borderRadius(8)
    .accessibilityText(this.stageStatus(kind))
  }

  // Until the session ABI reports the first failing stage, 权限 is the truthful
  // current stage: this build declares no nearby permission and holds none (D1).
  private currentStage(): string {
    return STAGE_PERMISSION;
  }

  private stageIndex(kind: string): number {
    for (let index = 0; index < STAGE_ORDER.length; index++) {
      if (STAGE_ORDER[index] === kind) {
        return index;
      }
    }
    return STAGE_ORDER.length;
  }

  private stageLabel(kind: string): Resource {
    if (kind === STAGE_PERMISSION) {
      return $r('app.string.nearby_stage_permission');
    }
    if (kind === STAGE_DISCOVERY) {
      return $r('app.string.nearby_stage_discovery');
    }
    if (kind === STAGE_AUTH) {
      return $r('app.string.nearby_stage_auth');
    }
    if (kind === STAGE_WIFI) {
      return $r('app.string.nearby_stage_wifi');
    }
    if (kind === STAGE_QUIC) {
      return $r('app.string.nearby_stage_quic');
    }
    if (kind === STAGE_VERSION) {
      return $r('app.string.nearby_stage_version');
    }
    return $r('app.string.nearby_stage_codec');
  }

  // One announced/visible state label per stage row (color-not-only).
  private stageStatus(kind: string): Resource {
    const current = this.stageIndex(this.currentStage());
    const index = this.stageIndex(kind);
    if (index < current) {
      return $r('app.string.nearby_stage_status_passed');
    }
    if (index === current) {
      return $r('app.string.nearby_stage_status_current');
    }
    return $r('app.string.nearby_stage_status_not_reached');
  }

  private stageMarkerColor(kind: string): string {
    if (kind === this.currentStage()) {
      return COLOR_BLOCKED;
    }
    return this.stageIndex(kind) < this.stageIndex(this.currentStage()) ? COLOR_ON_SURFACE_MUTED
      : COLOR_NOT_REACHED;
  }

  private stageTextColor(kind: string): string {
    return this.stageIndex(kind) <= this.stageIndex(this.currentStage()) ? COLOR_ON_SURFACE
      : COLOR_ON_SURFACE_MUTED;
  }
}
