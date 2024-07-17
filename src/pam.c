#include <pwd.h>
#include <security/_pam_types.h>
#include <security/pam_appl.h>

#include "astal-auth.h"

struct _AstalAuthPam {
    GObject parent_instance;

    gchar *username;
    gchar *service;
};

typedef struct {
    GTask *task;
    GMainContext *context;
    GMutex data_mutex;
    GCond data_cond;

    gchar *secret;
    gboolean secret_set;
} AstalAuthPamPrivate;

typedef struct {
    AstalAuthPam *pam;
    guint signal_id;
    gchar *msg;
} AstalAuthPamSignalEmitData;

static void astal_auth_pam_signal_emit_data_free(AstalAuthPamSignalEmitData *data) {
    g_free(data->msg);
    g_free(data);
}

typedef enum {
    ASTAL_AUTH_PAM_SIGNAL_PROMPT_VISIBLE,
    ASTAL_AUTH_PAM_SIGNAL_PROMPT_HIDDEN,
    ASTAL_AUTH_PAM_SIGNAL_INFO,
    ASTAL_AUTH_PAM_SIGNAL_ERROR,
    ASTAL_AUTH_PAM_SIGNAL_SUCCESS,
    ASTAL_AUTH_PAM_SIGNAL_FAIL,
    ASTAL_AUTH_PAM_N_SIGNALS
} AstalAuthPamSignals;

typedef enum {
    ASTAL_AUTH_PAM_PROP_USERNAME = 1,
    ASTAL_AUTH_PAM_PROP_SERVICE,
    ASTAL_AUTH_PAM_N_PROPERTIES
} AstalAuthPamProperties;

static guint astal_auth_pam_signals[ASTAL_AUTH_PAM_N_SIGNALS] = {
    0,
};
static GParamSpec *astal_auth_pam_properties[ASTAL_AUTH_PAM_N_PROPERTIES] = {
    NULL,
};

G_DEFINE_TYPE_WITH_PRIVATE(AstalAuthPam, astal_auth_pam, G_TYPE_OBJECT);

void astal_auth_pam_set_username(AstalAuthPam *self, const gchar *username) {
    g_return_if_fail(ASTAL_AUTH_IS_PAM(self));
    g_return_if_fail(username != NULL);

    g_free(self->username);
    self->username = g_strdup(username);
    g_object_notify(G_OBJECT(self), "username");
}

void astal_auth_pam_supply_secret(AstalAuthPam *self, const gchar *secret) {
    g_return_if_fail(ASTAL_AUTH_IS_PAM(self));
    AstalAuthPamPrivate *priv = astal_auth_pam_get_instance_private(self);

    g_mutex_lock(&priv->data_mutex);
    g_free(priv->secret);
    priv->secret = g_strdup(secret);
    priv->secret_set = TRUE;
    g_cond_signal(&priv->data_cond);
    g_mutex_unlock(&priv->data_mutex);
}

void astal_auth_pam_set_service(AstalAuthPam *self, const gchar *service) {
    g_return_if_fail(ASTAL_AUTH_IS_PAM(self));
    g_return_if_fail(service != NULL);

    g_free(self->service);
    self->service = g_strdup(service);
    g_object_notify(G_OBJECT(self), "service");
}

const gchar *astal_auth_pam_get_username(AstalAuthPam *self) {
    g_return_val_if_fail(ASTAL_AUTH_IS_PAM(self), NULL);
    return self->username;
}

const gchar *astal_auth_pam_get_service(AstalAuthPam *self) {
    g_return_val_if_fail(ASTAL_AUTH_IS_PAM(self), NULL);
    return self->service;
}

