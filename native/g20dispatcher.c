#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/inotify.h>
#include <sys/limits.h>
#include <sys/poll.h>
#include <sys/cdefs.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <linux/input.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <spawn.h>
#include <android/keycodes.h>

#include "BinderGlue.h"
#include "IsKodiTopmostApp.h"
#if defined __has_include
#  if __has_include ("private.h")
#    include "private.h"
#  endif
#endif

#define	nitems(x) (sizeof((x)) / sizeof((x)[0]))

#define KEY_HOLD_THRESHOLD_US 500000
static const char *device_path = "/dev/input";

static char g20_open_device_path[PATH_MAX] = { 0, };
static struct pollfd ufds[3];
static int nfds;
#define UFDS_IDX_G20 nitems(ufds) - 1

static void close_g20_device()
{
    g20_open_device_path[0] = '\0';

    if (ufds[UFDS_IDX_G20].fd > -1) {
        close(ufds[UFDS_IDX_G20].fd);
        ufds[UFDS_IDX_G20].fd = -1;
    }

    nfds = nitems(ufds) - 1;
}

static bool open_g20_device(const char *devname)
{
    char name[80];
    name[sizeof(name) - 1] = '\0';

    const int fd = open(devname, O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) >= 1 && !strcmp(name, "RemoteG20 Consumer Control")) {
            close_g20_device();

            strcpy(g20_open_device_path, devname);
            ufds[UFDS_IDX_G20].fd = fd;
            nfds = nitems(ufds);

            return true;
        }

        close(fd);
    }

    return false;
}

static void read_notify_from_device_path(const char *dirname, int nfd)
{
    int res;
    char devname[PATH_MAX];
    char *filename;
    char event_buf[512];
    int event_size;
    int event_pos = 0;
    struct inotify_event *event;

    res = read(nfd, event_buf, sizeof(event_buf));
    if (res < (int)sizeof(*event))
        return;

    strcpy(devname, dirname);
    filename = devname + strlen(devname);
    *filename++ = '/';

    while (res >= (int)sizeof(*event)) {
        event = (struct inotify_event *)(event_buf + event_pos);

        if (event->len) {
            strcpy(filename, event->name);

            if (event->mask & IN_CREATE) {
                (void) open_g20_device(devname);
            } else {
                if (!strcmp(g20_open_device_path, devname))
                    close_g20_device();
            }
        }

        event_size = sizeof(*event) + event->len;
        res -= event_size;
        event_pos += event_size;
    }
}

static void scan_device_path_for_g20()
{
    char devname[PATH_MAX];
    char *filename;
    DIR *dir;
    struct dirent *de;

    if (!(dir = opendir(device_path)))
        return;

    strcpy(devname, device_path);
    filename = devname + strlen(devname);
    *filename++ = '/';
    while ((de = readdir(dir))) {
        if (de->d_name[0] == '.' &&
           (de->d_name[1] == '\0' ||
            (de->d_name[1] == '.' && de->d_name[2] == '\0')))
            continue;
        
        if (__predict_false(strncmp(de->d_name, "event", 5)))
            continue;

        strcpy(filename, de->d_name);

        if (open_g20_device(devname))
            break;
    }

    closedir(dir);
}

static void start_cmd(const char* const args[])
{
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_USEVFORK);
    posix_spawn(NULL, "/system/bin/cmd", NULL, &attr, (char *const *)args, NULL);
}

static void launch_activity(const char *intent_package)
{
    const char* const args[] = { "cmd", "activity", "start", intent_package, NULL };
    start_cmd(args);
}

static void daemonise()
{
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) < 0) {
        perror("getrlimit");
        exit(EXIT_FAILURE);
    }
    if (rl.rlim_max == RLIM_INFINITY)
        rl.rlim_max = 1024;

    for (int i = STDERR_FILENO + 1; i < rl.rlim_max; ++i)
        close(i);

    /* for (int i = 1; i < _NSIG; ++i)
        signal(i, SIG_DFL);

    sigset_t set;
    sigemptyset(&set);
    sigprocmask(SIG_SETMASK, &set, NULL); */

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    } else if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    if (setsid() < 0) {
        perror("setsid");
        exit(EXIT_FAILURE);
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    pid = fork();
    if (pid < 0)
        exit(EXIT_FAILURE);
    else if (pid > 0)
        exit(EXIT_SUCCESS);

    // stdin
    open("/dev/null", O_RDWR);
    // stdout
    open("/dev/null", O_RDWR);
    // stderr
    open("/dev/null", O_RDWR);

    umask(0);

    if (chdir("/") < 0)
        exit(EXIT_FAILURE);
}

