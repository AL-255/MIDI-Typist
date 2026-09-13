#ifndef KEYBOARD_BUILD_H
#define KEYBOARD_BUILD_H

/* Host-visible build identity, "<version>-<build target>". The build supplies
 * both parts: the CMake project version and the board model the port targets,
 * e.g. v0.1.0-RZ03-0499 for the Huntsman V3 Pro Mini firmware. Fallbacks keep
 * portable and synthetic test builds self-describing without board config. */
#ifndef MT_BUILD_VERSION
#define MT_BUILD_VERSION "0.0.0"
#endif
#ifndef MT_BUILD_TARGET
#define MT_BUILD_TARGET "unknown"
#endif
#define MT_BUILD_ID "v" MT_BUILD_VERSION "-" MT_BUILD_TARGET

/* Direct, non-CMake logic tests have no repository provenance. Never report
 * a guessed commit for an unversioned source export or standalone compile. */
#ifndef MT_GIT_COMMIT
#define MT_GIT_COMMIT "unknown"
#endif
#ifndef MT_GIT_STATE
#define MT_GIT_STATE "unknown"
#endif
#define MT_GIT_REPLY "git=" MT_GIT_COMMIT " state=" MT_GIT_STATE
#define MT_BUILD_INFO MT_BUILD_ID " " MT_GIT_REPLY

#endif
