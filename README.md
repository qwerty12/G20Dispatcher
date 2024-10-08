# G20Dispatcher

An Android non-root userspace "key remapper" that works by receiving events directly from the input device and dispatching additional key presses.

The code is written really badly - [hacks and assumptions abound](#limitations) - but I thought it was worth sharing for the idea alone. This is non-configurable, made for the specific purpose of getting a [G20](https://www.twelectronics.co.uk/g10-g20#comp-kkrcrgz61) remote working with the 2023 onn. 4K TV Box.

Most G20 keys are recognised by the onn. box - the onn. initial Bluetooth setup wizard appears to accept a G20 as an acceptable remote for pairing (and exiting) - however, a fair few keys are simply seen by Android as KEY_UNKNOWN and the YouTube and the Netflix buttons present the same scancode to Android, making differentiating them impossible. Either the onn. doesn't actually support the G20, or the G20 I have (bought from MECOOL, the remote itself is unbranded) reports a different VID and/PID or is programmed to send out different scancodes from a reference G20.

Of course, if your device is rooted, just write a [key layout file](https://source.android.com/docs/core/interaction/input/key-layout-files) instead of dealing with this workaround.

## How does this work?

tl;dr daemon gets input notifications at the lowest level, sends out own keypresses; accessibility service starts said daemon and blocks original keypresses

G20Dispatcher consists of two parts:

* a daemon that uses evdev to receive raw keypresses from the G20 remote and send out injected key press events in response

* an [accessibility service](https://developer.android.com/guide/topics/ui/accessibility/service) that blocks Android applications from seeing the KEY_UNKNOWN presses reported from the remote and takes care of starting, stopping and respawning the daemon

The daemon looks for the /dev/input/event device node corresponding to your remote. If it's not found, inotify will listen out for new devnodes (and for the existing devnode being removed in case of, say, disconnection). It will read `input_event`s from the remote and use [`InputManager`](https://developer.android.com/reference/android/hardware/input/InputManager)'s `injectInputEvent` via Binder to send an equivalent keypress event to Android that it will understand. This is done natively in C/C++ code in the same process - `cmd input keyevent` isn't spawned, and nor is a helper written in Java (or Kotlin) used.

The daemon runs under `adbd`, as the *shell* user. On an Android device that isn't rooted, this is the only way this can work: being able to read from /dev/input requires your app's user belongs to the *input* group, which only happens if the `android.permission.DIAGNOSTIC` permission is declared - but only system applications and ADB can do that. Also, injecting synthetic key events requires `android.permission.INJECT_EVENTS`, which again is only granted to system apps and ADB (it's why `cmd input keyevent` works).

The accessibility service registers a simple [`onKeyEvent`](https://developer.android.com/reference/android/accessibilityservice/AccessibilityService#onKeyEvent(android.view.KeyEvent)) handler that will block the received KEY_UNKNOWN events with the G20-specific scancodes. The daemon cannot block already-received key presses. The accessibility service cannot inject its own key events, and nor can it receive unprocessed scancodes straight from the remote (important because Android sees two specific buttons as the same, while evdev allows for them to be discerned from each other).  
The accessibility service starts and stops the daemon via ADB. There is no communication between the daemon and the accessibility service.

The daemon will open an empty file at start and immediately `unlink` it. In the accessibility service, a [`FileObserver`](https://developer.android.com/reference/android/os/FileObserver) is used to detect when the file is closed - in theory, only one open process should hold an open file descriptor to the file, so if it's closed, we can, erm, assume the process has been terminated.

## Limitations

* It assumes Android 12 throughout (in the actual code itself, in the ADB commands ran and the build tools invoked etc. etc.)

### Daemon

* Only one G20 remote is handled; in the case of multiple G20s, the latest connected one wins

* the sentinel file is assumed to not exist at start

    * not that there should be a need, but running two instances probably isn't going to work well - there are no checks for existing instances
  
* double-tap keypresses aren't detected (possible to do if dynamically adjusting `poll`'s timeout)

* key presses are only dispatched when the button is released, so this makes holding buttons a little annoying (see `poll` point above)

* the time taken to assume a key is being held down is arbitrarily chosen and probably doesn't line up with Android's default

* the C++ code is there only because I realistically have no choice but to write C++

    * the Binder C++ interface indeed really isn't meant for use with the NDK. In order to get this to build with the NDK, a number of hacks are used:

        * a bunch of headers have been copied from an Android SQ3A.220705.001.B2 / android-12.1.0_r21 source tree

        * missing libraries were pulled from my onn. for linking

        * the daemon links to /system/lib/libstdc++.so instead of the NDK's not-included-on-devices LLVM libstdc++ via various hacks. Why this doesn't cause a crash at some point I do not know.

    * Binder NDK does provide a C interface, but that involves then maintaining C bindings for things like KeyEvent

* building the daemon isn't integrated into Gradle; CMake etc. isn't used, you need to build it manually before building the accessibility service (the APK ends up serving as a container)

    * (building the daemon is done by running a batch file)

* the code to detect the [active application](https://github.com/qwerty12/G20Dispatcher/blob/master/native/IsKodiTopmostApp.cpp) makes a lot of assumptions. However, it doesn't provide essential functionality and can simply be removed if needed

### Accessibility service

* As there's no form of IPC between the service and the daemon, there's no nice way to tell the daemon to quit. In the service, another ADB connection is established to run `killall` to stop the daemon

    * there's also no reliable way to tell if the daemon is still running, either, so the service attempts termination of the daemon only if it can assume it's been started in the first place

* also with the lack of IPC, detecting if the daemon has been terminated (`hidepid` makes the traditional way impossible) is done by using `FileObserver` to check if a file created by the daemon has been closed. Setting up the `FileObserver` here is very race-condition prone, and just unreliable in general, so there may be times termination simply isn't detected, meaning the service has to be restarted manually to restart the daemon (or `adb shell "exec $(pm path pk.q12.g20dispatcher | cut -f2 -d: | sed 's%/base.apk%/lib/arm/libg20dispatcher.so%')"`)

* ADB is used freely because this has been written for an Android TV device, and on those, the equivalent of `adb tcpip 5555` is automatically ran whenever USB debugging has been enabled

* on each initial subsequent start of the service, there's a 30-second delay before the first ADB connection to start the daemon is established. This is done because at boot time, the service fails to start the daemon reliably

    * unfortunately, the allowed ways to check if boot has completed are unreliable for this and so the delay is applied even when starting the service for the first time from the Android settings

## Key mappings

The mappings are designed to match the most natural equivalents where possible, falling back to the key codes the G10 remote sends.

| G20 Button            | Outside of Kodi                                            | Inside Kodi (same as Outside if empty)                           |
|-----------------------|-----------------------------------------------------------|------------------------------------------------------------------|
| Input      | Launch Activity defined as `INPUT_SWITCHER_ACTIVITY` in private.h | 
| Subtitles      | [`KEYCODE_CAPTIONS`](https://developer.android.com/reference/android/view/KeyEvent#KEYCODE_CAPTIONS) | [`KEYCODE_T`](https://developer.android.com/reference/android/view/KeyEvent#KEYCODE_T)<br>[`KEYCODE_L`](https://developer.android.com/reference/android/view/KeyEvent#KEYCODE_L) (held)                                                      |
| Info           | [`KEYCODE_INFO`](https://developer.android.com/reference/android/view/KeyEvent#KEYCODE_INFO)       | [`KEYCODE_I`](https://developer.android.com/reference/android/view/KeyEvent#KEYCODE_I)<br>[`KEYCODE_O`](https://developer.android.com/reference/android/view/KeyEvent#KEYCODE_L) (held)                                                                                          |
| Red            | [`KEYCODE_PROG_RED`](https://developer.android.com/reference/android/view/KeyEvent#KEYCODE_PROG_RED)   |                                                                  |
| Green          | [`KEYCODE_PROG_GREEN`](https://developer.android.com/reference/android/view/KeyEvent#KEYCODE_PROG_GREEN) |                                                                                                                                                                                                                   |
| Yellow         | [`KEYCODE_PROG_YELLOW`](https://developer.android.com/reference/android/view/KeyEvent#KEYCODE_PROG_YELLOW) |                                                                  |
| Settings       | [`KEYCODE_MEDIA_PLAY_PAUSE`](https://developer.android.com/reference/android/view/KeyEvent#KEYCODE_MEDIA_PLAY_PAUSE)<br>Launch TV Settings (held) |                                                  |
| YouTube        | Launch [SmartTube](https://github.com/yuliskov/SmartTube)<br>Launch [FCast](https://fcast.org/) |                                                      |
| Netflix        | Launch [Kodi](https://kodi.tv/) |                                                      |

## Building

Clone the project

```bash
  git clone https://github.com/qwerty12/G20Dispatcher.git
```

Go to the project directory

```bash
  cd G20Dispatcher
```

### Building the daemon

Assuming you have the NDK already set up, go the lib directory and download the needed lib files (substitute as needed if you're building for something other than an ARMv7 Android 12 device)

```bash
  cd native\extra_ndk\lib
  curl --remote-name-all "https://raw.githubusercontent.com/theworkjoy/onn_yoc_dump/onn_4k_gtv-user-12-SGZ1.221127.063.A1-9885170-release-keys/system/system/lib/libbinder.so" "https://raw.githubusercontent.com/theworkjoy/onn_yoc_dump/onn_4k_gtv-user-12-SGZ1.221127.063.A1-9885170-release-keys/system/system/lib/libc++.so" "https://raw.githubusercontent.com/theworkjoy/onn_yoc_dump/onn_4k_gtv-user-12-SGZ1.221127.063.A1-9885170-release-keys/system/system/lib/libinput.so" "https://raw.githubusercontent.com/theworkjoy/onn_yoc_dump/onn_4k_gtv-user-12-SGZ1.221127.063.A1-9885170-release-keys/system/system/lib/libutils.so"
```

Go to the native directory and edit the batch file to fix the path to the NDK and then run it

```bash
  cd G20Dispatcher\native
  code make.bat
  make
```

### Building the accessibility service

To build, make sure the android-34 android.jar from [Reginer's aosp-android-jar](https://github.com/Reginer/aosp-android-jar) is installed. Instructions for doing so can be found [here](https://github.com/1fexd/aosp-android-jar-mirror#installation).

Afterwards, just open the project in Android Studio or invoke `gradlew` directly.

## Thanks

 - [AOSP getevent.c](https://android.googlesource.com/platform/system/core.git/+/main/toolbox/getevent.c) - g20dispatcher.c is largely based on said code
 - [zhouziyang's redroid-vncserver](https://github.com/remote-android/redroid-vncserver) - the only example of using Binder directly to call `injectInputEvent`. BinderGlue.cpp is heavily based off that code
 - [gburca's BinderDemo](https://github.com/gburca/BinderDemo) which helped me to understand some of the Binder concepts
 - [MuntashirAkon's libadb-android](https://github.com/MuntashirAkon/libadb-android) - an easy, quick and reliable way to communicate with ADB which is needed by the accessibility service
 - [mirfatif's PermissionManagerX](https://github.com/mirfatif/PermissionManagerX) for the idea to bundle the daemon as an extracted "library" so it would be visible to ADB
 - [readme.so](https://readme.so/) - the base of what you're reading right now
 - [KeyTester](https://github.com/a13ssandr0/KeyTester) - useful for quickly seeing what's recieved by Android
 - ChatGPT
