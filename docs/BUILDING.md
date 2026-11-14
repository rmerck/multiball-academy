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
- `-DPINBALL_FUZZ=ON` (clang with libFuzzer, not Apple clang) builds only `pinball_fuzz`, which fuzzes the score file, PNG header, asset name and argument parsers, the review tools' window description, and what the touch screen is given: the window's size and safe area, and sequences of touches.

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

The first configuration takes a few minutes: SDL tries some 350 small compilations to learn about the system, each a run of `xcodebuild` (see "Recorded configure results" below for the way round that). The result is `build-ios-sim/Release-iphonesimulator/Multiball Academy.app`, with `Multiball Academy.app.dSYM` beside it. Nothing is signed, which the simulator does not need. To run it on a simulator that is started without a window:

```sh
xcrun simctl list devices available                  # pick a device, note its identifier
xcrun simctl boot <identifier>
xcrun simctl install <identifier> "build-ios-sim/Release-iphonesimulator/Multiball Academy.app"
SIMCTL_CHILD_SDL_AUDIO_DRIVER=dummy xcrun simctl launch <identifier> io.github.rmerck.multiballacademy
xcrun simctl io <identifier> screenshot frame.png
xcrun simctl shutdown <identifier>
```

`SIMCTL_CHILD_SDL_AUDIO_DRIVER=dummy` gives the game SDL's silent audio driver. Leave it out to hear the game, with the Simulator application open; in a simulator that has no window the system's audio device can take half a minute to fail. Arguments after the identifier reach the game, but only a Debug build, or one configured with `-DPINBALL_IOS_TEST_EXIT=ON`, honours `--frames N` and ends by itself; the Release application ignores them. The project in `build-ios-sim` can also be opened in Xcode and run from there.

What an iOS build leaves out: the development tools, `ctest` (it runs programs on the Mac, and these are iOS programs) and the macOS signing options. The core unit tests and the two file handling tests are still built, and one script runs them inside a simulator; a second checks the finished bundle. CI runs both:

```sh
bash tools/ios/simulator_tests.sh build-ios-sim Release
bash tools/ios/check_bundle.sh "build-ios-sim/Release-iphonesimulator/Multiball Academy.app" \
  build-ios-sim/product.env . --release
```

`check_bundle.sh` works on a simulator or a device build and holds the bundle to what the App Store expects of it: the `Info.plist` values (identifier, names, version, build number, minimum system, launch screen, orientations, no usage descriptions, no URL scheme, document type or other way for the system to hand the game something), the privacy manifest, every asset identical to its source and as many as the manifest compiled into the executable lists, system libraries only, no network calls, every required-reason API declared, and for a Release build no build path in the executable, no handling of launch arguments unless the build was configured with `PINBALL_IOS_TEST_EXIT`, and debug symbols beside the bundle that carry the executable's UUID. The values it expects come from `product.env` in the build directory, which CMake writes; scripts read it as text with `tools/product_value.sh` and never run it.

What the application is made of, all from `cmake/Ios.cmake`:

