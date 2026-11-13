# Building, testing and packaging

Needs CMake 3.26 or newer and a C++23 compiler (GCC 13, Clang 18, Xcode 16 or newer). Python 3 is optional: without it CMake warns and three of the tests (`assets_clean`, `asset_integrity`, `layout_json`) are left out, unless `-DPINBALL_REQUIRE_ALL_TESTS=ON` makes that an error. In-source builds are refused.

macOS, with the libraries from Homebrew:

```sh
brew install cmake sdl3 box2d
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
open "build/Multiball Academy.app"
```

On macOS the game is an application bundle, `Multiball Academy.app`, with its icon, its `Info.plist` and the assets inside. The executable is `build/Multiball Academy.app/Contents/MacOS/Multiball Academy`; run that path directly to see the game's messages in the terminal or to pass `--frames`. On Linux the game is the plain executable `build/pinball`.

Linux: the same `cmake` and `ctest` commands work where SDL3 3.4 or newer and Box2D 3.2 or newer are installed as CMake packages. Few distributions have both yet, so the usual way is to let the build fetch them (`-DPINBALL_FETCH_DEPS=ON`, below), which needs the headers SDL builds against. On Ubuntu 24.04, as CI does:

```sh
sudo apt-get install -y --no-install-recommends \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev \
  libxss-dev libxtst-dev libxkbcommon-dev libwayland-dev wayland-protocols libegl1-mesa-dev \
  libgl1-mesa-dev libasound2-dev libpulse-dev libudev-dev libdbus-1-dev
cmake -S . -B build -DPINBALL_FETCH_DEPS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/pinball
```

The default build type is RelWithDebInfo. Options:

- `-DPINBALL_SANITIZE=ON` builds with AddressSanitizer and UBSan.
- `-DPINBALL_WERROR=ON` treats warnings as errors. CI uses this.
- `-DPINBALL_FETCH_DEPS=ON` needs no installed SDL or Box2D: it downloads SDL3 3.4.20 (release archive, checked by SHA-256) and Box2D 3.2.0 (fixed by commit), builds them with the same protective compiler flags as the game, and links them statically. CI uses this. `tools/check_pins.sh` reports when either has a newer release; a weekly workflow runs it. FetchContent's own switches that would put other sources in their place unchecked (`FETCHCONTENT_SOURCE_DIR_SDL3`, `FETCHCONTENT_SOURCE_DIR_BOX2D`, `FETCHCONTENT_FULLY_DISCONNECTED`, `FETCHCONTENT_TRY_FIND_PACKAGE_MODE=ALWAYS`) stop the configuration.
- `-DPINBALL_ALLOW_UNVERIFIED_DEPS=ON` accepts those switches, with a warning: the dependencies are then whatever was supplied, not the pinned and verified sources.
- `-DPINBALL_HARDENED_RUNTIME=OFF` (macOS, only with `PINBALL_FETCH_DEPS` and without sanitizers) skips the ad hoc signing with the hardened runtime that a Release build otherwise gets.
- `-DPINBALL_CODESIGN_IDENTITY="Developer ID Application: NAME (TEAMID)"` (macOS, under the same conditions) signs the Release bundle with that certificate and a secure timestamp instead of ad hoc. See below.
- `-DCMAKE_OSX_DEPLOYMENT_TARGET=...` (macOS) sets the oldest macOS the build runs on. With `PINBALL_FETCH_DEPS` the default is 11.0. A build against installed libraries targets the macOS it is built on, like those libraries.
- `-DPINBALL_BUILD_TOOLS=OFF` leaves out the development tools `pinball_snapshot` and `pinball_layout`, and with them the render and layout tests.
- `-DBUILD_TESTING=OFF` leaves out the tests.
- `-DPINBALL_REQUIRE_ALL_TESTS=ON` stops the configuration if a test cannot be registered: no Python 3, or the tools left out. CI uses this, and runs `ctest --no-tests=error`.
- `-DPINBALL_FUZZ=ON` (clang with libFuzzer, not Apple clang) builds only `pinball_fuzz`, which fuzzes the score file, PNG header, asset name and argument parsers.