static void astal_auth_pam_set_property(GObject *object, guint property_id, const GValue *value,
                                        GParamSpec *pspec) {
    AstalAuthPam *self = ASTAL_AUTH_PAM(object);

    switch (property_id) {
        case ASTAL_AUTH_PAM_PROP_USERNAME:
            astal_auth_pam_set_username(self, g_value_get_string(value));
            break;
        case ASTAL_AUTH_PAM_PROP_SERVICE:
            astal_auth_pam_set_service(self, g_value_get_string(value));
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void astal_auth_pam_get_property(GObject *object, guint property_id, GValue *value,
                                        GParamSpec *pspec) {
    AstalAuthPam *self = ASTAL_AUTH_PAM(object);

    switch (property_id) {
        case ASTAL_AUTH_PAM_PROP_USERNAME:
            g_value_set_string(value, self->username);
            break;
        case ASTAL_AUTH_PAM_PROP_SERVICE:
            g_value_set_string(value, self->service);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void astal_auth_pam_callback(GObject *object, GAsyncResult *res, gpointer user_data) {
    AstalAuthPam *self = ASTAL_AUTH_PAM(object);
    AstalAuthPamPrivate *priv = astal_auth_pam_get_instance_private(self);

    GTask *task = g_steal_pointer(&priv->task);

    GError *error = NULL;
    g_task_propagate_int(task, &error);

    if (error == NULL) {
        g_signal_emit(self, astal_auth_pam_signals[ASTAL_AUTH_PAM_SIGNAL_SUCCESS], 0);
    } else {
        g_signal_emit(self, astal_auth_pam_signals[ASTAL_AUTH_PAM_SIGNAL_FAIL], 0, error->message);
        g_error_free(error);
    }
    g_object_unref(task);
}

static gboolean astal_auth_pam_emit_signal_in_context(gpointer user_data) {
    AstalAuthPamSignalEmitData *data = user_data;
    g_signal_emit(data->pam, data->signal_id, 0, data->msg);
    return G_SOURCE_REMOVE;
}

static void astal_auth_pam_emit_signal(AstalAuthPam *pam, guint signal, const gchar *msg) {
    GSource *emit_source;
    AstalAuthPamSignalEmitData *data;

    data = g_new0(AstalAuthPamSignalEmitData, 1);
    data->pam = pam;
    data->signal_id = astal_auth_pam_signals[signal];
    data->msg = g_strdup(msg);

    emit_source = g_idle_source_new();
    g_source_set_callback(emit_source, astal_auth_pam_emit_signal_in_context, data,
                          (GDestroyNotify)astal_auth_pam_signal_emit_data_free);
    g_source_set_priority(emit_source, G_PRIORITY_DEFAULT);
    g_source_attach(emit_source,
                    ((AstalAuthPamPrivate *)astal_auth_pam_get_instance_private(pam))->context);
    g_source_unref(emit_source);
}

int astal_auth_pam_handle_conversation(int num_msg, const struct pam_message **msg,
                                       struct pam_response **resp, void *appdata_ptr) {
    AstalAuthPam *self = appdata_ptr;
    AstalAuthPamPrivate *priv = astal_auth_pam_get_instance_private(self);

    struct pam_response *replies = NULL;
    if (num_msg <= 0 || num_msg > PAM_MAX_NUM_MSG) {
        return PAM_CONV_ERR;
    }
    replies = (struct pam_response *)calloc(num_msg, sizeof(struct pam_response));
    if (replies == NULL) {
        return PAM_BUF_ERR;
    }
    for (int i = 0; i < num_msg; ++i) {
        guint signal;
        switch (msg[i]->msg_style) {
            case PAM_PROMPT_ECHO_OFF:
                signal = ASTAL_AUTH_PAM_SIGNAL_PROMPT_HIDDEN;
                break;
            case PAM_PROMPT_ECHO_ON:
                signal = ASTAL_AUTH_PAM_SIGNAL_PROMPT_VISIBLE;
                break;
            case PAM_ERROR_MSG:
                signal = ASTAL_AUTH_PAM_SIGNAL_ERROR;
                ;
                break;
            case PAM_TEXT_INFO:
                signal = ASTAL_AUTH_PAM_SIGNAL_INFO;
                break;
            default:
                g_free(replies);
                return PAM_CONV_ERR;
                break;
        }
        guint signal_id = astal_auth_pam_signals[signal];
        if (g_signal_has_handler_pending(self, signal_id, 0, FALSE)) {
            astal_auth_pam_emit_signal(self, signal, msg[i]->msg);
            g_mutex_lock(&priv->data_mutex);
            while (!priv->secret_set) {
                g_cond_wait(&priv->data_cond, &priv->data_mutex);
            }
            replies[i].resp_retcode = 0;
            replies[i].resp = g_strdup(priv->secret);
            g_free(priv->secret);
            priv->secret = NULL;
            priv->secret_set = FALSE;
            g_mutex_unlock(&priv->data_mutex);
        }
    }
    *resp = replies;
    return PAM_SUCCESS;
}

static void astal_auth_pam_thread(GTask *task, gpointer object, gpointer task_data,
                                  GCancellable *cancellable) {
    AstalAuthPam *self = g_task_get_source_object(task);

    pam_handle_t *pamh = NULL;
    const struct pam_conv conv = {
        .conv = astal_auth_pam_handle_conversation,
        .appdata_ptr = self,
    };

    int retval;
    retval = pam_start(self->service, self->username, &conv, &pamh);
    if (retval == PAM_SUCCESS) {
        retval = pam_authenticate(pamh, 0);
        pam_end(pamh, retval);
    }
    if (retval != PAM_SUCCESS) {
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED, "%s",
                                pam_strerror(pamh, retval));
    } else {
        g_task_return_int(task, retval);
    }
}

gboolean astal_auth_pam_start_authenticate_with_callback(AstalAuthPam *self,
                                                         GAsyncReadyCallback result_callback,
                                                         gpointer user_data) {
    g_return_val_if_fail(ASTAL_AUTH_IS_PAM(self), FALSE);
    AstalAuthPamPrivate *priv = astal_auth_pam_get_instance_private(self);
    g_return_val_if_fail(priv->task == NULL, FALSE);

    priv->task = g_task_new(self, NULL, result_callback, user_data);
    g_task_set_priority(priv->task, 0);
    g_task_set_name(priv->task, "[AstalAuth] authenticate");
    g_task_run_in_thread(priv->task, astal_auth_pam_thread);

    return TRUE;
}

gboolean astal_auth_pam_start_authenticate(AstalAuthPam *self) {
    return astal_auth_pam_start_authenticate_with_callback(
        self, (GAsyncReadyCallback)astal_auth_pam_callback, NULL);
}

static void astal_auth_pam_on_hidden(AstalAuthPam *pam, const gchar *msg, gchar *password) {
    astal_auth_pam_supply_secret(pam, password);
    g_free(password);
}

gboolean astal_auth_pam_authenticate(const gchar *password, GAsyncReadyCallback result_callback,
                                     gpointer user_data) {
    AstalAuthPam *pam = g_object_new(ASTAL_AUTH_TYPE_PAM, NULL);
    g_signal_connect(pam, "auth-prompt-hidden", G_CALLBACK(astal_auth_pam_on_hidden),
                     (void *)g_strdup(password));

    gboolean started =
        astal_auth_pam_start_authenticate_with_callback(pam, result_callback, user_data);
    g_object_unref(pam);
    return started;
}

gssize astal_auth_pam_authenticate_finish(GAsyncResult *res, GError **error) {
    return g_task_propagate_int(G_TASK(res), error);
}

static void astal_auth_pam_init(AstalAuthPam *self) {
    AstalAuthPamPrivate *priv = astal_auth_pam_get_instance_private(self);

    priv->secret = NULL;

    g_cond_init(&priv->data_cond);
    g_mutex_init(&priv->data_mutex);

    priv->context = g_main_context_get_thread_default();
}

static void astal_auth_pam_finalize(GObject *gobject) {
    AstalAuthPam *self = ASTAL_AUTH_PAM(gobject);
    AstalAuthPamPrivate *priv = astal_auth_pam_get_instance_private(self);

    g_free(self->username);
    g_free(self->service);

    g_free(priv->secret);

    g_cond_clear(&priv->data_cond);
    g_mutex_clear(&priv->data_mutex);

    G_OBJECT_CLASS(astal_auth_pam_parent_class)->finalize(gobject);
}

static void astal_auth_pam_class_init(AstalAuthPamClass *class) {
    GObjectClass *object_class = G_OBJECT_CLASS(class);

    object_class->get_property = astal_auth_pam_get_property;
    object_class->set_property = astal_auth_pam_set_property;

    object_class->finalize = astal_auth_pam_finalize;

    struct passwd *passwd = getpwuid(getuid());

    astal_auth_pam_properties[ASTAL_AUTH_PAM_PROP_USERNAME] =
        g_param_spec_string("username", "username", "username used for authentication",
                            passwd->pw_name, G_PARAM_CONSTRUCT | G_PARAM_READWRITE);

    astal_auth_pam_properties[ASTAL_AUTH_PAM_PROP_SERVICE] =
        g_param_spec_string("service", "service", "the pam service to use", "astal-auth",
                            G_PARAM_CONSTRUCT | G_PARAM_READWRITE);

    g_object_class_install_properties(object_class, ASTAL_AUTH_PAM_N_PROPERTIES,
                                      astal_auth_pam_properties);

    astal_auth_pam_signals[ASTAL_AUTH_PAM_SIGNAL_PROMPT_VISIBLE] =
        g_signal_new("auth-prompt-visible", G_TYPE_FROM_CLASS(class), G_SIGNAL_RUN_FIRST, 0, NULL,
                     NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);

    astal_auth_pam_signals[ASTAL_AUTH_PAM_SIGNAL_PROMPT_HIDDEN] =
        g_signal_new("auth-prompt-hidden", G_TYPE_FROM_CLASS(class), G_SIGNAL_RUN_FIRST, 0, NULL,
                     NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);

    astal_auth_pam_signals[ASTAL_AUTH_PAM_SIGNAL_INFO] =
        g_signal_new("auth-info", G_TYPE_FROM_CLASS(class), G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
                     G_TYPE_NONE, 1, G_TYPE_STRING);

    astal_auth_pam_signals[ASTAL_AUTH_PAM_SIGNAL_ERROR] =
        g_signal_new("auth-error", G_TYPE_FROM_CLASS(class), G_SIGNAL_RUN_FIRST, 0, NULL, NULL,
                     NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
    astal_auth_pam_signals[ASTAL_AUTH_PAM_SIGNAL_SUCCESS] =
        g_signal_new("success", G_TYPE_FROM_CLASS(class), G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
                     G_TYPE_NONE, 0);
    astal_auth_pam_signals[ASTAL_AUTH_PAM_SIGNAL_FAIL] =
        g_signal_new("fail", G_TYPE_FROM_CLASS(class), G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
                     G_TYPE_NONE, 1, G_TYPE_STRING);
}
