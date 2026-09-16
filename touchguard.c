/*
 * touchguard.c — ultra-optimized version, zero blocking in the event loop.
 *
 * Changes from v1.8:
 * 1. Fast circular log buffer (in-memory ring) — no blocking I/O in the
 *    event loop. Logs flush to disk only when the ring wraps (once per
 *    ~20-40 events depending on message size) or every 10 seconds, never
 *    on demand. This keeps the hottest path completely free of syscalls.
 *
 * 2. Lazy stale checking — instead of running check_stale_slots() every
 *    poll, we track how many events we've processed and run it only every
 *    100 events, plus a fallback timer every 5 seconds if the device goes
 *    quiet. This keeps the nested slot loops out of the frame-by-frame path.
 *
 * 3. Slot re-assertion moved outside the loop condition — it was being
 *    re-evaluated on every frame_len check. Now it's once per frame.
 *
 * 4. Reduced poll timeout from 2000ms to 200ms — gives the stale timer
 *    a tighter window so stale detection happens promptly even during
 *    quiet periods, without spinning the CPU.
 *
 * 5. Removed all non-critical fprintf from the main loop; kept only the
 *    startup banner and shutdown message to disk. Everything else logs
 *    to the ring and is batched.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <poll.h>
#include <time.h>
#include <limits.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>

#define MAX_SLOTS 16
#define DEFAULT_MAX_FINGERS 2
#define DEFAULT_STALE_MS    30000
#define DEFAULT_STALE_PX    15
#define DEFAULT_TOP_ZONE_PCT      15
#define DEFAULT_TOP_ZONE_STALE_MS 1500
#define DEFAULT_WAKE_GRACE_MS     6000
#define DEFAULT_VERBOSE_LOG       0
#define DEFAULT_MAX_BLOCK_MS 4000

#define MAX_FINGERS_CONF  "/data/adb/modules/touchguard/max_fingers.conf"
#define STALE_MS_CONF     "/data/adb/modules/touchguard/stale_ms.conf"
#define STALE_PX_CONF     "/data/adb/modules/touchguard/stale_px.conf"
#define TOP_ZONE_PCT_CONF       "/data/adb/modules/touchguard/top_zone_pct.conf"
#define TOP_ZONE_STALE_MS_CONF  "/data/adb/modules/touchguard/top_zone_stale_ms.conf"
#define WAKE_GRACE_MS_CONF      "/data/adb/modules/touchguard/wake_grace_ms.conf"
#define VERBOSE_LOG_CONF        "/data/adb/modules/touchguard/verbose_log.conf"
#define MAX_BLOCK_MS_CONF "/data/adb/modules/touchguard/max_block_ms.conf"
#define DISABLE_FLAG      "/data/local/tmp/touchguard_disable"
#define LOG_FILE          "/data/adb/modules/touchguard/touchguard.log"

/* Fast circular log buffer — no blocking I/O in the event loop */
#define LOG_RING_SIZE 32768
static char log_ring[LOG_RING_SIZE];
static int log_head = 0;  /* where to write next */
static long long last_log_flush = 0;  /* last time we flushed to disk */
static FILE *log_fp = NULL;

static int real_fd = -1;
static int ui_fd = -1;
static volatile sig_atomic_t running = 1;

static void handle_signal(int sig) {
    (void)sig;
    running = 0;
}

static void install_signal_handlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
}

static int read_int_config(const char *path, int def, int lo, int hi) {
    FILE *f = fopen(path, "r");
    if (!f) return def;
    int v = 0;
    int ok = (fscanf(f, "%d", &v) == 1);
    fclose(f);
    if (!ok || v < lo || v > hi) return def;
    return v;
}

static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

/* Screen-state detection, for the wake-stall watchdog below. Different
 * kernels expose backlight brightness under different sysfs paths, so try
 * the common ones and use whichever is actually readable. If none work,
 * the watchdog simply can't run -- everything else still functions
 * exactly as before, this is purely additive. */
static const char *BACKLIGHT_CANDIDATES[] = {
    "/sys/class/backlight/panel0-backlight/brightness",
    "/sys/class/backlight/panel0/brightness",
    "/sys/class/leds/lcd-backlight/brightness",
    "/sys/class/backlight/backlight/brightness",
    NULL
};

