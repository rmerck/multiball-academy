# The iOS application: bundle properties, Info.plist, icon, privacy manifest
# and the assets inside the bundle, for the pinball target that
# CMakeLists.txt has just created. Included only when PINBALL_IOS is ON,
# which CMakeLists.txt allows with the Xcode generator alone: the resources
# below are copied, compiled and sealed by Xcode's own build phases.

# TEMPORARY, until src/main_ios.cpp is written: without an entry point the
# application does not link, so a stand-in is compiled. It goes by itself
# once the real file defines SDL_AppInit; then delete this block and
# packaging/ios/TemporaryMain.cpp.
file(STRINGS ${CMAKE_SOURCE_DIR}/src/main_ios.cpp pinball_ios_entry REGEX "SDL_AppInit *\\(")
if(NOT pinball_ios_entry)
  message(WARNING "src/main_ios.cpp has no entry point yet: building the temporary one, "
                  "packaging/ios/TemporaryMain.cpp")
  target_sources(pinball PRIVATE ${CMAKE_SOURCE_DIR}/packaging/ios/TemporaryMain.cpp)
endif()

# Resources. In an iOS bundle "Resources" is the bundle's top directory.
#
# The assets go in as a folder: Xcode copies the directory as it is (no PNG
# is rewritten, so each still matches the SHA-256 compiled into the game),
# inside the application target's own build and before the bundle is signed.
# They end up at <Name>.app/assets, where SDL_GetBasePath() points. The
# pinball_assets target that copies them on other systems cannot be used
# here: for iOS the bundle's path holds a build setting, written
# ${EFFECTIVE_PLATFORM_NAME}, that only Xcode can fill in.
#
# The asset catalog holds the icon and the launch screen's colour. The
# privacy manifest declares what the game and the statically linked SDL and
# Box2D use of the APIs Apple wants a reason for; it must be re-read whenever
# either pin moves.
set(pinball_ios_resources
  ${CMAKE_SOURCE_DIR}/assets
  ${CMAKE_SOURCE_DIR}/packaging/ios/Assets.xcassets
  ${CMAKE_SOURCE_DIR}/packaging/ios/PrivacyInfo.xcprivacy)
target_sources(pinball PRIVATE ${pinball_ios_resources})
set_source_files_properties(${pinball_ios_resources} PROPERTIES
  MACOSX_PACKAGE_LOCATION Resources)

set_target_properties(pinball PROPERTIES
  OUTPUT_NAME "${PINBALL_APP_NAME}"
  MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/cmake/Info-iOS.plist.in
  MACOSX_BUNDLE_BUNDLE_NAME "${PINBALL_APP_NAME}"
  MACOSX_BUNDLE_GUI_IDENTIFIER ${PINBALL_BUNDLE_ID}
  MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
  # On iOS the bundle version is the build number, which must rise with
  # every upload of the same version.
  MACOSX_BUNDLE_BUNDLE_VERSION ${PINBALL_BUILD_NUMBER}
  MACOSX_BUNDLE_COPYRIGHT "${PINBALL_COPYRIGHT}"
  XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${PINBALL_BUNDLE_ID}
  XCODE_ATTRIBUTE_MARKETING_VERSION ${PROJECT_VERSION}
  XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION ${PINBALL_BUILD_NUMBER}
  # iPhone and iPad.
  XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
  # The iOS application is not offered on the Mac or the Vision Pro.
  XCODE_ATTRIBUTE_SUPPORTS_MACCATALYST NO
  XCODE_ATTRIBUTE_SUPPORTS_MAC_DESIGNED_FOR_IPHONE_IPAD NO
  XCODE_ATTRIBUTE_SUPPORTS_XR_DESIGNED_FOR_IPHONE_IPAD NO
  XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME AppIcon
  # An archive holds the application and nothing else. (CMakeLists.txt sets
  # SKIP_INSTALL for every other target.)
  XCODE_ATTRIBUTE_SKIP_INSTALL NO
  XCODE_ATTRIBUTE_INSTALL_PATH "$(LOCAL_APPS_DIR)")

# A Release application: the debug information goes to a .dSYM beside the
# bundle, for reading crash reports, and the executable is stripped, so it
# carries no symbol names and no path of the machine that built it.
# (CMakeLists.txt adds -g to the Release flags for this.)
set_target_properties(pinball PROPERTIES
  "XCODE_ATTRIBUTE_GCC_GENERATE_DEBUGGING_SYMBOLS[variant=Release]" YES
  "XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT[variant=Release]" "dwarf-with-dsym"
  "XCODE_ATTRIBUTE_DEPLOYMENT_POSTPROCESSING[variant=Release]" YES
  "XCODE_ATTRIBUTE_STRIP_INSTALLED_PRODUCT[variant=Release]" YES
  "XCODE_ATTRIBUTE_STRIP_STYLE[variant=Release]" all
  "XCODE_ATTRIBUTE_SEPARATE_STRIP[variant=Release]" YES)

# Signing is Xcode's. With a team id it signs automatically for that team
# (development for a device build, distribution when an archive is
# exported). Without one nothing is signed: that is enough for the
# simulator and for a device build that is only compiled. The id is given
# on the command line and lives in the build directory alone.
if(PINBALL_IOS_TEAM_ID)
  set_target_properties(pinball PROPERTIES
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE Automatic
    XCODE_ATTRIBUTE_DEVELOPMENT_TEAM ${PINBALL_IOS_TEAM_ID})
else()
  set_target_properties(pinball PROPERTIES
    XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO
    XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED NO
    XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "")
endif()

# Every build ends by checking that the bundle holds the assets, each one
# identical to its source, and the privacy manifest: an application without
# them builds and then refuses to start, or is refused at upload. The script
# finds the bundle through the variables Xcode gives its build phases.
add_custom_command(TARGET pinball POST_BUILD
  COMMAND sh ${CMAKE_SOURCE_DIR}/tools/ios/check_bundle.sh --resources ${CMAKE_SOURCE_DIR}
  COMMENT "Checking the bundle's resources"
  VERBATIM)