There are no install rules: the game runs from the build directory. On Linux its `assets` directory is next to the executable. On macOS it is inside the bundle, at `Contents/Resources/assets`, and the bundle can be moved or copied as a whole.

`--frames N` (1 to 1,000,000) makes the game exit after N frames and leave the high score file alone. The tests use it; the game takes no other arguments. With no display:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "build/Multiball Academy.app/Contents/MacOS/Multiball Academy" --frames 60   # macOS
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy build/pinball --frames 60                                                      # Linux
```

## A build to give to someone else

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DPINBALL_FETCH_DEPS=ON
cmake --build build-release --parallel
```

On macOS the result is `build-release/Multiball Academy.app`. Its executable is optimised, links only system libraries, carries no path from the build machine and runs on macOS 11.0 or newer. The whole bundle is signed, with the hardened runtime on, so the system ignores injected libraries and the signature seals the assets, the icon and the `Info.plist` together with the executable. Signing is the last step of every build; anything that changes the bundle afterwards breaks the seal until the next build. No entitlements are requested.

By default that signature is ad hoc: it names no publisher, and the bundle is not notarised, so macOS Gatekeeper will still warn about it on another machine. To sign as an identified developer, name a Developer ID Application certificate from your keychain (`security find-identity -v -p codesigning` lists them):

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DPINBALL_FETCH_DEPS=ON \
  -DPINBALL_CODESIGN_IDENTITY="Developer ID Application: NAME (TEAMID)"
cmake --build build-release --parallel
codesign --verify --deep --strict --verbose=2 "build-release/Multiball Academy.app"
```

The bundle is then signed with that identity, the hardened runtime and a secure timestamp (which needs the network at each build). To have Apple notarise it, store credentials for `notarytool` in the keychain once, under a profile name of your choice, then run the script with the bundle and that name:

```sh
xcrun notarytool store-credentials pinball-notary    # asks for the Apple ID, team and app-specific password
tools/notarize.sh "build-release/Multiball Academy.app" pinball-notary
```

`tools/notarize.sh` checks the signature (Developer ID, hardened runtime, timestamp), zips the bundle with `ditto`, submits it with `xcrun notarytool submit --wait`, staples the ticket and verifies the result with `codesign --verify --deep --strict` and `spctl -a -vv`. Do it last: a rebuild signs the bundle again and drops the ticket. CI builds and checks the ad hoc signed bundle only; signing with a certificate and notarising are done by hand.

On Linux the same build is a position-independent executable with full RELRO, stack protection, fortified library calls and control-flow protection. SDL still opens the system's display and audio libraries at run time, as every SDL program does.

## iOS and iPadOS

The same source builds a universal iPhone and iPad application. It needs a Mac with Xcode 26 or newer and CMake's Xcode generator; the minimum system is iOS 26.0, and the build is arm64 only, for devices and for the simulator. SDL3 and Box2D are always the pinned ones (`PINBALL_FETCH_DEPS`), compiled for iOS with the rest. If Xcode is not the active developer directory, put `export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer` in front of the commands.

For the simulator:

```sh
cmake -S . -B build-ios-sim -G Xcode -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT=iphonesimulator -DPINBALL_FETCH_DEPS=ON
cmake --build build-ios-sim --config Release
```

The first configuration takes a few minutes: SDL tries several hundred small compilations to learn about the system. The result is `build-ios-sim/Release-iphonesimulator/Multiball Academy.app`, with `Multiball Academy.app.dSYM` beside it. Nothing is signed, which the simulator does not need. To run it on a simulator that is started without a window:

```sh
xcrun simctl list devices available                  # pick a device, note its identifier
xcrun simctl boot <identifier>
xcrun simctl install <identifier> "build-ios-sim/Release-iphonesimulator/Multiball Academy.app"
SIMCTL_CHILD_SDL_AUDIO_DRIVER=dummy xcrun simctl launch <identifier> io.github.rmerck.multiballacademy
xcrun simctl io <identifier> screenshot frame.png
xcrun simctl shutdown <identifier>
```

`SIMCTL_CHILD_SDL_AUDIO_DRIVER=dummy` gives the game SDL's silent audio driver. Leave it out to hear the game, with the Simulator application open; in a simulator that has no window the system's audio device can take half a minute to fail. Arguments after the identifier reach the game, so `--frames 600` works as on the desktop. The project in `build-ios-sim` can also be opened in Xcode and run from there.

What an iOS build leaves out: the development tools, `ctest` (it runs programs on the Mac, and these are iOS programs) and the macOS signing options. The core unit tests and the two file handling tests are still built, and one script runs them inside a simulator; a second checks the finished bundle. CI runs both:

```sh
tools/ios/simulator_tests.sh build-ios-sim Release
tools/ios/check_bundle.sh "build-ios-sim/Release-iphonesimulator/Multiball Academy.app" \
  build-ios-sim/product.env . --release