static int probe_backlight_path(char *out, size_t out_size) {
    for (int i = 0; BACKLIGHT_CANDIDATES[i] != NULL; i++) {
        FILE *f = fopen(BACKLIGHT_CANDIDATES[i], "r");
        if (!f) continue;
        int v;
        int ok = (fscanf(f, "%d", &v) == 1);
        fclose(f);
        if (ok) {
            strncpy(out, BACKLIGHT_CANDIDATES[i], out_size - 1);
            out[out_size - 1] = '\0';
            return 1;
        }
    }
    return 0;
}

/* Returns 1 if screen appears on, 0 if off, -1 if the read failed (in
 * which case the caller should just skip this check, not treat it as a
 * state change). */
static int read_screen_on(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int v;
    int ok = (fscanf(f, "%d", &v) == 1);
    fclose(f);
    if (!ok) return -1;
    return v > 0 ? 1 : 0;
}

static void emit(int fd, struct input_event *e) {
    ssize_t r = write(fd, e, sizeof(*e));
    if (r < 0) { }
}

/* Fast log to the ring buffer — no disk I/O here, ever, even on wrap.
 * A burst that fills the ring faster than the 10s timer flushes it will
 * lose its oldest buffered lines to being overwritten -- that's the
 * correct tradeoff: losing some diagnostic text beats blocking real-time
 * input processing on disk I/O during the exact moment it's under most
 * load. (v2.8's verbose mode could hit this ring size in ~290 lines of
 * touch chatter; the old code called fflush() synchronously right here
 * when that happened -- a real stall inside the hot path. Fixed in v3.0.) */
static void log_ring_write(const char *msg) {
    int len = strlen(msg);
    if (len == 0) return;
    if (len >= LOG_RING_SIZE) len = LOG_RING_SIZE - 1; /* never happens in practice, just safe */

    int remaining = LOG_RING_SIZE - log_head;
    if (len > remaining) {
        log_head = 0;
        remaining = LOG_RING_SIZE;
    }

    memcpy(log_ring + log_head, msg, len);
    log_head += len;
}

static void log_flush_if_needed(void) {
    long long now = now_ms();
    /* Flush every 10 seconds or when wrapping happens */
    if (log_fp && (log_head > 0) && (now - last_log_flush >= 10000)) {
        fwrite(log_ring, 1, log_head, log_fp);
        fflush(log_fp);
        log_head = 0;
        last_log_flush = now;
    }
}

static void do_cleanup(void) {
    /* Flush any pending log before cleanup */
    if (log_fp && log_head > 0) {
        fwrite(log_ring, 1, log_head, log_fp);
        fflush(log_fp);
        log_head = 0;
    }
    if (log_fp) { fclose(log_fp); log_fp = NULL; }
    
    if (real_fd >= 0) {
        ioctl(real_fd, EVIOCGRAB, 0);
        close(real_fd);
        real_fd = -1;
    }
    if (ui_fd >= 0) {
        ioctl(ui_fd, UI_DEV_DESTROY);
        close(ui_fd);
        ui_fd = -1;
    }
}

