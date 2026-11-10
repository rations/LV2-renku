/* lv2ui_host — an LV2 plug-in's own UI for lv2host_core (see lv2ui_host.h). */
#include "lv2ui_host.h"

#include "../../ns/ui-haiku.lv2/ui_haiku.h"

#include <View.h>

#include <dlfcn.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* write_function handed to the UI: control floats go through the core's
   lock-free ring; every other protocol is ignored (allow-list). */
static void ui_write_cb(LV2UI_Controller controller, uint32_t port_index, uint32_t buffer_size,
                        uint32_t protocol, const void *buffer)
{
    Lv2Host *h = (Lv2Host *)controller;
    if (protocol == 0 && buffer_size == sizeof(float) && buffer) {
        lv2host_write_control_port(h, port_index, *(const float *)buffer);
    }
}

Lv2UiHost::Lv2UiHost(Lv2Host *host)
    : fHost(host), fDl(NULL), fDesc(NULL), fHandle(NULL), fView(NULL), fShowIface(NULL),
      fIdleIface(NULL), fShown(false), fLast(NULL)
{
    fError[0] = '\0';
}

Lv2UiHost::~Lv2UiHost()
{
    Close();
    free(fLast);
    if (fDl) {
        dlclose(fDl);
    }
}

void Lv2UiHost::Fail(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (!fError[0]) {
        va_list ap2;
        va_copy(ap2, ap);
        vsnprintf(fError, sizeof fError, fmt, ap2);
        va_end(ap2);
    }
    fprintf(stderr, "error: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

/* UIs may declare lv2:requiredFeature; we only provide urid map/unmap. */
bool Lv2UiHost::FeaturesOk(const LilvUI *ui)
{
    LilvWorld *world = fHost->world;
    bool ok = true;
    LilvNode *req_pred = lilv_new_uri(world, LV2_CORE__requiredFeature);
    if (!req_pred) {
        return false;
    }
    LilvNodes *req = lilv_world_find_nodes(world, lilv_ui_get_uri(ui), req_pred, NULL);
    if (req) {
        LILV_FOREACH(nodes, it, req)
        {
            const LilvNode *f = lilv_nodes_get(req, it);
            const char *uri = lilv_node_as_uri(f);
            if (!uri || (strcmp(uri, LV2_URID__map) != 0 && strcmp(uri, LV2_URID__unmap) != 0)) {
                Fail("the plug-in's UI requires a feature this host lacks: %s", uri ? uri : "(?)");
                ok = false;
            }
        }
        lilv_nodes_free(req);
    }
    lilv_node_free(req_pred);
    return ok;
}

/* dlopen the UI binary and find the descriptor whose URI matches. */
bool Lv2UiHost::LoadBinary(const LilvUI *ui)
{
    const LilvNode *bin = lilv_ui_get_binary_uri(ui);
    if (!bin) {
        Fail("the plug-in's UI has no ui:binary");
        return false;
    }
    char *path = lilv_file_uri_parse(lilv_node_as_uri(bin), NULL);
    if (!path) {
        Fail("the plug-in's UI binary URI cannot be parsed");
        return false;
    }
    fDl = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!fDl) {
        Fail("the plug-in's UI could not be loaded: %s", dlerror());
        lilv_free(path);
        return false;
    }
    lilv_free(path);

    LV2UI_DescriptorFunction df = (LV2UI_DescriptorFunction)dlsym(fDl, "lv2ui_descriptor");
    if (!df) {
        Fail("the plug-in's UI binary has no lv2ui_descriptor");
        return false;
    }
    const char *want = lilv_node_as_uri(lilv_ui_get_uri(ui));
    for (uint32_t i = 0;; i++) {
        const LV2UI_Descriptor *d = df(i);
        if (!d) {
            break;
        }
        if (d->URI && want && !strcmp(d->URI, want)) {
            fDesc = d;
            break;
        }
    }
    if (!fDesc) {
        Fail("the UI <%s> is not in its binary", want ? want : "(?)");
        return false;
    }
    return true;
}

bool Lv2UiHost::Open(const char *plugin_uri)
{
    /* Pick a UI: prefer HaikuUI (embeddable BView); otherwise any UI that
       declares ui:showInterface (owns its own window). */
    LilvWorld *world = fHost->world;
    LilvNode *haiku_ui_class = lilv_new_uri(world, LV2_UI_HAIKU__HaikuUI);
    LilvNode *ext_data_pred = lilv_new_uri(world, LV2_CORE__extensionData);
    LilvNode *show_iface_node = lilv_new_uri(world, LV2_UI__showInterface);
    const LilvUI *chosen = NULL;
    bool chosen_haiku = false;

    LilvUIs *uis = lilv_plugin_get_uis(fHost->plugin);
    if (uis && haiku_ui_class && ext_data_pred && show_iface_node) {
        LILV_FOREACH(uis, it, uis)
        {
            const LilvUI *ui = lilv_uis_get(uis, it);
            if (lilv_ui_is_a(ui, haiku_ui_class)) {
                chosen = ui;
                chosen_haiku = true;
                break;
            }
            if (!chosen &&
                lilv_world_ask(world, lilv_ui_get_uri(ui), ext_data_pred, show_iface_node)) {
                chosen = ui;
            }
        }
    }
    lilv_node_free(haiku_ui_class);
    lilv_node_free(ext_data_pred);
    lilv_node_free(show_iface_node);

    bool ok = false;
    if (!chosen) {
        Fail("the plug-in has no HaikuUI and no ui:showInterface UI");
    } else if (FeaturesOk(chosen) && LoadBinary(chosen)) {
        const LilvNode *bundle = lilv_ui_get_bundle_uri(chosen);
        char *bundle_path = bundle ? lilv_file_uri_parse(lilv_node_as_uri(bundle), NULL) : NULL;

        const LV2_Feature feat_map = {LV2_URID__map, lv2host_urid_map()};
        const LV2_Feature feat_unmap = {LV2_URID__unmap, lv2host_urid_unmap()};
        const LV2_Feature *feats[] = {&feat_map, &feat_unmap, NULL};

        LV2UI_Widget widget = NULL;
        fHandle = fDesc->instantiate(fDesc, plugin_uri, bundle_path ? bundle_path : "", ui_write_cb,
                                     (LV2UI_Controller)fHost, &widget, feats);
        if (bundle_path) {
            lilv_free(bundle_path);
        }
        if (!fHandle) {
            Fail("the plug-in's UI could not be instantiated");
        } else {
            if (fDesc->extension_data) {
                fShowIface =
                    (const LV2UI_Show_Interface *)fDesc->extension_data(LV2_UI__showInterface);
                fIdleIface =
                    (const LV2UI_Idle_Interface *)fDesc->extension_data(LV2_UI__idleInterface);
            }
            /* A HaikuUI hands back its BView; a showInterface UI has none. */
            fView = chosen_haiku ? (BView *)widget : NULL;
            ok = true;
        }
    }
    if (uis) {
        lilv_uis_free(uis);
    }
    if (!ok) {
        return false;
    }

    /* Force one initial port_event per parameter and per control output. */
    const uint32_t total = fHost->n_params + fHost->n_outs;
    fLast = (float *)calloc(total ? total : 1, sizeof(float));
    if (!fLast) {
        Fail("out of memory");
        Close();
        return false;
    }
    for (uint32_t i = 0; i < fHost->n_params; i++) {
        fLast[i] = fHost->params[i].def + 1.0f;
    }
    for (uint32_t i = 0; i < fHost->n_outs; i++) {
        fLast[fHost->n_params + i] = NAN; /* unequal to everything */
    }
    return true;
}

bool Lv2UiHost::Show()
{
    if (!fHandle || !fShowIface || !fShowIface->show || fShowIface->show(fHandle) != 0) {
        Fail("the plug-in's UI could not be shown");
        return false;
    }
    fShown = true;
    return true;
}

bool Lv2UiHost::Tick()
{
    if (!fHandle || !fLast) {
        return true;
    }
    if (fDesc->port_event) {
        for (uint32_t i = 0; i < fHost->n_params; i++) {
            const uint32_t port = fHost->params[i].port_index;
            const float v = lv2host_get_control(fHost, port);
            if (v != fLast[i]) {
                fLast[i] = v;
                fDesc->port_event(fHandle, port, sizeof(float), 0, &v);
            }
        }
        for (uint32_t i = 0; i < fHost->n_outs; i++) {
            const uint32_t port = fHost->outs[i].port_index;
            const float v = lv2host_get_control(fHost, port);
            if (v != fLast[fHost->n_params + i]) {
                fLast[fHost->n_params + i] = v;
                fDesc->port_event(fHandle, port, sizeof(float), 0, &v);
            }
        }
    }
    /* The spec requires the host to drive idle() and to hide the UI once
       idle() returns non-zero (the user closed it). */
    if (fIdleIface && fIdleIface->idle && fIdleIface->idle(fHandle) != 0) {
        if (fShown && fShowIface && fShowIface->hide) {
            fShowIface->hide(fHandle);
            fShown = false;
        }
        return false;
    }
    return true;
}

void Lv2UiHost::Close()
{
    if (fShown && fShowIface && fShowIface->hide) {
        fShowIface->hide(fHandle);
    }
    fShown = false;
    if (fHandle && fDesc && fDesc->cleanup) {
        fDesc->cleanup(fHandle);
    }
    fHandle = NULL;
    fView = NULL;
}