```

`check_bundle.sh` holds the bundle to what the App Store expects of it: the `Info.plist` values (identifier, version, build number, minimum system, launch screen, orientations, no usage descriptions), the privacy manifest, every asset identical to its source and as many as the manifest compiled into the executable lists, system libraries only, no network calls, every required-reason API declared, and for a Release build no build path in the executable and debug symbols beside the bundle.

What the application is made of, all from `cmake/Ios.cmake`:

- `cmake/Info-iOS.plist.in` is its `Info.plist`. The name, the identifier and the version come from the block at the top of `CMakeLists.txt`; the bundle version is `PINBALL_BUILD_NUMBER`.
- `packaging/ios/PrivacyInfo.xcprivacy` is the privacy manifest: no tracking, nothing collected, and two required-reason API categories, file timestamps (C617.1: the game's own files) and system boot time (35F9.1: measuring elapsed time). It covers the statically linked SDL and Box2D as well, which ship none of their own. Read it again whenever either pin moves.
- `packaging/ios/Assets.xcassets` holds the icon and the launch screen's colour. The icon there now is a placeholder with a striped frame, made by `tools/ios/make_placeholder_icon.py` from the macOS icon; an upload needs the real one, an opaque 1024 pixel square, in its place.
- The `assets` directory is copied into the bundle by Xcode, unchanged, and each build ends by checking that it arrived.
- SDL is built without its camera, sensor, joystick, haptic, HIDAPI, GPU, OpenGL ES and Vulkan code (`cmake/PinnedDeps.cmake`), so the application links no CoreMotion, CoreBluetooth, CoreHaptics or OpenGL ES and refers to no camera class. AVFoundation stays: it is the audio session. GameController stays: SDL reads hardware keyboards and mice through it. Neither needs a purpose string.

Options for iOS:

- `-DPINBALL_BUILD_NUMBER=N` is the bundle version, a whole number. App Store Connect wants a higher one for every upload of the same version.
- `-DPINBALL_IOS_TEAM_ID=XXXXXXXXXX` is the Apple developer team to sign for, with Xcode's automatic signing. It is kept in the build directory, never in the repository. Without it nothing is signed.
- `-DCMAKE_OSX_DEPLOYMENT_TARGET=...` sets the oldest iOS the build runs on. The default is 26.0.
- `-DPINBALL_WERROR=ON` and `-DPINBALL_SANITIZE=ON` work as elsewhere. `PINBALL_REQUIRE_ALL_TESTS`, `PINBALL_FUZZ`, `PINBALL_HARDENED_RUNTIME` and `PINBALL_CODESIGN_IDENTITY` do not apply; the tools are always left out.

### A build for a device, TestFlight or the App Store

A device build needs a second build directory, configured for the device SDK. Unsigned, to see that it compiles:

```sh
cmake -S . -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DPINBALL_FETCH_DEPS=ON
cmake --build build-ios --config Release
```

Everything from here on needs the Apple Developer account, so only the account holder, or someone the account holder has given a role and an App Store Connect API key, can do it: signing, the archive that is to be distributed, the export and the upload. CI signs nothing and uploads nothing.

Configure with the team, and with a build number higher than the last upload's, then archive and export:

```sh
cmake -S . -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DPINBALL_FETCH_DEPS=ON \
  -DPINBALL_IOS_TEAM_ID=XXXXXXXXXX -DPINBALL_BUILD_NUMBER=2