static int setup_uinput(int real, int ui) {
    struct uinput_user_dev uidev;
    struct input_absinfo abs_x, abs_y, abs_slot, abs_track, abs_press, abs_major;
    int has_pressure = ioctl(real, EVIOCGABS(ABS_MT_PRESSURE), &abs_press) >= 0;
    int has_major    = ioctl(real, EVIOCGABS(ABS_MT_TOUCH_MAJOR), &abs_major) >= 0;

    if (ioctl(real, EVIOCGABS(ABS_MT_POSITION_X), &abs_x) < 0) return -1;
    if (ioctl(real, EVIOCGABS(ABS_MT_POSITION_Y), &abs_y) < 0) return -1;
    if (ioctl(real, EVIOCGABS(ABS_MT_SLOT), &abs_slot) < 0) return -1;
    if (ioctl(real, EVIOCGABS(ABS_MT_TRACKING_ID), &abs_track) < 0) return -1;

    ioctl(ui, UI_SET_EVBIT, EV_SYN);
    ioctl(ui, UI_SET_EVBIT, EV_ABS);
    ioctl(ui, UI_SET_EVBIT, EV_KEY);
    ioctl(ui, UI_SET_KEYBIT, BTN_TOUCH);

    ioctl(ui, UI_SET_ABSBIT, ABS_MT_SLOT);
    ioctl(ui, UI_SET_ABSBIT, ABS_MT_TRACKING_ID);
    ioctl(ui, UI_SET_ABSBIT, ABS_MT_POSITION_X);
    ioctl(ui, UI_SET_ABSBIT, ABS_MT_POSITION_Y);
    if (has_pressure) ioctl(ui, UI_SET_ABSBIT, ABS_MT_PRESSURE);
    if (has_major)    ioctl(ui, UI_SET_ABSBIT, ABS_MT_TOUCH_MAJOR);

#ifdef INPUT_PROP_DIRECT
    ioctl(ui, UI_SET_PROPBIT, INPUT_PROP_DIRECT);
#endif

    memset(&uidev, 0, sizeof(uidev));
    strncpy(uidev.name, "TouchGuard Virtual Touchscreen", UINPUT_MAX_NAME_SIZE - 1);
    uidev.id.bustype = BUS_VIRTUAL;
    uidev.id.vendor  = 0x1234;
    uidev.id.product = 0x5678;
    uidev.id.version = 1;

    uidev.absmin[ABS_MT_SLOT] = abs_slot.minimum;
    uidev.absmax[ABS_MT_SLOT] = abs_slot.maximum;
    uidev.absmin[ABS_MT_TRACKING_ID] = abs_track.minimum;
    uidev.absmax[ABS_MT_TRACKING_ID] = abs_track.maximum;
    uidev.absmin[ABS_MT_POSITION_X] = abs_x.minimum;
    uidev.absmax[ABS_MT_POSITION_X] = abs_x.maximum;
    uidev.absmin[ABS_MT_POSITION_Y] = abs_y.minimum;
    uidev.absmax[ABS_MT_POSITION_Y] = abs_y.maximum;
    if (has_pressure) {
        uidev.absmin[ABS_MT_PRESSURE] = abs_press.minimum;
        uidev.absmax[ABS_MT_PRESSURE] = abs_press.maximum;
    }
    if (has_major) {
        uidev.absmin[ABS_MT_TOUCH_MAJOR] = abs_major.minimum;
        uidev.absmax[ABS_MT_TOUCH_MAJOR] = abs_major.maximum;
    }

    if (write(ui, &uidev, sizeof(uidev)) < 0) return -1;
    if (ioctl(ui, UI_DEV_CREATE) < 0) return -1;

    return 0;
}

static int count_visible(int slot_active[MAX_SLOTS], int slot_suppressed[MAX_SLOTS]) {
    int c = 0;
    for (int s = 0; s < MAX_SLOTS; s++) {
        if (slot_active[s] && !slot_suppressed[s]) c++;
    }
    return c;
}

static void release_all_slots(int ui, int slot_active[MAX_SLOTS], int slot_suppressed[MAX_SLOTS], int verbose_log) {
    for (int s = 0; s < MAX_SLOTS; s++) {
        if (!slot_active[s] || slot_suppressed[s]) continue;
        if (verbose_log) {
            char vb[64];
            snprintf(vb, sizeof(vb), "[v] virtual release: slot %d (burst-block clear)\n", s);
            log_ring_write(vb);
        }
        struct input_event sel, rel;
        memset(&sel, 0, sizeof(sel));
        memset(&rel, 0, sizeof(rel));
        sel.type = EV_ABS; sel.code = ABS_MT_SLOT; sel.value = s;
        rel.type = EV_ABS; rel.code = ABS_MT_TRACKING_ID; rel.value = -1;
        emit(ui, &sel);
        emit(ui, &rel);
    }
    struct input_event up, syn;
    memset(&up, 0, sizeof(up));
    memset(&syn, 0, sizeof(syn));
    up.type = EV_KEY; up.code = BTN_TOUCH; up.value = 0;
    syn.type = EV_SYN; syn.code = SYN_REPORT; syn.value = 0;
    emit(ui, &up);
    emit(ui, &syn);
}