int main(void)
{
    // Apps can access files in subdirectories of /data/local/tmp/ but not those in /data/local/tmp/ itself
    const char *filename = "/data/local/tmp/.g20dispatcher/sentinel";

    daemonise();

    const int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd == -1) {
        perror("Error opening file");
        return EXIT_FAILURE;
    }

    if (unlink(filename) == -1) {
        perror("Error unlinking file");
        return EXIT_FAILURE;
    }

    nfds = 2;
    ufds[0].fd = inotify_init();
    ufds[1].fd = SetupBinderOrCrash();
    ufds[UFDS_IDX_G20].fd = -1;
    for (size_t i = 0; i < nitems(ufds); ++i)
        ufds[i].events = POLLIN;

    if (inotify_add_watch(ufds[0].fd, device_path, IN_DELETE | IN_CREATE) < 0)
        return EXIT_FAILURE;

    scan_device_path_for_g20();
    connectInputService();

    __s32 press_keycode = 0;
    suseconds_t press_usec = 0;

    for (;;) {
        poll(ufds, nfds, -1);

        if (__predict_false(ufds[0].revents & POLLIN))
            read_notify_from_device_path(device_path, ufds[0].fd);

        if (__predict_false(ufds[1].revents & POLLIN))
            OnBinderReadReady();

        if (__predict_false(nfds < nitems(ufds))) {
            if (press_keycode) press_keycode = 0;
            if (press_usec) press_usec = 0;
            continue;
        }

        if (__predict_false((ufds[UFDS_IDX_G20].revents & POLLIN) == 0))
            continue;

        struct input_event event;

        if (__predict_false(read(ufds[UFDS_IDX_G20].fd, &event, sizeof(event)) < (int)sizeof(event)))
            continue;

        if (event.type == EV_MSC) {
            if (press_keycode == 0) {
                press_keycode = event.value;
            } else if (event.value != press_keycode) {
                press_keycode = 0;
                press_usec = 0;
                continue;
            }
        }

        if (press_keycode == 0)
            continue;

        if (event.type != EV_KEY)
            continue;

        if (press_usec == 0) {
            if (event.value == 1) {
                press_usec = (event.input_event_sec * 1000000L) + event.input_event_usec;
                continue;
            }

            if (event.value != 0)
                continue;
        }

        const suseconds_t duration = ((event.input_event_sec * 1000000L) + event.input_event_usec) - press_usec;
        const KeyPressMode mode = __predict_true(duration < KEY_HOLD_THRESHOLD_US) ? KEYPRESS_NORMAL : KEYPRESS_LONG_PRESS;

        #define SEND_KEYPRESS(translated_keycode) injectInputEvent(translated_keycode, mode); break
        switch (press_keycode)
        {
            #ifdef INPUT_SWITCHER_ACTIVITY
            case 0x000c01bb: // (Input)
                if (__predict_true(mode == KEYPRESS_NORMAL))
                    launch_activity(INPUT_SWITCHER_ACTIVITY);
                break;
            #endif
            case 0x000c0061: // KEY_SUBTITLE
                if (__predict_true(IsKodiTopmostApp())) {
                    if (__predict_true(mode == KEYPRESS_NORMAL)) {
                        injectInputEvent(AKEYCODE_T, KEYPRESS_NORMAL);
                    } else if (mode == KEYPRESS_LONG_PRESS) {
                        injectInputEvent(AKEYCODE_L, KEYPRESS_NORMAL);
                    }
                    break;
                } else {
                    SEND_KEYPRESS(AKEYCODE_CAPTIONS);
                }
            case 0x000c01bd: // KEY_INFO
                if (__predict_true(IsKodiTopmostApp())) {
                    if (__predict_true(mode == KEYPRESS_NORMAL)) {
                        injectInputEvent(AKEYCODE_I, KEYPRESS_NORMAL);
                    } else if (mode == KEYPRESS_LONG_PRESS) {
                        injectInputEvent(AKEYCODE_O, KEYPRESS_NORMAL);
                    }
                    break;
                } else {
                    SEND_KEYPRESS(AKEYCODE_INFO);
                }
            case 0x000c0069: // KEY_RED
                SEND_KEYPRESS(AKEYCODE_PROG_RED);
            case 0x000c006a: // KEY_GREEN
                SEND_KEYPRESS(AKEYCODE_PROG_GREEN);
            case 0x000c006c: // KEY_YELLOW
                SEND_KEYPRESS(AKEYCODE_PROG_YELLOW);
            case 0x000c0096: // KEY_TAPE
                if (__predict_true(mode == KEYPRESS_NORMAL)) {
                    injectInputEvent(AKEYCODE_MEDIA_PLAY_PAUSE, KEYPRESS_NORMAL);
                } else if (mode == KEYPRESS_LONG_PRESS) {
                    const char* const args[] = { "cmd", "activity", "start", "-a", "android.settings.SETTINGS", NULL };
                    start_cmd(args);
                }
                break;
            case 0x000c0077: // (YouTube)
                launch_activity("com.teamsmart.videomanager.tv"); break;
            case 0x000c0078: // (Netflix)
                if (__predict_true(mode == KEYPRESS_NORMAL)) {
                    launch_activity("org.xbmc.kodi/.Splash");
                }
                #ifdef NETFLIX_LONG_PRESS_ACTIVITY
                else if (mode == KEYPRESS_LONG_PRESS) {
                    launch_activity(NETFLIX_LONG_PRESS_ACTIVITY);
                }
                #endif
                break;
            default:
                break;
        }
        #undef SEND_KEYPRESS

        press_keycode = 0;
        press_usec = 0;
    }

    return EXIT_SUCCESS;
}