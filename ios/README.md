# iOS Stage-1 portability gate

This directory is a build-and-link probe, not the FlyNES product UI. It has no
Metal, audio-device, runtime/session, nearby-networking, or public bridge API.
The launchable Objective-C++ bundle calls the existing `flynes_app` C ABI and
the existing `nes_*` C ABI, then exits after writing structured smoke evidence.

The smoke embeds the repository's existing `from_below.nes` test fixture and
`LICENSE-from-below.txt`. The notice records the repository's present license
claim and provenance gap: the referenced upstream source repository does not
contain a verbatim license file. This gate therefore does not treat its own
hash check as legal clearance. Before distributing an app/IPA that contains
this ROM, archive adequate author/license evidence or replace the fixture with
one whose redistribution grant is unambiguous. CI uploads text evidence only;
it never uploads the ROM-bearing app bundle.

On macOS with Xcode 26.6 selected, the two configurations are intentionally
separate:

```sh
cmake -S ios -B build/ios-device -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT="$(xcrun --sdk iphoneos --show-sdk-path)" \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0
cmake -S ios -B build/ios-simulator -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT="$(xcrun --sdk iphonesimulator --show-sdk-path)" \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0
```

Windows can build and run the portable runner through `ios/tests`; only the
fixed macOS CI job can establish iPhoneOS/iPhoneSimulator Mach-O evidence and
execute the simulator app.

The device build disables Xcode signing because it is a compile/link artifact,
not an installable IPA. CoreSimulator receives only a local ad-hoc signature so
it can install the smoke bundle; no Apple account, team, profile, entitlement,
or distribution credential is used.
