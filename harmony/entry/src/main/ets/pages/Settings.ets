import { common } from '@kit.AbilityKit';
import { router } from '@kit.ArkUI';
import { promptAction } from '@kit.ArkUI';
import { vibrator } from '@kit.SensorServiceKit';
import { BusinessError } from '@kit.BasicServicesKit';
import { i18n } from '@kit.LocalizationKit';
import nativeApp, { SettingsDto } from 'libentry.so';
import { hapticPreviewGapMs, runtimeLanguageFor } from './SettingsHelpers';

const SECTION_DISPLAY: string = 'section.display';
const SECTION_CONTROLS: string = 'section.controls';
const SECTION_AUDIO: string = 'section.audio';
const SECTION_GAME_LANGUAGE: string = 'section.game_language';
const SECTION_ABOUT: string = 'section.about';
const VIDEO_EXTREME: number = 3;
// Android parity: material theme colors (values/colors.xml).
const COLOR_BACKGROUND: string = '#121316';
const COLOR_SURFACE: string = '#1B1D22';
const COLOR_SURFACE_VARIANT: string = '#292B31';
const COLOR_ON_SURFACE: string = '#F4EFE6';
const COLOR_ON_SURFACE_MUTED: string = '#BEB8AE';
const COLOR_PRIMARY: string = '#FF6B5E';
const COLOR_ON_PRIMARY: string = '#121316';
const COLOR_OUTLINE_SOFT: string = '#24F4EFE6';

@Entry
@Component
struct Settings {
  @State private section: string = SECTION_DISPLAY;
  @State private snapshot: SettingsDto = Settings.defaultSnapshot();
  @State private status: string = '';

  aboutToAppear(): void {
    this.reload();
  }

  onPageShow(): void {
    this.reload();
  }