xcodebuild archive -project build-ios/MultiballAcademy.xcodeproj -scheme pinball \
  -configuration Release -destination 'generic/platform=iOS' \
  -archivePath build-ios/MultiballAcademy.xcarchive -allowProvisioningUpdates

cp -R "build-ios/Release-iphoneos/Multiball Academy.app.dSYM" build-ios/MultiballAcademy.xcarchive/dSYMs/

xcodebuild -exportArchive -archivePath build-ios/MultiballAcademy.xcarchive \
  -exportOptionsPlist packaging/ios/ExportOptions.plist \
  -exportPath build-ios/export -allowProvisioningUpdates
```

The `cp` puts the debug symbols into the archive, where the export looks for them: a CMake project builds into its own directory, so Xcode leaves the `.dSYM` there. The archive's `Products` holds the application and nothing else. `-allowProvisioningUpdates` lets Xcode create the certificate and the provisioning profile; it uses the account signed in to Xcode, or an API key given with `-authenticationKeyPath`, `-authenticationKeyID` and `-authenticationKeyIssuerID`. Keep the key file outside the repository. `packaging/ios/ExportOptions.plist` asks for the `app-store-connect` method (the current name; `app-store` is deprecated) and writes `build-ios/export/Multiball Academy.ipa`. Changing its `destination` from `export` to `upload` sends the build straight to App Store Connect instead; `xcrun altool --upload-app` and the Transporter application take the `.ipa`. For a trial that creates no build, set `method` to `validation`.

Before the first upload: the real icon, the app record in App Store Connect under the bundle identifier `io.github.rmerck.multiballacademy`, and the privacy policy and support pages ([PRIVACY.md](PRIVACY.md), [SUPPORT.md](SUPPORT.md)) reachable at their public addresses. After it, read the message App Store Connect sends: a required-reason API it found that the privacy manifest does not declare is named there. Then TestFlight on a real iPhone and a real iPad, for what no simulator shows: the ring/silent switch, interruptions, a 120 Hz display, heat.

## Tests and CI

`ctest` runs 17 tests, and 18 on macOS: the core unit tests (`core_tests`, no display needed), the file handling tests, the same file handling tests built without the system's single-call move-without-replace so that the `link()` fallback other systems use is run everywhere (`safe_files_fallback`), two asset checks, three offscreen render checks, a check that the snapshot tool replaces a symbolic link at its output name instead of writing through it, the layout dump check, a headless run of the real game, and six rejected command lines. On macOS `bundle_layout` also checks the application bundle: a valid `Info.plist` with the identifier and version the game reports, the icon, the assets, and nothing but the game in `Contents/MacOS`. They need no display or audio device, and scratch files stay in the build directory, beside the bundle and never in it. A timed run (`--frames`) never opens the high score file, and on Linux the tests also point the per-user preferences directory into the build tree.

Every pull request must pass seven CI jobs, all with warnings as errors. The first six build SDL3 and Box2D from the pinned sources, which are fetched and verified on every run. Only SDL's compiled objects are cached between runs, never source code, and only runs on `main` write the cache.

- macOS, with and without sanitizers.
- macOS Release, checked for build paths in the binary, system-only libraries, a complete bundle (valid `Info.plist` with the expected identifier and version, the icon, the assets), a signature that verifies over the whole bundle after the tests have run, and the hardened runtime.
- Linux with GCC and with clang, both under sanitizers.
- Linux Release with GCC, with the hardening checked: `readelf` on the binary for what the linker decides, and `tools/check_hardening.py` for the compiler flags of the game's own files and the stack-protector and FORTIFY references in two of its objects (the binary cannot show those, because the statically linked SDL has them too).
- A two minute libFuzzer run over the parsers.

An eighth job, not required, builds and tests on Linux with clang and libc++ under sanitizers. libc++ is the standard library macOS uses, so a missing include shows up there before it breaks the macOS jobs.

A ninth, `ios (simulator)`, also not required yet, builds the iOS application for the simulator on a macOS 26 runner with Xcode 26.6, Release with warnings as errors and nothing signed, checks the bundle with `tools/ios/check_bundle.sh` and runs the core and file handling tests inside a simulator with `tools/ios/simulator_tests.sh`. It does not start the game. It caches nothing.
