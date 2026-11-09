/*
 * lv2guihost — native-GUI LV2 host for RenkuOS: runs one plug-in on the
 * Media Kit (the default) or JACK (--backend jack), through the shared
 * hosting core (lv2host_core.h), and shows its own UI (lv2ui_host.h).
 *
 * Two UI paths:
 *  - HaikuUI: the UI's BView goes into this host's window; port_event() and
 *    cleanup() run on that window's looper with the looper locked.
 *  - ui:showInterface (any UI type): the UI owns its own window; the
 *    application's looper drives idle() until it returns non-zero.
 *
 * Threading: the BApplication exists before the audio client is opened (the
 * Media Kit needs it) and outlives it; the host and the UI are opened in
 * ReadyToRun on the application thread. The plug-in runs on the backend's
 * RT thread; no UI code ever runs on, or blocks, it.
 */
#include "../common/lv2host_core.h"
#include "../common/lv2ui_host.h"

#include <Application.h>
#include <GroupLayout.h>
#include <Message.h>
#include <MessageRunner.h>
#include <Messenger.h>
#include <View.h>
#include <Window.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint32 kMsgTick = 'tick';

static volatile sig_atomic_t quit_requested = 0;

static void quit_signal_handler(int sig)
{
    (void)sig;
    quit_requested = 1;
}

/* ---- Window hosting a HaikuUI BView ---- */

class GuiWindow : public BWindow
{
public:
    GuiWindow(const char *title, Lv2UiHost *ui)
        : BWindow(BRect(120, 120, 520, 260), title, B_TITLED_WINDOW,
                  B_AUTO_UPDATE_SIZE_LIMITS | B_NOT_ZOOMABLE),
          fUi(ui), fRunner(NULL)
    {
        SetLayout(new BGroupLayout(B_VERTICAL));
        AddChild(fUi->View());
        fRunner = new BMessageRunner(BMessenger(this), new BMessage(kMsgTick), kLv2UiTickUsec);
    }

    ~GuiWindow() override
    {
        delete fRunner;
    }

    void MessageReceived(BMessage *msg) override
    {
        if (msg->what == kMsgTick) {
            if (quit_requested || !fUi->Tick()) {
                PostMessage(B_QUIT_REQUESTED);
            }
            return;
        }
        BWindow::MessageReceived(msg);
    }

    bool QuitRequested() override
    {
        /* Looper locked: clean the UI up per the HaikuUI contract (it takes
           its view with it), then quit the app. */
        fUi->Close();
        be_app->PostMessage(B_QUIT_REQUESTED);
        return true;
    }

private:
    Lv2UiHost *fUi;
    BMessageRunner *fRunner;
};

/* ---- The application: opens the host and its UI ---- */

class GuiHostApp : public BApplication
{
public:
    GuiHostApp(const char *uri, const char *backend)
        : BApplication("application/x-vnd.rations-lv2guihost"), fUri(uri), fBackend(backend),
          fUi(NULL), fRunner(NULL), fOpen(false), fRc(1)
    {
    }

    ~GuiHostApp() override
    {
        delete fRunner;
        /* Every window is gone by now: safe to unload the UI binary. */
        delete fUi;
        if (fOpen) {
            unsigned xruns = fHost.n_xruns;
            lv2host_close(&fHost);
            printf("xruns while running: %u\n", xruns);
        }
    }

    int Result() const
    {
        return fRc;
    }

    void ReadyToRun() override
    {
        const Lv2hOpenOptions opt = {fBackend, "lv2host", "LV2HOST_BACKEND_DIR"};
        if (!lv2host_open(&fHost, fUri, "lv2guihost", &opt)) {
            Quit(); /* lv2host_open printed why */
            return;
        }
        fOpen = true;
        lv2host_autoconnect(&fHost);

        LilvNode *name = lilv_plugin_get_name(fHost.plugin);
        printf("loaded: %s <%s>\n", name ? lilv_node_as_string(name) : "?", fUri);
        printf("audio: %d in, %d out%s; %s, %.0f Hz, period %u\n", fHost.n_audio_in,
               fHost.n_audio_out, fHost.dual_mono ? " (dual-mono x2)" : "",
               lv2host_backend_name(&fHost), fHost.sample_rate, lv2host_period(&fHost));

        fUi = new Lv2UiHost(&fHost);
        if (!fUi->Open(fUri)) {
            if (name) {
                lilv_node_free(name);
            }
            Quit(); /* Open printed why */
            return;
        }
        if (fUi->IsEmbedded()) {
            printf("UI: HaikuUI (embedded BView)\n");
            GuiWindow *win = new GuiWindow(name ? lilv_node_as_string(name) : "LV2 plugin", fUi);
            win->Show();
        } else {
            printf("UI: ui:showInterface (the UI owns its window)\n");
            if (!fUi->Show()) {
                if (name) {
                    lilv_node_free(name);
                }
                Quit();
                return;
            }
            fRunner = new BMessageRunner(BMessenger(this), new BMessage(kMsgTick), kLv2UiTickUsec);
        }
        if (name) {
            lilv_node_free(name);
        }
        fRc = 0;
    }

    void MessageReceived(BMessage *msg) override
    {
        if (msg->what == kMsgTick) { /* showInterface UIs only */
            if (quit_requested || !fUi->Tick()) {
                PostMessage(B_QUIT_REQUESTED);
            }
            return;
        }
        BApplication::MessageReceived(msg);
    }

    bool QuitRequested() override
    {
        if (fUi && !fUi->IsEmbedded()) {
            fUi->Close(); /* showInterface: the app's looper owns the UI */
        }
        return BApplication::QuitRequested(); /* asks the window too */
    }

private:
    const char *fUri;
    const char *fBackend;
    Lv2Host fHost;
    Lv2UiHost *fUi;
    BMessageRunner *fRunner;
    bool fOpen;
    int fRc;
};

static void usage(void)
{
    fprintf(stderr, "usage: lv2guihost [--backend mediakit|jack] <plugin-uri>\n");
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

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = quit_signal_handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGTERM, &sa, NULL) != 0 || sigaction(SIGINT, &sa, NULL) != 0 ||
        sigaction(SIGHUP, &sa, NULL) != 0) {
        fprintf(stderr, "warning: failed to install signal handlers\n");
    }

    GuiHostApp *app = new GuiHostApp(uri, backend);
    app->Run();
    int rc = app->Result();
    delete app; /* closes the UI binary, then the audio client */
    return rc;
}
