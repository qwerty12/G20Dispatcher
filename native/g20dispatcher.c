#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <dirent.h>
#include <fcntl.h>
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
#include <android/keycodes.h>
#include <android/log.h>
#include "BinderGlue.h"
#include "IsKodiTopmostApp.h"

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

    int fd = open(devname, O_RDONLY | O_CLOEXEC);
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

static void daemonise(void)
{
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) < 0)
        exit(EXIT_FAILURE);

    if (rl.rlim_max == RLIM_INFINITY)
        rl.rlim_max = 1024;

	pid_t pid = fork();
	if (pid < 0)
		exit(EXIT_FAILURE);
	else if (pid > 0)
		exit(EXIT_SUCCESS);

    umask(0);
	if (setsid() < 0)
		exit(EXIT_FAILURE);
	if (chdir("/") < 0)
		exit(EXIT_FAILURE);

    pid = fork();
	if (pid < 0)
		exit(EXIT_FAILURE);
	else if (pid > 0)
		exit(EXIT_SUCCESS);

    for (int i = 0; i < rl.rlim_max; ++i)
        close(i);

	// stdin
    open("/dev/null", O_RDONLY | O_CLOEXEC);
    // stdout
    open("/dev/null", O_RDWR | O_CLOEXEC);
	// stderror
    dup2(STDOUT_FILENO, STDERR_FILENO);
}

int main(void)
{
    // Apps can access files in subdirectories of /data/local/tmp/ but not those in /data/local/tmp/ itself
    const char *filename = "/data/local/tmp/.g20dispatcher/sentinel";

    daemonise();

    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
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

    if (inotify_add_watch(ufds[0].fd, device_path, IN_DELETE | IN_CREATE) < 0) {
        fprintf(stderr, "could not add watch for %s, %s\n", device_path, strerror(errno));
        return EXIT_FAILURE;
    }

    scan_device_path_for_g20();
    connectInputService();

    __s32 press_keycode = 0;
    suseconds_t press_usec = 0;

    for (;;) {
        poll(ufds, nfds, -1);

        if (ufds[0].revents & POLLIN)
            read_notify_from_device_path(device_path, ufds[0].fd);

        if (ufds[1].revents & POLLIN)
            OnBinderReadReady();

        if (nfds < nitems(ufds)) {
            if (press_keycode) press_keycode = 0;
            if (press_usec) press_usec = 0;
            continue;
        }

        if (ufds[UFDS_IDX_G20].revents & POLLIN) {
            struct input_event event;

            if (__predict_false(read(ufds[UFDS_IDX_G20].fd, &event, sizeof(event)) < (int)sizeof(event))) {
                fprintf(stderr, "could not get event\n");
                continue;
            }

            if (event.type == EV_MSC) {
                if (press_keycode == 0) {
                    press_keycode = event.value;
                } else if (event.value != press_keycode) {
                    press_keycode = 0;
                    press_usec = 0;
                    continue;
                }
            }

            if (__predict_true(press_keycode > 0)) {
                if (event.type == EV_KEY && press_usec == 0 && event.value == 1) {
                    press_usec = (event.input_event_sec * 1000000L) + event.input_event_usec;
                } else if (event.type == EV_KEY && press_usec != 0 && event.value == 0) {
                    const suseconds_t duration = ((event.input_event_sec * 1000000L) + event.input_event_usec) - press_usec;
                    const KeyPressMode mode = __predict_true(duration < KEY_HOLD_THRESHOLD_US) ? KEYPRESS_NORMAL : KEYPRESS_LONG_PRESS;

                    #define SEND_KEYPRESS(translated_keycode) injectInputEvent(translated_keycode, mode); break
                    #define SEND_LOG_MESSAGE(translated_keycode) __android_log_print(ANDROID_LOG_VERBOSE, "G20D", __STRING(translated_keycode %d), mode); break
                    switch (press_keycode)
                    {
                        case 0x000c0061: // KEY_SUBTITLE
                            if (IsKodiTopmostApp()) {
                                SEND_KEYPRESS(AKEYCODE_T);
                            } else {
                                SEND_KEYPRESS(AKEYCODE_CAPTIONS);
                            }
                        case 0x000c01bd: // KEY_INFO
                            if (IsKodiTopmostApp()) {
                                SEND_KEYPRESS(AKEYCODE_I);
                            } else {
                                SEND_KEYPRESS(AKEYCODE_INFO);
                            }
                        case 0x000c0069: // KEY_RED
                            SEND_KEYPRESS(AKEYCODE_PROG_RED);
                        case 0x000c006a: // KEY_GREEN
                            if (mode == KEYPRESS_NORMAL) {
                                if (IsKodiTopmostApp()) {
                                    injectInputEvent(AKEYCODE_SPACE, KEYPRESS_NORMAL);
                                } else {
                                    injectInputEvent(AKEYCODE_MEDIA_PLAY_PAUSE, KEYPRESS_NORMAL);
                                }
                            } else if (mode == KEYPRESS_LONG_PRESS) {
                                injectInputEvent(AKEYCODE_PROG_GREEN, KEYPRESS_NORMAL);
                            }
                            break;
                        case 0x000c006c: // KEY_YELLOW
                            SEND_KEYPRESS(AKEYCODE_PROG_YELLOW);
                        case 0x000c006b: // KEY_BLUE
                            SEND_KEYPRESS(AKEYCODE_PROG_BLUE);
                        case 0x000c0096: // KEY_TAPE
                            SEND_LOG_MESSAGE(AKEYCODE_NOTIFICATION);
                        case 0x000c0077: // (YouTube)
                            SEND_LOG_MESSAGE(AKEYCODE_BUTTON_3);
                        case 0x000c0078: // (Netflix)
                            SEND_LOG_MESSAGE(AKEYCODE_BUTTON_4);
                        case 0x000c0079: // KEY_KBDILLUMUP (Prime Video)
                            SEND_LOG_MESSAGE(AKEYCODE_BUTTON_9);
                        case 0x000c007a: // KEY_KBDILLUMDOWN (Google Play)
                            SEND_LOG_MESSAGE(AKEYCODE_BUTTON_10);
                        default:
                            break;
                    }
                    #undef SEND_KEYPRESS
                    #undef SEND_LOG_MESSAGE

                    press_keycode = 0;
                    press_usec = 0;
                }
            }
        }
    }

    return EXIT_SUCCESS;
}