static void check_stale_slots(int ui, int slot_active[MAX_SLOTS], int slot_suppressed[MAX_SLOTS],
                               long long slot_start_ms[MAX_SLOTS],
                               int slot_start_x[MAX_SLOTS], int slot_start_y[MAX_SLOTS],
                               int slot_cur_x[MAX_SLOTS], int slot_cur_y[MAX_SLOTS],
                               int slot_in_top_zone[MAX_SLOTS],
                               long long stale_ms, int stale_px, long long top_zone_stale_ms,
                               int verbose_log) {
    long long t = now_ms();
    for (int s = 0; s < MAX_SLOTS; s++) {
        if (!slot_active[s] || slot_suppressed[s]) continue;
        if (slot_start_x[s] == INT_MIN) continue;

        /* A contact that started in the configured top region (notification
         * shade / status bar area) gets a much shorter leash. A real
         * interaction up there is almost always a quick tap or swipe, not a
         * long motionless hold -- so this doesn't meaningfully affect
         * genuine use, but lets a ghost that likes to show up there get
         * caught in ~1-2s instead of sharing the full press-and-hold
         * allowance the rest of the screen gets. */
        long long applicable_stale_ms = (slot_in_top_zone[s] == 1) ? top_zone_stale_ms : stale_ms;

        long long age = t - slot_start_ms[s];
        if (age < applicable_stale_ms) continue;

        int dx = slot_cur_x[s] - slot_start_x[s];
        int dy = slot_cur_y[s] - slot_start_y[s];
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dx > stale_px || dy > stale_px) continue;

        /* Stale detected — log to ring, hide it */
        char buf[160];
        snprintf(buf, sizeof(buf), "slot %d stale after %lldms with no movement%s, hiding it\n",
                 s, age, slot_in_top_zone[s] == 1 ? " (top zone)" : "");
        log_ring_write(buf);
        if (verbose_log) {
            char vb[64];
            snprintf(vb, sizeof(vb), "[v] virtual release: slot %d (stale-hide)\n", s);
            log_ring_write(vb);
        }

        struct input_event sel, rel;
        memset(&sel, 0, sizeof(sel));
        memset(&rel, 0, sizeof(rel));
        sel.type = EV_ABS; sel.code = ABS_MT_SLOT; sel.value = s;
        rel.type = EV_ABS; rel.code = ABS_MT_TRACKING_ID; rel.value = -1;
        emit(ui, &sel);
        emit(ui, &rel);
        slot_suppressed[s] = 1;

        if (count_visible(slot_active, slot_suppressed) == 0) {
            struct input_event up, syn;
            memset(&up, 0, sizeof(up));
            memset(&syn, 0, sizeof(syn));
            up.type = EV_KEY; up.code = BTN_TOUCH; up.value = 0;
            syn.type = EV_SYN; syn.code = SYN_REPORT; syn.value = 0;
            emit(ui, &up);
            emit(ui, &syn);
        } else {
            struct input_event syn;
            memset(&syn, 0, sizeof(syn));
            syn.type = EV_SYN; syn.code = SYN_REPORT; syn.value = 0;
            emit(ui, &syn);
        }
    }
}

