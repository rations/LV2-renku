/*
 * lv2host — headless LV2 host: one plug-in as a stereo client of the RenkuOS
 * Media Kit (a media node, the default) or of JACK (--backend jack, through
 * the jack.so add-on), with a stdin REPL for its parameters and inputs. For
 * running and validating LV2 plug-ins without a DAW. The hosting itself lives
 * in the shared core (lv2host_core.h).
 *
 * Threading: main() owns a BApplication (the Media Kit needs one; its looper
 * does not need to run, but it must outlive the audio client) and runs the
 * REPL; the backend runs the plug-in on its own RT thread.
 */
#include "../common/lv2host_core.h"

#include <Application.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_params(const Lv2Host *h)
{
    printf("%u parameter(s):\n", h->n_params);
    for (uint32_t i = 0; i < h->n_params; i++) {
        const Lv2hParam *pr = &h->params[i];
        printf("  [%u] %-24s min %-9g max %-9g def %-9g cur %-9g", i, pr->name, pr->min, pr->max,
               pr->def, lv2host_get_control(h, pr->port_index));
        if (pr->toggled) {
            printf(" [toggled]");
        }
        if (pr->integer) {
            printf(" [integer]");
        }
        if (pr->enumeration) {
            printf(" [enum]");
        }
        if (pr->n_scale_points) {
            printf(" [%u scale points]", pr->n_scale_points);
        }
        printf("\n");
    }
}

static void print_outs(const Lv2Host *h)
{
    printf("%u control output(s):\n", h->n_outs);
    for (uint32_t i = 0; i < h->n_outs; i++) {
        const Lv2hParam *pr = &h->outs[i];
        printf("  [%u] %-24s min %-9g max %-9g cur %-9g\n", i, pr->name, pr->min, pr->max,
               lv2host_get_control(h, pr->port_index));
    }
}

/* SIGTERM/SIGINT/SIGHUP request a clean shutdown: the handler only sets a
   flag; installed without SA_RESTART so the blocking REPL fgets() returns
   with EINTR and main() runs the normal teardown (lv2host_close must happen:
   a killed JACK client leaves the server graph wedged, and a media node
   stays registered). */
static volatile sig_atomic_t quit_requested = 0;

static void quit_signal_handler(int sig)
{
    (void)sig;
    quit_requested = 1;
}

static const char kHelp[] =
    "commands: list | get N | set N VALUE | out | inputs | connect N [1|2] |"
    " disconnect N [1|2] | status | quit\n";

/* The audio sources the inputs can be connected from, numbered for connect. */
static void print_sources(Lv2Host *h)
{
    const char **src = lv2host_get_sources(h);
    if (!src || !src[0]) {
        printf("no audio sources\n");
    } else {
        for (int i = 0; src[i]; i++) {
            printf("  [%d] %s\n", i, src[i]);
        }
    }
    lv2host_free_sources(h, src);
}

/* connect / disconnect source number `n` to input 1 or 2. */
static void connect_source(Lv2Host *h, int n, int input, bool connect)
{
    const char **src = lv2host_get_sources(h);
    int count = 0;
    while (src && src[count]) {
        count++;
    }
    if (n < 0 || n >= count) {
        printf("no such source %d (see inputs)\n", n);
    } else {
        int r = connect ? lv2host_connect_input(h, src[n], input - 1)
                        : lv2host_disconnect_input(h, src[n], input - 1);
        printf("%s %s %s in_%d%s\n", connect ? "connect" : "disconnect", src[n],
               connect ? "->" : "-x", input, r == 0 ? "" : ": failed");
    }
    lv2host_free_sources(h, src);
}

static void print_status(const Lv2Host *h)
{
    printf("%s, %.0f Hz, period %u, xruns %u%s\n", lv2host_backend_name(h), h->sample_rate,
           lv2host_period(h), h->n_xruns, h->backend_gone ? ", the audio system went away" : "");
}