- `cmake/Info-iOS.plist.in` is its `Info.plist`. The name, the identifier and the version come from the block at the top of `CMakeLists.txt`; the bundle version is `PINBALL_BUILD_NUMBER`. Under its icon on the home screen the game is "Multiball" (`PINBALL_HOME_NAME`), which fits there; everywhere else it is Multiball Academy.
- `packaging/ios/PrivacyInfo.xcprivacy` is the privacy manifest: no tracking, nothing collected, and two required-reason API categories, file timestamps (C617.1: the game's own files) and system boot time (35F9.1: measuring elapsed time). It covers the statically linked SDL and Box2D as well, which ship none of their own. Read it again whenever either pin moves.
- `packaging/ios/Assets.xcassets` holds the icon (an opaque 1024 pixel square drawn by `tools/art/make_icons.py`) and the launch screen's colour (the backdrop colour, checked by ctest `art_palette`). A placeholder icon, known by a marker file `packaging/ios/ICON_IS_A_PLACEHOLDER` beside the catalog or by the hash of one that was once committed (`cmake/IosIconCheck.cmake`), stops a configuration with `PINBALL_IOS_TEAM_ID`, so it cannot be signed and shipped by mistake; ctest `ios_icon_guard` tests the guard.
- The `assets` directory is copied into the bundle by Xcode, unchanged, and each build ends by checking that it arrived.
- SDL is built without its camera, sensor, joystick, haptic, HIDAPI, GPU, OpenGL ES and Vulkan code (`cmake/PinnedDeps.cmake`), so the application links no CoreMotion, CoreBluetooth, CoreHaptics or OpenGL ES and refers to no camera class. AVFoundation, CoreMedia and CoreVideo stay: SDL links them on iOS whatever is switched off, and uses the first for the audio session. GameController stays: SDL reads hardware keyboards and mice through it. None of them needs a purpose string, and `check_bundle.sh` checks that no camera class is referred to.

Options for iOS:

- `-DPINBALL_BUILD_NUMBER=N` is the bundle version, a whole number. App Store Connect wants a higher one for every upload of the same version.
- `-DPINBALL_IOS_TEAM_ID=XXXXXXXXXX` is the Apple developer team to sign for, with Xcode's automatic signing. It is kept in the build directory, never in the repository. Without it nothing is signed.
- `-DCMAKE_OSX_DEPLOYMENT_TARGET=...` sets the oldest iOS the build runs on. The default is 26.0.
- `-DPINBALL_IOS_TEST_EXIT=ON` makes a Release application honour `--frames N` and end by itself, as a Debug one does, for automated runs on a simulator. The application that is submitted ignores launch arguments and never ends itself, so this is refused together with `PINBALL_IOS_TEAM_ID`. `tools/ios/simulator_tests.sh` starts the game for a timed run when the build has it.
- `-DPINBALL_WERROR=ON` and `-DPINBALL_SANITIZE=ON` work as elsewhere; with the first, a warning from the linker stops the build too. `PINBALL_REQUIRE_ALL_TESTS`, `PINBALL_FUZZ`, `PINBALL_HARDENED_RUNTIME` and `PINBALL_CODESIGN_IDENTITY` do not apply; the tools are always left out.

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

bash tools/ios/collect_dsym.sh build-ios/MultiballAcademy.xcarchive build-ios

xcodebuild -exportArchive -archivePath build-ios/MultiballAcademy.xcarchive \
  -exportOptionsPlist packaging/ios/ExportOptions.plist \
  -exportPath build-ios/export -allowProvisioningUpdates
```

Before the upload, check what the signature carries, on the exported `.ipa`:

```sh
bash tools/ios/check_signed.sh "build-ios/export/Multiball Academy.ipa" build-ios/product.env \
  --distribution --team XXXXXXXXXX
```

It allows only the entitlements signing adds by itself (the application identifier, the team, its keychain group, `get-task-allow` and `beta-reports-active`) and fails on any capability, such as an app group or iCloud, that was switched on somewhere; it also checks that the signature verifies, that the identifier is the configured one, that nothing but the game is inside, and, with `--distribution` and the team (`--team`, or `PINBALL_IOS_TEAM_ID` in the environment), that an Apple Distribution certificate of that team signed it, `get-task-allow` is false, `beta-reports-active` is true, and the provisioning profile inside is the team's, for this identifier, lists no devices, does not provision all devices and has not expired. `tests/CheckSignedStubs.sh` (ctest `check_signed_stubs`) tries every refusal against stand-ins for Apple's tools. Without `--distribution` it checks a development-signed `.app` the same way.

`collect_dsym.sh` puts the debug symbols into the archive, where the export looks for them: a CMake project builds into its own directory, so Xcode leaves the `.dSYM` there. The script first checks that the symbols carry the archived executable's UUID. The archive's `Products` holds the application and nothing else. `-allowProvisioningUpdates` lets Xcode create the certificate and the provisioning profile; it uses the account signed in to Xcode, or an API key given with `-authenticationKeyPath`, `-authenticationKeyID` and `-authenticationKeyIssuerID`. Keep the key file outside the repository. `packaging/ios/ExportOptions.plist` asks for the `app-store-connect` method (the current name; `app-store` is deprecated) and writes `build-ios/export/Multiball Academy.ipa`. Changing its `destination` from `export` to `upload` sends the build straight to App Store Connect instead; `xcrun altool --upload-app` and the Transporter application take the `.ipa`. For a trial that creates no build, set `method` to `validation`.

Before the first upload: the real icon (the configuration above stops until it is there), the app record in App Store Connect under the bundle identifier `io.github.rmerck.multiballacademy`, and the privacy policy and support pages ([PRIVACY.md](PRIVACY.md), [SUPPORT.md](SUPPORT.md)) reachable at their public addresses. After it, read the message App Store Connect sends: a required-reason API it found that the privacy manifest does not declare is named there. Then TestFlight on a real iPhone and a real iPad, for what no simulator shows: the ring/silent switch, interruptions, a 120 Hz display, heat.

### Recorded configure results

SDL's questions about the compiler have the same answers every time for one Xcode, one SDK, one deployment target and one SDL. `cmake/preseed` holds those answers, recorded from a full configuration, and adding `-C cmake/IosPreseed.cmake` to the configure command (after `-DCMAKE_SYSTEM_NAME` and `-DCMAKE_OSX_SYSROOT`) puts them in the cache, so a first configuration takes about a minute instead of ten on a CI runner. It is optional, and CI uses it.

A record is applied only when it was made for exactly this configuration: its header names Xcode's build version, the SDK, the deployment target and the SHA-256 of `cmake/PinnedDeps.cmake`, and all four must match. Otherwise nothing is applied, a notice says so and every question is asked as before. A record is never run as CMake code: it is read as lines of `NAME=1` or `NAME=`, and only names of SDL's yes-or-no results are accepted, so it cannot change which sources are fetched, their pinned hash, or any option.

After an Xcode update on the CI image, or a change to `cmake/PinnedDeps.cmake`, the old record no longer matches. CI then configures in full and uploads a new record as the artifact `ios-preseed`; read it, put it in `cmake/preseed` and delete the one it replaces. By hand, from a build directory that was configured without `-C`:

```sh
cmake -DBUILD=build-ios-sim -P cmake/MakeIosPreseed.cmake
```

## Tests and CI

`ctest` runs 142 tests on Linux, and 143 on macOS; 110 of them are the layout renders and checks (label `layout`), and two, `app_events` and `app_saved_game`, are slow (`ctest -LE slow` leaves them out). Among the rest: the core unit tests (`core_tests`, no display needed), a check that the table's layout header uses none of the C library's mathematics (`table_layout_no_libm`), the file handling tests, the same file handling tests built without the system's single-call move-without-replace so that the `link()` fallback other systems use is run everywhere (`safe_files_fallback`), two asset checks, five offscreen render checks (two of them the table with a ball locked in the Hangar and with the bay doors down), a check that the snapshot tool replaces a symbolic link at its output name instead of writing through it, the layout dump check, a check that the recorded configure results for iOS hold only probe results, a check of the rule the privacy policy's address is held to, the refusals of the signed-build check, a headless run of the real game, and six rejected command lines. On macOS `bundle_layout` also checks the application bundle: a valid `Info.plist` with the identifier and version the game reports, the icon, the assets, and nothing but the game in `Contents/MacOS`. They need no display or audio device, and scratch files stay in the build directory, beside the bundle and never in it. A timed run (`--frames`) never opens the high score file, and on Linux the tests also point the per-user preferences directory into the build tree.

Every pull request must pass seven CI jobs, all with warnings as errors. The first six build SDL3 and Box2D from the pinned sources, which are fetched and verified on every run. Only SDL's compiled objects are cached between runs, never source code, and only runs on `main` write the cache.

- macOS, with and without sanitizers.
- macOS Release, checked for build paths in the binary, system-only libraries, a complete bundle (valid `Info.plist` with the expected identifier and version, the icon, the assets), a signature that verifies over the whole bundle after the tests have run, and the hardened runtime.
- Linux with GCC and with clang, both under sanitizers.
- Linux Release with GCC, with the hardening checked: `readelf` on the binary for what the linker decides, and `tools/check_hardening.py` for the compiler flags of the game's own files and the stack-protector and FORTIFY references in two of its objects (the binary cannot show those, because the statically linked SDL has them too).
- A two minute libFuzzer run over the parsers.

An eighth job, not required, builds and tests on Linux with clang and libc++ under sanitizers. libc++ is the standard library macOS uses, so a missing include shows up there before it breaks the macOS jobs.

A ninth, `ios (simulator)`, also not required yet, builds the iOS application on a macOS 26 runner with Xcode 26.6, Release with warnings as errors and nothing signed, for the simulator and for a device. It checks both bundles with `tools/ios/check_bundle.sh` and runs the core and file handling tests inside a simulator with `tools/ios/simulator_tests.sh`. The game is started there for 300 frames and must report a good run and end by itself. It caches nothing; it configures from the recorded results described above when they match.

### The table's acceptance suite

The table is judged by bots: an idle one, a random one, a reflex player with a timing error of 3 to 8 simulation steps, and a scripted one that sweeps every flipper timing (`tests/bots`). The quick checks run inside `core_tests` with every build: no launch drains, every exit feeds a flipper or an inlane, the table's own elements (door bank, lock, scoop, spinner, kickback, sensor lines: `tests/ElementTest.cpp`), no ball sticks with one, two or three balls in play, and the same seed plays the same game. The statistical criteria (ball times, drain split, shot windows, ten million steps with three balls, and whole games under the rules: multiball and course starts, multiball length, score shares, the length of a game) are a program of their own, `pinball_acceptance`, which plays about half a billion steps: about three quarters of an hour on two cores. It is built with everything else but its tests are registered only when asked for:

```sh
cmake -B build-acceptance -DCMAKE_BUILD_TYPE=Release -DPINBALL_ACCEPTANCE_TESTS=ON
cmake --build build-acceptance --target pinball_acceptance
ctest --test-dir build-acceptance -L acceptance -j 2 --output-on-failure
```

Or run it directly for the report as one table, all of it or the sections named (`launch shots lanes exits rest wedge p1 p2 p2saver kickback stress rules`; `p2:5` and `rules:5` are one timing error, `stress:p1` one bot; `--quick` cuts the samples to try it out):

```sh
build-acceptance/pinball_acceptance | grep '^ROW'
```

Each line is a criterion with its threshold, the measured value and a verdict. `FAIL` is a fairness or structural criterion not met, and the program then ends with status 1. `KNOWN-MISS` is a pacing criterion the design is known to miss: it is reported with its value and does not fail while it is no worse than its pin, the value recorded for it plus a margin for the size of the sample; beyond the pin it is a `FAIL`. `INFO` is measured for the record, and `DEFERRED` needs an element the game does not have yet. Use an optimized build without sanitizers. The thresholds are in `tests/AcceptanceMain.cpp`; change one only with the design. The suite runs in full once a week and on request (`.github/workflows/acceptance.yml`); it is not a required check.