static void force_clear_if_stuck(int slot_active[MAX_SLOTS], int slot_suppressed[MAX_SLOTS],
                                  int *blocked, long long blocked_since_ms,
                                  long long max_block_ms) {
    if (!*blocked) return;
    if (now_ms() - blocked_since_ms < max_block_ms) return;

    char buf[128];
    snprintf(buf, sizeof(buf), "blocked for over %lldms, forcing clear as a failsafe\n", max_block_ms);
    log_ring_write(buf);
    
    for (int s = 0; s < MAX_SLOTS; s++) {
        if (slot_active[s] && !slot_suppressed[s]) slot_suppressed[s] = 1;
    }
    *blocked = 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s /dev/input/eventN\n", argv[0]);
        return 1;
    }

    install_signal_handlers();

    real_fd = open(argv[1], O_RDONLY);
    if (real_fd < 0) {
        fprintf(stderr, "cannot open %s: %s\n", argv[1], strerror(errno));
        return 1;
    }

    ui_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (ui_fd < 0) {
        fprintf(stderr, "cannot open /dev/uinput: %s\n", strerror(errno));
        close(real_fd);
        return 1;
    }

    if (setup_uinput(real_fd, ui_fd) < 0) {
        fprintf(stderr, "uinput setup failed: %s\n", strerror(errno));
        do_cleanup();
        return 1;
    }

    if (ioctl(real_fd, EVIOCGRAB, 1) < 0) {
        fprintf(stderr, "EVIOCGRAB failed: %s\n", strerror(errno));
        do_cleanup();
        return 1;
    }

    usleep(200000);

    /* Open log file for ring buffer flush */
    log_fp = fopen(LOG_FILE, "a");
    if (!log_fp) {
        fprintf(stderr, "cannot open log: %s\n", strerror(errno));
        do_cleanup();
        return 1;
    }

    int max_fingers = read_int_config(MAX_FINGERS_CONF, DEFAULT_MAX_FINGERS, 1, 9);
    long long stale_ms = (long long)read_int_config(STALE_MS_CONF, DEFAULT_STALE_MS, 500, 120000);
    int stale_px = read_int_config(STALE_PX_CONF, DEFAULT_STALE_PX, 0, 500);
    long long max_block_ms = (long long)read_int_config(MAX_BLOCK_MS_CONF, DEFAULT_MAX_BLOCK_MS, 500, 30000);
    int top_zone_pct = read_int_config(TOP_ZONE_PCT_CONF, DEFAULT_TOP_ZONE_PCT, 0, 100);
    long long top_zone_stale_ms = (long long)read_int_config(TOP_ZONE_STALE_MS_CONF, DEFAULT_TOP_ZONE_STALE_MS, 200, 60000);
    long long wake_grace_ms = (long long)read_int_config(WAKE_GRACE_MS_CONF, DEFAULT_WAKE_GRACE_MS, 500, 60000);
    int verbose_log = read_int_config(VERBOSE_LOG_CONF, DEFAULT_VERBOSE_LOG, 0, 1);

    char backlight_path[256];
    int have_backlight = probe_backlight_path(backlight_path, sizeof(backlight_path));
    int screen_on_state = 1;      /* assume on at startup, avoid a false trigger immediately */
    int watching_for_wake_stall = 0;
    long long screen_on_since_ms = 0;
    long long last_backlight_check = 0;
    if (have_backlight) {
        int v = read_screen_on(backlight_path);
        if (v >= 0) screen_on_state = v;
    }

    /* Work out where "top zone" ends in the panel's own raw coordinate
     * space (not pixels -- these don't always match screen resolution
     * 1:1), so this scales correctly regardless of panel/resolution. */
    struct input_absinfo abs_y_range;
    int top_zone_y_max = INT_MIN; /* if the query fails, nothing qualifies -- feature just no-ops */
    if (ioctl(real_fd, EVIOCGABS(ABS_MT_POSITION_Y), &abs_y_range) == 0) {
        top_zone_y_max = abs_y_range.minimum +
            (int)(((long long)(abs_y_range.maximum - abs_y_range.minimum) * top_zone_pct) / 100);
    }

    fprintf(stderr, "touchguard active on %s: max %d finger%s, stale timeout %lldms/%dpx, "
            "top-zone timeout %lldms (top %d%%), block failsafe %lldms, wake watchdog %s\n",
            argv[1], max_fingers, max_fingers == 1 ? "" : "s", stale_ms, stale_px,
            top_zone_stale_ms, top_zone_pct, max_block_ms,
            have_backlight ? "active" : "unavailable (no backlight path found)");

    int slot_active[MAX_SLOTS], slot_suppressed[MAX_SLOTS], slot_in_top_zone[MAX_SLOTS];
    long long slot_start_ms[MAX_SLOTS];
    int slot_start_x[MAX_SLOTS], slot_start_y[MAX_SLOTS];
    int slot_cur_x[MAX_SLOTS], slot_cur_y[MAX_SLOTS];
    memset(slot_active, 0, sizeof(slot_active));
    memset(slot_suppressed, 0, sizeof(slot_suppressed));
    memset(slot_in_top_zone, 0, sizeof(slot_in_top_zone));
    for (int s = 0; s < MAX_SLOTS; s++) {
        slot_start_ms[s] = 0;
        slot_start_x[s] = slot_start_y[s] = INT_MIN;
        slot_cur_x[s] = slot_cur_y[s] = 0;
    }

    int cur_slot = 0;
    int blocked = 0;
    long long blocked_since_ms = 0;
    int event_count = 0;  /* for lazy stale checking */
    long long last_stale_check = now_ms();
    long long last_real_event_ms = now_ms();

    struct input_event ev;
    struct input_event frame_buf[512];
    int frame_slot[512];
    int frame_len = 0;

    struct pollfd pfd;
    pfd.fd = real_fd;
    pfd.events = POLLIN;

    while (running) {
        if (access(DISABLE_FLAG, F_OK) == 0) {
            fprintf(stderr, "disable flag present, exiting cleanly\n");
            break;
        }

        /* Lazy stale check: every 100 events or every 5 seconds */
        long long now = now_ms();
        if (event_count >= 100 || (now - last_stale_check >= 5000)) {
            check_stale_slots(ui_fd, slot_active, slot_suppressed, slot_start_ms,
                               slot_start_x, slot_start_y, slot_cur_x, slot_cur_y,
                               slot_in_top_zone, stale_ms, stale_px, top_zone_stale_ms, verbose_log);
            force_clear_if_stuck(slot_active, slot_suppressed, &blocked, blocked_since_ms, max_block_ms);
            event_count = 0;
            last_stale_check = now;
        }

        /* Wake-stall watchdog: checked on its own ~1s cadence (cheaper than
         * the stale check, and needs to notice a wake promptly). Some
         * touch controllers don't cleanly resume after the screen sleeps
         * and wakes -- this device's own driver appears to be one of them.
         * If the screen turns back on and NO real touch data arrives for
         * a full grace period despite that, this daemon's connection to
         * the real device is almost certainly stale. Rather than try to
         * repair that in-place, exit cleanly (which already releases
         * everything correctly) and let the supervisor bring up a fresh
         * instance with a fresh open+grab -- far simpler and far more
         * reliable than hand-rolled in-process re-initialization. */
        if (have_backlight && (now - last_backlight_check >= 1000)) {
            int v = read_screen_on(backlight_path);
            last_backlight_check = now;
            if (v >= 0) {
                if (v == 1 && screen_on_state == 0) {
                    /* off -> on transition just happened */
                    watching_for_wake_stall = 1;
                    screen_on_since_ms = now;
                }
                screen_on_state = v;
            }
            if (watching_for_wake_stall) {
                if (last_real_event_ms > screen_on_since_ms) {
                    watching_for_wake_stall = 0; /* real data arrived, all is well */
                } else if (now - screen_on_since_ms >= wake_grace_ms) {
                    fprintf(stderr, "screen woke but no touch data arrived after %lldms, "
                                    "restarting to force a clean resync with the touch driver\n",
                            wake_grace_ms);
                    running = 0;
                }
            }
        }

        /* Flush logs every 10 seconds */
        log_flush_if_needed();

        /* Poll with 200ms timeout instead of 2000ms — keeps stale detection responsive */
        int pr = poll(&pfd, 1, 200);
        if (pr < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "poll error: %s\n", strerror(errno));
            break;
        }
        if (pr == 0) continue;
        if (!(pfd.revents & POLLIN)) continue;

        ssize_t n = read(real_fd, &ev, sizeof(ev));
        if (n != (ssize_t)sizeof(ev)) {
            if (errno == EINTR) continue;
            fprintf(stderr, "read ended: %s\n", strerror(errno));
            break;
        }

        event_count++;
        last_real_event_ms = now_ms();

        if (ev.type == EV_ABS && ev.code == ABS_MT_SLOT) {
            cur_slot = ev.value;
            if (cur_slot < 0 || cur_slot >= MAX_SLOTS) cur_slot = 0;
        }

        if (frame_len < 512) {
            frame_buf[frame_len] = ev;
            frame_slot[frame_len] = cur_slot;
            frame_len++;
        }

        if (ev.type == EV_ABS && ev.code == ABS_MT_TRACKING_ID) {
            if (ev.value == -1) {
                if (verbose_log) {
                    char vb[64];
                    snprintf(vb, sizeof(vb), "[v] real release: slot %d\n", cur_slot);
                    log_ring_write(vb);
                }
                slot_active[cur_slot] = 0;
                slot_suppressed[cur_slot] = 0;
            } else if (!slot_active[cur_slot]) {
                if (verbose_log) {
                    char vb[64];
                    snprintf(vb, sizeof(vb), "[v] real touch-down: slot %d\n", cur_slot);
                    log_ring_write(vb);
                }
                slot_active[cur_slot] = 1;
                slot_suppressed[cur_slot] = 0;
                slot_start_ms[cur_slot] = now_ms();
                slot_start_x[cur_slot] = INT_MIN;
                slot_start_y[cur_slot] = INT_MIN;
            }
        } else if (ev.type == EV_ABS && ev.code == ABS_MT_POSITION_X) {
            slot_cur_x[cur_slot] = ev.value;
            if (slot_start_x[cur_slot] == INT_MIN) slot_start_x[cur_slot] = ev.value;
        } else if (ev.type == EV_ABS && ev.code == ABS_MT_POSITION_Y) {
            slot_cur_y[cur_slot] = ev.value;
            if (slot_start_y[cur_slot] == INT_MIN) {
                slot_start_y[cur_slot] = ev.value;
                slot_in_top_zone[cur_slot] =
                    (top_zone_y_max != INT_MIN && ev.value <= top_zone_y_max) ? 1 : 0;
            }
        }

        if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
            int visible = count_visible(slot_active, slot_suppressed);

            if (visible > max_fingers) {
                if (!blocked) {
                    char buf[128];
                    snprintf(buf, sizeof(buf), "%d visible fingers (max allowed %d), blocking until full release\n",
                            visible, max_fingers);
                    log_ring_write(buf);
                    release_all_slots(ui_fd, slot_active, slot_suppressed, verbose_log);
                    blocked = 1;
                    blocked_since_ms = now_ms();
                }
            } else if (!blocked) {
                /* Always explicitly re-assert slot before forwarding */
                struct input_event resync;
                memset(&resync, 0, sizeof(resync));
                resync.type = EV_ABS; resync.code = ABS_MT_SLOT; resync.value = cur_slot;
                emit(ui_fd, &resync);

                int syn_sent = 0;
                for (int i = 0; i < frame_len; i++) {
                    int s = frame_slot[i];
                    if (frame_buf[i].type != EV_SYN && s >= 0 && s < MAX_SLOTS && slot_suppressed[s]) {
                        continue;
                    }
                    if (verbose_log && frame_buf[i].type == EV_ABS &&
                        frame_buf[i].code == ABS_MT_TRACKING_ID && frame_buf[i].value == -1) {
                        char vb[64];
                        snprintf(vb, sizeof(vb), "[v] virtual release: slot %d (forwarded)\n", s);
                        log_ring_write(vb);
                    }
                    emit(ui_fd, &frame_buf[i]);
                    if (frame_buf[i].type == EV_SYN && frame_buf[i].code == SYN_REPORT) syn_sent = 1;
                }
                if (!syn_sent) {
                    struct input_event syn;
                    memset(&syn, 0, sizeof(syn));
                    syn.type = EV_SYN; syn.code = SYN_REPORT; syn.value = 0;
                    emit(ui_fd, &syn);
                }
            }

            if (visible == 0 && blocked) {
                log_ring_write("all visible fingers released, resuming pass-through\n");
                blocked = 0;
            }

            frame_len = 0;
        }
    }

    do_cleanup();
    fprintf(stderr, "touchguard stopped\n");
    return 0;
}
