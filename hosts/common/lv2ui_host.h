/*
 * lv2ui_host — shows an LV2 plug-in's own UI for a plug-in running in
 * lv2host_core. Shared by lv2guihost and the apps built on this stack
 * (rktuner).
 *
 * Two UI kinds, HaikuUI preferred:
 *  - HaikuUI (<https://github.com/rations/LV2-haiku/ns/ui-haiku#HaikuUI>): the
 *    UI's widget is a detached BView* that the caller adds to its own window.
 *  - ui:showInterface (any UI type): the UI owns its own window; Show() opens
 *    it.
 *
 * UI -> RT: the UI's write_function marshals control floats into the core's
 * lock-free ring. RT -> UI: Tick(), called ~30 Hz (kLv2UiTickUsec), compares
 * the control store and delivers changes through port_event(), and drives
 * idle(). No UI code runs on, or blocks, the RT thread.
 *
 * Threading: Tick() and Close() run on the looper that shows the UI (the
 * window holding View(), or the application for a showInterface UI), with
 * that looper locked, as the HaikuUI contract requires. Destroy the object
 * only after every window showing View() is gone: the view's code lives in
 * the UI binary that the destructor unloads.
 */
#ifndef LV2UI_HOST_H
#define LV2UI_HOST_H

#include "lv2host_core.h"

#include <SupportDefs.h>
#include <lv2/ui/ui.h>

class BView;

static const bigtime_t kLv2UiTickUsec = 33000; /* ~30 Hz */

class Lv2UiHost
{
public:
    explicit Lv2UiHost(Lv2Host *host);
    ~Lv2UiHost();

    /* Pick the plug-in's UI and instantiate it. false, with Error() set, if
       it has no usable UI or loading fails. Non-RT; on the looper that will
       show it. */
    bool Open(const char *plugin_uri);

    /* HaikuUI: the widget to add to a window (the window then owns it until
       Close()). NULL for a showInterface UI. */
    BView *View() const
    {
        return fView;
    }
    bool IsEmbedded() const
    {
        return fView != NULL;
    }

    /* showInterface UIs: open the UI's own window. */
    bool Show();

    /* Deliver control changes and drive idle(). Returns false once the UI
       asked to close (idle() returned non-zero). */
    bool Tick();

    /* hide() and cleanup(); after this View() is NULL. Safe to call twice. */
    void Close();

    const char *Error() const
    {
        return fError;
    }

private:
    bool LoadBinary(const LilvUI *ui);
    bool FeaturesOk(const LilvUI *ui);
    void Fail(const char *fmt, ...);

    Lv2Host *fHost;
    void *fDl;
    const LV2UI_Descriptor *fDesc;
    LV2UI_Handle fHandle;
    BView *fView;
    const LV2UI_Show_Interface *fShowIface;
    const LV2UI_Idle_Interface *fIdleIface;
    bool fShown;
    float *fLast; /* last value delivered per param, then per control output */
    char fError[256];
};

#endif /* LV2UI_HOST_H */
