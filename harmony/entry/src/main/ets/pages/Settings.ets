import { common } from '@kit.AbilityKit';
import { router } from '@kit.ArkUI';
import nativeApp, { SettingsDto } from 'libentry.so';

const SECTION_DISPLAY: string = 'section.display';
const SECTION_CONTROLS: string = 'section.controls';
const SECTION_AUDIO: string = 'section.audio';
const SECTION_GAME_LANGUAGE: string = 'section.game_language';
const SECTION_ABOUT: string = 'section.about';
const VIDEO_EXTREME: number = 3;

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
    Row() {
      List() {
        ForEach(this.sectionKeys(), (key: string) => {
          ListItem() {
            Text(this.sectionTitle(key))
              .fontSize(16)
              .fontColor('#F2F4F7')
              .width('100%')
              .padding(14)
              .backgroundColor(this.section === key ? '#2A303C' : '#1B1F28')
              .borderRadius(8)
          }
          .onClick(() => {
            this.section = key;
          })
        }, (key: string) => key)
      }
      .width(240)
      .height('100%')
      .divider({ strokeWidth: 0 })
      .padding(12)

      Scroll() {
        Column({ space: 12 }) {
          Text(this.sectionTitle(this.section))
            .fontSize(22)
            .fontWeight(FontWeight.Bold)
            .fontColor('#F2F4F7')
            .width('100%')
          if (this.status.length > 0) {
            Text(this.status)
              .fontSize(14)
              .fontColor('#FF6B5E')
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
        .padding(20)
        .alignItems(HorizontalAlign.Start)
      }
      .layoutWeight(1)
      .height('100%')
    }
    .width('100%')
    .height('100%')
    .backgroundColor('#12141A')
  }

  @Builder
  displaySection() {
    Text($r('app.string.settings_aspect'))
      .fontSize(14)
      .fontColor('#A8B0BD')
    this.choiceRow($r('app.string.settings_aspect_4_3'), this.snapshot.aspectMode === 1, () => {
      this.commitAspect(1);
    })
    this.choiceRow($r('app.string.settings_aspect_square'), this.snapshot.aspectMode === 2, () => {
      this.commitAspect(2);
    })
    this.choiceRow($r('app.string.settings_aspect_integer'), this.snapshot.aspectMode === 3, () => {
      this.commitAspect(3);
    })
    Text($r('app.string.settings_video_quality'))
      .fontSize(14)
      .fontColor('#A8B0BD')
      .margin({ top: 8 })
    this.choiceRow($r('app.string.settings_video_power_saver'), this.snapshot.videoQualityPreset === 1,
      () => {
        this.commitVideoQuality(1);
      })
    this.choiceRow($r('app.string.settings_video_balanced'), this.snapshot.videoQualityPreset === 2, () => {
      this.commitVideoQuality(2);
    })
    Text($r('app.string.settings_video_extreme_locked'))
      .fontSize(16)
      .fontColor('#6A7080')
      .width('100%')
      .padding(12)
      .backgroundColor('#161A22')
      .borderRadius(8)
    this.choiceRow($r('app.string.settings_video_custom'), this.snapshot.videoQualityPreset === 4, () => {
      this.commitVideoQuality(4);
    })
    Row() {
      Text($r('app.string.settings_adaptive_protection'))
        .fontSize(16)
        .fontColor('#F2F4F7')
        .layoutWeight(1)
      Toggle({ type: ToggleType.Switch, isOn: this.snapshot.adaptiveProtection === 1 })
        .onChange((isOn: boolean) => {
          this.commitAdaptive(isOn ? 1 : 0);
        })
    }
    .width('100%')
    .padding({ top: 8 })
  }

  @Builder
  controlsSection() {
    Text($r('app.string.direction_control'))
      .fontSize(14)
      .fontColor('#A8B0BD')
    this.choiceRow($r('app.string.direction_joystick'), this.snapshot.directionMode === 1, () => {
      this.commitDirection(1);
    })
    this.choiceRow($r('app.string.direction_fixed_joystick'), this.snapshot.directionMode === 2, () => {
      this.commitDirection(2);
    })
    this.choiceRow($r('app.string.direction_dpad'), this.snapshot.directionMode === 3, () => {
      this.commitDirection(3);
    })
    Button($r('app.string.control_layout_title'))
      .width('100%')
      .height(48)
      .onClick(() => {
        void router.pushUrl({ url: 'pages/ControlLayout' });
      })
    Text($r('app.string.haptic_level'))
      .fontSize(14)
      .fontColor('#A8B0BD')
      .margin({ top: 8 })
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
        .fontColor('#F2F4F7')
        .layoutWeight(1)
      Toggle({ type: ToggleType.Switch, isOn: this.snapshot.distinctAbHaptics === 1 })
        .onChange((isOn: boolean) => {
          this.commitDistinctAb(isOn ? 1 : 0);
        })
    }
    .width('100%')
    Button($r('app.string.reset_controls'))
      .width('100%')
      .height(48)
      .onClick(() => {
        this.resetControls();
      })
  }

  @Builder
  audioSection() {
    Row() {
      Text($r('app.string.audio_enabled'))
        .fontSize(16)
        .fontColor('#F2F4F7')
        .layoutWeight(1)
      Toggle({ type: ToggleType.Switch, isOn: this.snapshot.audioEnabled === 1 })
        .onChange((isOn: boolean) => {
          this.commitAudioEnabled(isOn ? 1 : 0);
        })
    }
    .width('100%')
    Text($r('app.string.audio_focus'))
      .fontSize(14)
      .fontColor('#A8B0BD')
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
      .fontSize(14)
      .fontColor('#A8B0BD')
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
        .fontColor('#F2F4F7')
        .layoutWeight(1)
      Toggle({ type: ToggleType.Switch, isOn: this.snapshot.autosaveEnabled === 1 })
        .onChange((isOn: boolean) => {
          this.commitAutosave(isOn ? 1 : 0);
        })
    }
    .width('100%')
    Button($r('app.string.manage_sources'))
      .width('100%')
      .height(48)
      .onClick(() => {
        void router.pushUrl({ url: 'pages/Sources' });
      })
  }

  @Builder
  aboutSection() {
    Text($r('app.string.about_app'))
      .fontSize(16)
      .fontColor('#F2F4F7')
      .width('100%')
    Text($r('app.string.licenses_summary'))
      .fontSize(14)
      .fontColor('#A8B0BD')
      .width('100%')
  }

  @Builder
  choiceRow(label: Resource, selected: boolean, onPick: () => void) {
    Button(label)
      .width('100%')
      .height(44)
      .fontSize(15)
      .fontColor('#F2F4F7')
      .backgroundColor(selected ? '#3A404C' : '#1B1F28')
      .onClick(onPick)
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
      const loaded = nativeApp.settingsGet();
      if (loaded.videoQualityPreset === VIDEO_EXTREME) {
        loaded.videoQualityPreset = 2;
        nativeApp.settingsApply(loaded);
      }
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

  private commitVideoQuality(value: number): void {
    if (value === VIDEO_EXTREME) {
      return;
    }
    const next = this.copySnapshot(this.snapshot);
    next.videoQualityPreset = value;
    this.commit(next);
  }

  private commitAdaptive(value: number): void {
    const next = this.copySnapshot(this.snapshot);
    next.adaptiveProtection = value;
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