  build() {
    Column() {
      // Android parity: MaterialToolbar surface bar with back navigation.
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
        Text($r('app.string.settings'))
          .fontSize(20)
          .fontWeight(FontWeight.Bold)
          .fontColor(COLOR_ON_SURFACE)
          .layoutWeight(1)
      }
      .width('100%')
      .height(56)
      .padding({ left: 4, right: 16 })
      .backgroundColor(COLOR_SURFACE)

      Row() {
        // Android parity: 240dp master column on fly_surface.
        List() {
          ForEach(this.sectionKeys(), (key: string) => {
            ListItem() {
              Text(this.sectionTitle(key))
                .fontSize(16)
                .fontWeight(this.section === key ? FontWeight.Bold : FontWeight.Medium)
                .fontColor(this.section === key ? COLOR_PRIMARY : COLOR_ON_SURFACE)
                .maxLines(1)
                .width('100%')
                .height(48)
                .padding({ left: 16, right: 16 })
                .textAlign(TextAlign.Start)
                .backgroundColor(this.section === key ? COLOR_SURFACE_VARIANT : COLOR_SURFACE)
            }
            .onClick(() => {
              this.section = key;
            })
          }, (key: string) => key)
        }
        .width(240)
        .height('100%')
        .scrollBar(BarState.Off)
        .divider({ strokeWidth: 0 })
        .backgroundColor(COLOR_SURFACE)

        Scroll() {
          Column({ space: 12 }) {
            if (this.status.length > 0) {
              Text(this.status)
                .fontSize(16)
                .fontColor('#FF6B6B')
                .width('100%')
            }
            if (this.section === SECTION_DISPLAY) {
              this.displaySection()
            } else if (this.section === SECTION_CONTROLS) {
              this.controlsSection()
            } else if (this.section === SECTION_AUDIO) {
              this.audioSection()
            } else if (this.section === SECTION_GAME_LANGUAGE) {
              this.gameLanguageSection()
            } else {
              this.aboutSection()
            }
          }
          .width('100%')
          .padding(16)
          .alignItems(HorizontalAlign.Start)
        }
        .layoutWeight(1)
        .height('100%')
        .backgroundColor(COLOR_BACKGROUND)
      }
      .layoutWeight(1)
      .alignItems(VerticalAlign.Top)
      .padding(8)
    }
    .width('100%')
    .height('100%')
    .backgroundColor(COLOR_BACKGROUND)
  }

  @Builder
  displaySection() {
    Text($r('app.string.display_quality_title'))
      .fontSize(24)
      .fontWeight(FontWeight.Bold)
      .fontColor(COLOR_ON_SURFACE)
      .width('100%')
    Text($r('app.string.display_quality_subtitle'))
      .fontSize(14)
      .fontColor(COLOR_ON_SURFACE_MUTED)
      .width('100%')
    // Android parity: preset radio cards in a 2x2 grid.
    Row({ space: 8 }) {
      this.presetCard($r('app.string.settings_video_power_saver'),
        $r('app.string.settings_video_power_summary'), 0)
      this.presetCard($r('app.string.settings_video_balanced'),
        $r('app.string.settings_video_balanced_summary'), 1)
    }
    .width('100%')
    .alignItems(VerticalAlign.Top)
    Row({ space: 8 }) {
      this.presetCard($r('app.string.settings_video_extreme_locked'),
        $r('app.string.settings_video_extreme_summary'), 2)
      this.presetCard($r('app.string.settings_video_custom'),
        $r('app.string.settings_video_custom_summary'), 3)
    }
    .width('100%')
    .alignItems(VerticalAlign.Top)
    if (this.snapshot.videoQualityPreset === 4) {
      Column({ space: 8 }) {
        Text($r('app.string.video_custom_axes'))
          .fontSize(18)
          .fontWeight(FontWeight.Bold)
          .fontColor(COLOR_ON_SURFACE)
          .width('100%')
        Text($r('app.string.video_custom_refresh'))
          .fontSize(14)
          .fontColor(COLOR_ON_SURFACE_MUTED)
          .width('100%')
        this.choiceRow($r('app.string.video_refresh_follow'),
          this.snapshot.customRefreshPolicy === 1, () => this.commitCustomRefresh(1))
        this.choiceRow($r('app.string.video_refresh_60'),
          this.snapshot.customRefreshPolicy === 3, () => this.commitCustomRefresh(3))
        this.choiceRow($r('app.string.video_refresh_90'),
          this.snapshot.customRefreshPolicy === 4, () => this.commitCustomRefresh(4))
        this.choiceRow($r('app.string.video_refresh_120'),
          this.snapshot.customRefreshPolicy === 5, () => this.commitCustomRefresh(5))
        Text($r('app.string.video_custom_temporal'))
          .fontSize(14)
          .fontColor(COLOR_ON_SURFACE_MUTED)
          .width('100%')
        this.choiceRow($r('app.string.video_temporal_native'),
          this.snapshot.customTemporalMode === 1, () => this.commitCustomTemporal(1))
        this.choiceRow($r('app.string.video_temporal_motion'),
          this.snapshot.customTemporalMode === 2, () => this.commitCustomTemporal(2))
        Text($r('app.string.video_custom_spatial'))
          .fontSize(14)
          .fontColor(COLOR_ON_SURFACE_MUTED)
          .width('100%')
        this.choiceRow($r('app.string.video_spatial_nearest'),
          this.snapshot.customSpatialMode === 1, () => this.commitCustomSpatial(1))
        this.choiceRow($r('app.string.video_spatial_sharp'),
          this.snapshot.customSpatialMode === 2, () => this.commitCustomSpatial(2))
        this.choiceRow($r('app.string.video_spatial_mmpx'),
          this.snapshot.customSpatialMode === 3, () => this.commitCustomSpatial(3))
        this.choiceRow($r('app.string.video_spatial_scalefx'),
          this.snapshot.customSpatialMode === 4, () => this.commitCustomSpatial(4))
        Text($r('app.string.video_custom_post'))
          .fontSize(14)
          .fontColor(COLOR_ON_SURFACE_MUTED)
          .width('100%')
        this.choiceRow($r('app.string.video_post_none'),
          this.snapshot.customPostEffect === 1, () => this.commitCustomPost(1))
        this.choiceRow($r('app.string.video_post_crt'),
          this.snapshot.customPostEffect === 2, () => this.commitCustomPost(2))
      }
      .width('100%')
      .padding(16)
      .backgroundColor(COLOR_SURFACE_VARIANT)
      .borderRadius(16)
    }
    Row() {
      Text($r('app.string.settings_adaptive_protection'))
        .fontSize(16)
        .fontColor(COLOR_ON_SURFACE)
        .layoutWeight(1)
      Toggle({ type: ToggleType.Switch, isOn: this.snapshot.adaptiveProtection === 1 })
        .selectedColor(COLOR_PRIMARY)
        .onChange((isOn: boolean) => {
          this.commitAdaptive(isOn ? 1 : 0);
        })
    }
    .width('100%')
    .height(48)
    Text($r('app.string.aspect_mode'))
      .fontSize(16)
      .fontColor(COLOR_ON_SURFACE_MUTED)
      .width('100%')
      .margin({ top: 4 })
    this.choiceRow($r('app.string.settings_aspect_4_3'), this.snapshot.aspectMode === 1, () => {
      this.commitAspect(1);
    })
    this.choiceRow($r('app.string.settings_aspect_square'), this.snapshot.aspectMode === 2, () => {
      this.commitAspect(2);
    })
    this.choiceRow($r('app.string.settings_aspect_integer'), this.snapshot.aspectMode === 3, () => {
      this.commitAspect(3);
    })
  }

  @Builder
  presetCard(label: Resource, summary: Resource, index: number) {
    Column({ space: 4 }) {
      Row({ space: 8 }) {
        Circle()
          .width(18)
          .height(18)
          .fill(index === this.presetIndex() ? COLOR_PRIMARY : Color.Transparent)
          .stroke(this.presetLocked(index) ? COLOR_ON_SURFACE_MUTED : COLOR_PRIMARY)
          .strokeWidth(index === this.presetIndex() || this.presetLocked(index) ? 2 : 0)
          .visibility(index === this.presetIndex() ? Visibility.Visible : Visibility.None)
        Text(label)
          .fontSize(16)
          .fontWeight(FontWeight.Bold)
          .fontColor(this.presetLocked(index) ? COLOR_ON_SURFACE_MUTED : COLOR_ON_SURFACE)
          .layoutWeight(1)
          .maxLines(1)
      }
      .width('100%')
      Text(summary)
        .fontSize(13)
        .fontColor(COLOR_ON_SURFACE_MUTED)
        .width('100%')
    }
    .padding(12)
    .backgroundColor(COLOR_SURFACE_VARIANT)
    .borderRadius(16)
    .border({
      width: this.presetIndex() === index ? 2 : 1,
      color: this.presetIndex() === index ? COLOR_PRIMARY : COLOR_OUTLINE_SOFT
    })
    .layoutWeight(1)
    .constraintSize({ minHeight: 88 })
    .onClick(() => {
      this.selectPreset(index);
    })
  }

  @Builder
  controlsSection() {
    Text($r('app.string.direction_control'))
      .fontSize(16)
      .fontColor(COLOR_ON_SURFACE_MUTED)
      .width('100%')
    this.choiceRow($r('app.string.direction_joystick'), this.snapshot.directionMode === 1, () => {
      this.commitDirection(1);
    })
    this.choiceRow($r('app.string.direction_fixed_joystick'), this.snapshot.directionMode === 2, () => {
      this.commitDirection(2);
    })
    this.choiceRow($r('app.string.direction_dpad'), this.snapshot.directionMode === 3, () => {
      this.commitDirection(3);
    })
    Text($r('app.string.haptic_level'))
      .fontSize(16)
      .fontColor(COLOR_ON_SURFACE_MUTED)
      .width('100%')
      .margin({ top: 4 })
    this.choiceRow($r('app.string.haptic_off'), this.snapshot.hapticLevel === 1, () => {
      this.commitHaptic(1);
    })
    this.choiceRow($r('app.string.haptic_light'), this.snapshot.hapticLevel === 2, () => {
      this.commitHaptic(2);
    })
    this.choiceRow($r('app.string.haptic_standard'), this.snapshot.hapticLevel === 3, () => {
      this.commitHaptic(3);
    })
    this.choiceRow($r('app.string.haptic_strong'), this.snapshot.hapticLevel === 4, () => {
      this.commitHaptic(4);
    })
    Row() {
      Text($r('app.string.distinct_ab_haptics'))
        .fontSize(16)
        .fontColor(COLOR_ON_SURFACE)
        .layoutWeight(1)
      Toggle({ type: ToggleType.Switch, isOn: this.snapshot.distinctAbHaptics === 1 })
        .selectedColor(COLOR_PRIMARY)
        .onChange((isOn: boolean) => {
          this.commitDistinctAb(isOn ? 1 : 0);
        })
    }
    .width('100%')
    .height(48)
    this.actionRow($r('app.string.haptic_preview'), $r('app.string.haptic_preview_summary'), () => {
      void this.previewHaptics();
    })
    this.actionRow($r('app.string.control_layout_title'), $r('app.string.control_layout_summary'), () => {
      void router.pushUrl({ url: 'pages/ControlLayout' });
    })
    this.actionRow($r('app.string.reset_controls'), $r('app.string.reset_controls_summary'), () => {
      this.resetControls();
    })
  }

  @Builder
  audioSection() {
    Row() {
      Text($r('app.string.audio_enabled'))
        .fontSize(16)
        .fontColor(COLOR_ON_SURFACE)
        .layoutWeight(1)
      Toggle({ type: ToggleType.Switch, isOn: this.snapshot.audioEnabled === 1 })
        .selectedColor(COLOR_PRIMARY)
        .onChange((isOn: boolean) => {
          this.commitAudioEnabled(isOn ? 1 : 0);
        })
    }
    .width('100%')
    .height(48)
    Text($r('app.string.audio_focus'))
      .fontSize(16)
      .fontColor(COLOR_ON_SURFACE_MUTED)
      .width('100%')
    this.choiceRow($r('app.string.audio_focus_pause'), this.snapshot.audioFocusPolicy === 1, () => {
      this.commitAudioFocus(1);
    })
    this.choiceRow($r('app.string.audio_focus_duck'), this.snapshot.audioFocusPolicy === 2, () => {
      this.commitAudioFocus(2);
    })
    this.choiceRow($r('app.string.audio_focus_ignore'), this.snapshot.audioFocusPolicy === 3, () => {
      this.commitAudioFocus(3);
    })
  }

  @Builder
  gameLanguageSection() {
    Text($r('app.string.app_language'))
      .fontSize(16)
      .fontColor(COLOR_ON_SURFACE_MUTED)
    this.choiceRow($r('app.string.language_system'), this.snapshot.localeTag === 'system', () => {
      this.commitLocale('system');
    })
    this.choiceRow($r('app.string.language_en'), this.snapshot.localeTag === 'en', () => {
      this.commitLocale('en');
    })
    this.choiceRow($r('app.string.language_zh'), this.snapshot.localeTag === 'zh-Hans', () => {
      this.commitLocale('zh-Hans');
    })
    Row() {
      Text($r('app.string.autosave'))
        .fontSize(16)
        .fontColor(COLOR_ON_SURFACE)
        .layoutWeight(1)
      Toggle({ type: ToggleType.Switch, isOn: this.snapshot.autosaveEnabled === 1 })
        .selectedColor(COLOR_PRIMARY)
        .onChange((isOn: boolean) => {
          this.commitAutosave(isOn ? 1 : 0);
        })
    }
    .width('100%')
    .height(48)
    this.actionRow($r('app.string.manage_sources'), '', () => {
      void router.pushUrl({ url: 'pages/Sources' });
    })
  }

  @Builder
  aboutSection() {
    this.actionRow($r('app.string.license_information'), $r('app.string.licenses_summary'), () => {
      void router.pushUrl({ url: 'pages/Licenses' });
    })
    // D2: 好友管理 is a row inside this existing Settings page that opens the
    // dedicated page. Do not add a sixth settings root for it.
    this.actionRow($r('app.string.nearby_friends_manage'), $r('app.string.nearby_blocked_friend_store'), () => {
      void router.pushUrl({ url: 'pages/NearbyFriendsManage' });
    })
    Text($r('app.string.project_source'))
      .fontSize(16)
      .fontColor(COLOR_ON_SURFACE)
      .width('100%')
    Text($r('app.string.project_source_unconfigured'))
      .fontSize(14)
      .fontColor(COLOR_ON_SURFACE_MUTED)
      .width('100%')
  }

  // Android parity: ListPreference-style entry; selected entry uses fly_primary.
  @Builder
  choiceRow(label: Resource, selected: boolean, onPick: () => void) {
    Row({ space: 8 }) {
      Circle()
        .width(18)
        .height(18)
        .fill(selected ? COLOR_PRIMARY : Color.Transparent)
        .stroke(selected ? COLOR_PRIMARY : COLOR_ON_SURFACE_MUTED)
        .strokeWidth(2)
      Text(label)
        .fontSize(16)
        .fontColor(COLOR_ON_SURFACE)
        .layoutWeight(1)
    }
    .width('100%')
    .height(48)
    .padding({ left: 16, right: 16 })
    .backgroundColor(COLOR_SURFACE_VARIANT)
    .borderRadius(12)
    .onClick(onPick)
  }

  // Android parity: Preference entry with title+summary.
  @Builder
  actionRow(label: ResourceStr, summary: ResourceStr, onPick: () => void) {
    Column({ space: 2 }) {
      Text(label)
        .fontSize(16)
        .fontColor(COLOR_ON_SURFACE)
        .width('100%')
      if (typeof summary === 'string' ? summary.length > 0 : true) {
        Text(summary)
          .fontSize(13)
          .fontColor(COLOR_ON_SURFACE_MUTED)
          .width('100%')
      }
    }
    .width('100%')
    .padding({ left: 16, right: 16, top: 10, bottom: 10 })
    .backgroundColor(COLOR_SURFACE_VARIANT)
    .borderRadius(12)
    .onClick(onPick)
  }

  private presetIndex(): number {
    const preset: number = this.snapshot.videoQualityPreset;
    if (preset === 1) return 0;
    if (preset === 2) return 1;
    if (preset === VIDEO_EXTREME) return 2;
    return 3;
  }

  private presetLocked(index: number): boolean {
    return index === 2;
  }

  private selectPreset(index: number): void {
    // Android parity: Extreme is locked and explains itself in a dialog.
    if (index === 2) {
      this.showExtremeLocked();
      return;
    }
    const preset = index === 0 ? 1 : index === 1 ? 2 : 4;
    const next = this.copySnapshot(this.snapshot);
    next.videoQualityPreset = preset;
    this.commit(next);
  }

  private showExtremeLocked(): void {
    promptAction.showDialog({
      title: $r('app.string.video_extreme_locked_title'),
      message: $r('app.string.video_extreme_locked_message'),
      buttons: [{ text: $r('app.string.ok'), color: COLOR_PRIMARY }]
    }).catch(() => {});
  }

  private sectionKeys(): string[] {
    return [SECTION_DISPLAY, SECTION_CONTROLS, SECTION_AUDIO, SECTION_GAME_LANGUAGE, SECTION_ABOUT];
  }

  private sectionTitle(key: string): Resource {
    if (key === SECTION_DISPLAY) {
      return $r('app.string.settings_display');
    }
    if (key === SECTION_CONTROLS) {
      return $r('app.string.settings_controls');
    }
    if (key === SECTION_AUDIO) {
      return $r('app.string.settings_audio');
    }
    if (key === SECTION_GAME_LANGUAGE) {
      return $r('app.string.settings_game_language');
    }
    return $r('app.string.settings_about');
  }

  private reload(): void {
    const host = this.getUIContext().getHostContext();
    if (host === undefined) {
      this.status = 'UIAbility context is unavailable';
      return;
    }
    const context = host as common.UIAbilityContext;
    try {
      nativeApp.appOpen(context.filesDir, context.cacheDir);
      this.snapshot = nativeApp.settingsGet();
      this.status = '';
    } catch (error) {
      this.status = error instanceof Error ? error.message : 'settings load failed';
    }
  }

  private commitAspect(value: number): void {
    const next = this.copySnapshot(this.snapshot);
    next.aspectMode = value;
    this.commit(next);
  }

  private commitAdaptive(value: number): void {
    const next = this.copySnapshot(this.snapshot);
    next.adaptiveProtection = value;
    this.commit(next);
  }

  private commitCustomRefresh(value: number): void {
    const next = this.copySnapshot(this.snapshot);
    next.customRefreshPolicy = value;
    this.commit(next);
  }

  private commitCustomTemporal(value: number): void {
    const next = this.copySnapshot(this.snapshot);
    next.customTemporalMode = value;
    this.commit(next);
  }

  private commitCustomSpatial(value: number): void {
    const next = this.copySnapshot(this.snapshot);
    next.customSpatialMode = value;
    this.commit(next);
  }

  private commitCustomPost(value: number): void {
    const next = this.copySnapshot(this.snapshot);
    next.customPostEffect = value;
    this.commit(next);
  }

  private commitDirection(value: number): void {
    const next = this.copySnapshot(this.snapshot);
    next.directionMode = value;
    this.commit(next);
  }

  private commitHaptic(value: number): void {
    const next = this.copySnapshot(this.snapshot);
    next.hapticLevel = value;
    this.commit(next);
  }

  private commitDistinctAb(value: number): void {
    const next = this.copySnapshot(this.snapshot);
    next.distinctAbHaptics = value;
    this.commit(next);
  }

  private async previewHaptics(): Promise<void> {
    if (this.snapshot.hapticLevel === 1) {
      return;
    }
    const intensity: number = [0, 0, 40, 70, 100][this.snapshot.hapticLevel] ?? 40;
    this.pulseHaptic(1, intensity);
    await new Promise<void>((resolve) => {
      setTimeout(() => {
        resolve();
      }, hapticPreviewGapMs());
    });
    this.pulseHaptic(2, intensity);
  }

  private pulseHaptic(count: number, intensity: number): void {
    const effect: vibrator.VibratePreset = {
      type: 'preset',
      effectId: 'haptic.clock.timer',
      count: count,
      intensity: intensity
    };
    try {
      vibrator.startVibration(effect, { usage: 'touch' }, (error: BusinessError) => {
        if (error && error.code !== 0) {
          console.error(`FlyNES haptic preview failed: ${error.code}`);
        }
      });
    } catch (error) {
      const detail = error instanceof Error ? error.message : 'haptic preview failed';
      console.error(`FlyNES haptic preview failed: ${detail}`);
    }
  }

  private commitAudioEnabled(value: number): void {
    const next = this.copySnapshot(this.snapshot);
    next.audioEnabled = value;
    this.commit(next);
  }

  private commitAudioFocus(value: number): void {
    const next = this.copySnapshot(this.snapshot);
    next.audioFocusPolicy = value;
    this.commit(next);
  }

  private commitLocale(value: string): void {
    const next = this.copySnapshot(this.snapshot);
    next.localeTag = value;
    this.commit(next);
    if (this.status.length > 0) {
      return;
    }
    try {
      i18n.System.setAppPreferredLanguage(value === 'system' ? 'default' : value);
      const host = this.getUIContext().getHostContext();
      if (host === undefined) {
        throw new Error('UIAbility context is unavailable');
      }
      const context = host as common.UIAbilityContext;
      context.getApplicationContext().setLanguage(runtimeLanguageFor(
        value, i18n.System.getSystemLanguage()));
    } catch (error) {
      this.status = error instanceof Error ? error.message : 'language apply failed';
    }
  }

  private commitAutosave(value: number): void {
    const next = this.copySnapshot(this.snapshot);
    next.autosaveEnabled = value;
    this.commit(next);
  }

  private commit(next: SettingsDto): void {
    try {
      this.snapshot = nativeApp.settingsApply(next);
      this.status = '';
    } catch (error) {
      this.status = error instanceof Error ? error.message : 'settings apply failed';
    }
  }

  private resetControls(): void {
    const defaults = Settings.defaultSnapshot();
    const next = this.copySnapshot(this.snapshot);
    next.directionMode = defaults.directionMode;
    next.hapticLevel = defaults.hapticLevel;
    next.distinctAbHaptics = defaults.distinctAbHaptics;
    this.commit(next);
    try {
      nativeApp.controlLayoutApply(nativeApp.controlLayoutRecommended());
    } catch (error) {
      this.status = error instanceof Error ? error.message : 'layout reset failed';
    }
  }

  private copySnapshot(value: SettingsDto): SettingsDto {
    return {
      aspectMode: value.aspectMode,
      videoQualityPreset: value.videoQualityPreset,
      customRefreshPolicy: value.customRefreshPolicy,
      customTemporalMode: value.customTemporalMode,
      customSpatialMode: value.customSpatialMode,
      customPostEffect: value.customPostEffect,
      adaptiveProtection: value.adaptiveProtection,
      layoutPreset: value.layoutPreset,
      directionMode: value.directionMode,
      buttonScale: value.buttonScale,
      verticalOffset: value.verticalOffset,
      controlOpacity: value.controlOpacity,
      joystickScale: value.joystickScale,
      deadZone: value.deadZone,
      hapticLevel: value.hapticLevel,
      distinctAbHaptics: value.distinctAbHaptics,
      audioEnabled: value.audioEnabled,
      audioFocusPolicy: value.audioFocusPolicy,
      autosaveEnabled: value.autosaveEnabled,
      localeTag: value.localeTag,
      lastPlayedId: value.lastPlayedId
    };
  }

  private static defaultSnapshot(): SettingsDto {
    return {
      aspectMode: 1,
      videoQualityPreset: 2,
      customRefreshPolicy: 3,
      customTemporalMode: 1,
      customSpatialMode: 2,
      customPostEffect: 1,
      adaptiveProtection: 1,
      layoutPreset: 1,
      directionMode: 2,
      buttonScale: 1,
      verticalOffset: 0,
      controlOpacity: 0.78,
      joystickScale: 1,
      deadZone: 0.18,
      hapticLevel: 2,
      distinctAbHaptics: 1,
      audioEnabled: 1,
      audioFocusPolicy: 1,
      autosaveEnabled: 1,
      localeTag: 'system',
      lastPlayedId: ''
    };
  }
}