static void repl(Lv2Host *h)
{
    char line[256];
    printf("%s", kHelp);
    while (!quit_requested && fgets(line, sizeof(line), stdin)) {
        unsigned idx = 0;
        float val = 0.0f;
        if (!strncmp(line, "quit", 4) || !strncmp(line, "exit", 4)) {
            break;
        }
        if (!strncmp(line, "list", 4)) {
            print_params(h);
        } else if (!strncmp(line, "out", 3)) {
            print_outs(h);
        } else if (sscanf(line, "set %u %f", &idx, &val) == 2) {
            if (!lv2host_set_param(h, idx, val)) {
                printf("no such parameter %u\n", idx);
                continue;
            }
            const Lv2hParam *pr = &h->params[idx];
            if (val < pr->min) {
                val = pr->min;
            }
            if (val > pr->max) {
                val = pr->max;
            }
            printf("%s = %g\n", pr->name, val);
        } else if (!strncmp(line, "inputs", 6)) {
            print_sources(h);
        } else if (!strncmp(line, "status", 6)) {
            print_status(h);
        } else if (!strncmp(line, "connect", 7) || !strncmp(line, "disconnect", 10)) {
            bool connect = line[0] == 'c';
            int n = -1, input = 1;
            int got = sscanf(line + (connect ? 7 : 10), "%d %d", &n, &input);
            if (got < 1 || input < 1 || input > 2) {
                printf("usage: %s N [1|2]  (N from inputs)\n", connect ? "connect" : "disconnect");
            } else {
                connect_source(h, n, input, connect);
            }
        } else if (sscanf(line, "get %u", &idx) == 1) {
            if (idx >= h->n_params) {
                printf("no such parameter %u\n", idx);
                continue;
            }
            printf("%s = %g\n", h->params[idx].name,
                   lv2host_get_control(h, h->params[idx].port_index));
        } else if (line[0] != '\n' && line[0] != '\0') {
            printf("%s", kHelp);
        }
        if (h->block_too_big) {
            printf("warning: the period grew past the pre-allocated block; "
                   "output muted — restart the host\n");
        }
        if (h->backend_gone) {
            printf("the audio system went away; quit and start again\n");
        }
    }
}

static void usage(void)
{
    fprintf(stderr, "usage: lv2host [--backend mediakit|jack] <plugin-uri>\n");
    fprintf(stderr, "       (list installed plugins with lv2ls)\n");
}

int main(int argc, char **argv)
{
    const char *backend = "mediakit";
    const char *uri = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--backend") && i + 1 < argc) {
            backend = argv[++i];
            if (strcmp(backend, "mediakit") && strcmp(backend, "jack")) {
                usage();
                return 1;
            }
        } else if (argv[i][0] != '-' && !uri) {
            uri = argv[i];
        } else {
            usage();
            return 1;
        }
    }
    if (!uri) {
        usage();
        return 1;
    }

    /* The Media Kit needs a BApplication; it outlives the audio client. */
    BApplication app("application/x-vnd.rations-lv2host");

    static Lv2Host h;
    const Lv2hOpenOptions opt = {backend, "lv2host", "LV2HOST_BACKEND_DIR"};
    if (!lv2host_open(&h, uri, "lv2host", &opt)) {
        return 1; /* lv2host_open printed why */
    }
    lv2host_autoconnect(&h);

    LilvNode *name = lilv_plugin_get_name(h.plugin);
    printf("loaded: %s <%s>\n", name ? lilv_node_as_string(name) : "?", uri);
    if (name) {
        lilv_node_free(name);
    }
    printf("audio: %d in, %d out%s; %s, %.0f Hz, period %u (block up to %u)\n", h.n_audio_in,
           h.n_audio_out, h.dual_mono ? " (dual-mono x2)" : "", lv2host_backend_name(&h),
           h.sample_rate, lv2host_period(&h), h.max_block);
    print_params(&h);
    if (h.n_outs) {
        print_outs(&h);
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = quit_signal_handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGTERM, &sa, NULL) != 0 || sigaction(SIGINT, &sa, NULL) != 0 ||
        sigaction(SIGHUP, &sa, NULL) != 0) {
        fprintf(stderr, "warning: failed to install signal handlers\n");
    }

    repl(&h);

    unsigned xruns = h.n_xruns;
    lv2host_close(&h);
    printf("xruns while running: %u\n", xruns);
    return 0;
}
