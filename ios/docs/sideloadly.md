# Sideloadly refresh (Windows)

This is the Windows path for installing the **unsigned** `FlyNES-unsigned.ipa`
produced by `.github/workflows/ios-product.yml`. It is not a substitute for the
Stage-1 compile/link smoke in `ios-stage1.yml`.

## What CI gives you

GitHub Actions on macOS 26 / Xcode 26.6 archives `com.flynes.app` with
`CODE_SIGNING_ALLOWED=NO` and zips `Payload/FlyNES.app` as
`FlyNES-unsigned.ipa`, plus a SHA-256. The archive is statically linked, has
no app extensions, and has no extra entitlements.

Windows **cannot** compile Swift/Metal or emit a Mach-O. Do not treat a local
Windows tree as an IPA or device binary.

## Credentials stay off this machine's repo

Never put an Apple ID, password, app-specific password, 2FA code, `.p12`,
`.mobileprovision`, or API key in this repository, in CI variables for this
workflow, or in a commit. Sideloadly prompts for those on the Windows PC that
performs the refresh. They remain on that PC.

## Refresh with the same Apple ID and Bundle ID

1. Enable **Developer Mode** on the iPhone (`Settings → Privacy & Security → Developer Mode`).
2. Download `FlyNES-unsigned.ipa` and confirm the SHA-256 matches the artifact.
3. On Windows, open Sideloadly.
4. Connect the iPhone with a cable. Trust the computer if asked.
5. Use the **same Apple ID** you used on the previous refresh of this app.
6. Keep the **same Bundle ID**: `com.flynes.app`. Changing it orphans saves and
   forces a new 7-day personal-team clock.
7. Point Sideloadly at `FlyNES-unsigned.ipa` and start the install. Sideloadly
   will sign the payload locally with that Apple ID. FlyNES CI does not.

## 7-day personal team

A free personal team signature expires in **7 days**. Refresh with Sideloadly
using the same Apple ID and Bundle ID before expiry, or the Home Screen icon
stops launching. This is Apple's personal-team limit, not a FlyNES setting.

If Developer Mode is off, iOS will refuse to run the refreshed build even when
Sideloadly reports success.

## What not to do

- Do not upload Apple credentials to GitHub Actions, issues, or this repo.
- Do not add extra entitlements or app extensions to make Sideloadly happier.
- Do not reuse the Stage-1 bundle id `com.flynes.portability-smoke`.
- Do not claim a Windows host produced the IPA.
