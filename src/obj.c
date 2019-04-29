/* Stellarium Web Engine - Copyright (c) 2018 - Noctua Software Ltd
 *
 * This program is licensed under the terms of the GNU AGPL v3, or
 * alternatively under a commercial licence.
 *
 * The terms of the AGPL v3 license can be found in the main directory of this
 * repository.
 */

#include "swe.h"
#include <inttypes.h>

// Make sure the obj type padding is just after the type.
_Static_assert(offsetof(obj_t, type_padding_) ==
               offsetof(obj_t, type) + 4, "");

typedef struct {
    int         id;
    const char *id_str;
    const char *name;
    int         isattr; // Set to 1 if this is an attribute
    int         type;
    int         arg0;
    int         arg1;
} attr_info_t;

// Global list of all the registered klasses.
static obj_klass_t *g_klasses = NULL;

static obj_t *obj_create_(obj_klass_t *klass, const char *id, obj_t *parent,
                          json_value *args)
{
    const char *attr;
    int i;
    obj_t *obj;
    json_value *v;

    assert(klass->size);
    obj = calloc(1, klass->size);
    if (id) obj->id = strdup(id);
    obj->ref = 1;
    obj->klass = klass;
    if (parent) module_add(parent, obj);
    if (obj->klass->init) obj->klass->init(obj, args);

    // Set the attributes.
    // obj_call_json only accepts array arguments, so we have to put the
    // values into an array, which is not really supported by the json
    // library, that's why I set the array value to NULL before calling
    // json_builder_free!
    if (args && args->type == json_object) {
        for (i = 0; i < args->u.object.length; i++) {
            attr = args->u.object.values[i].name;
            if (obj_has_attr(obj, attr)) {
                v = json_array_new(1);
                json_array_push(v, args->u.object.values[i].value);
                obj_call_json(obj, attr, v);
                v->u.array.values[0] = NULL;
                json_builder_free(v);
            }
        }
    }

    return obj;
}

obj_t *obj_create(const char *type, const char *id, obj_t *parent,
                  json_value *args)
{
    obj_t *obj;
    obj_klass_t *klass;

    for (klass = g_klasses; klass; klass = klass->next) {
        if (klass->id && strcmp(klass->id, type) == 0) break;
        if (klass->model && strcmp(klass->model, type) == 0) break;
    }
    assert(klass);
    obj = obj_create_(klass, id, parent, args);
    return obj;
}

// Same as obj_create but the json arguments are passes the json arguments
// as a string.
EMSCRIPTEN_KEEPALIVE
obj_t *obj_create_str(const char *type, const char *id, obj_t *parent,
                      const char *args)
{
    obj_t *ret;
    json_value *jargs;
    json_settings settings = {.value_extra = json_builder_extra};
    jargs = args ? json_parse_ex(&settings, args, strlen(args), NULL) : NULL;
    ret = obj_create(type, id, parent, jargs);
    json_value_free(jargs);
    return ret;
}

EMSCRIPTEN_KEEPALIVE
void obj_release(obj_t *obj)
{
    if (!obj) return;
    assert(obj->ref);
    obj->ref--;
    if (obj->ref == 0) {
        if (obj->parent)
            DL_DELETE(obj->parent->children, obj);
        if (obj->klass->del) obj->klass->del(obj);
        free(obj->id);
        free(obj);
    }
}

EMSCRIPTEN_KEEPALIVE
obj_t *obj_clone(const obj_t *obj)
{
    assert(obj->klass->clone);
    return obj->klass->clone(obj);
}

static void name_on_designation(const obj_t *obj, void *user,
                                const char *cat, const char *value)
{
    int current_score;
    int *score = USER_GET(user, 0);
    char *out = USER_GET(user, 1);
    current_score = (strcmp(cat, "NAME") == 0) ? 2 : 1;
    if (current_score <= *score) return;
    *score = current_score;
    if (*cat && strcmp(cat, "NAME") != 0)
        snprintf(out, 128, "%s %s", cat, value);
    else
        snprintf(out, 128, "%s", value);
}

/*
 * Function: obj_get_name
 * Return the given name for an object or its first designation.
 */
const char *obj_get_name(const obj_t *obj, char buf[static 128])
{
    int score = 0;
    buf[0] = '\0';
    obj_get_designations(obj, USER_PASS(&score, buf), name_on_designation);
    // If no designation is found, use the oid.
    if (!buf[0])
        oid_to_str(obj->oid, buf);
    return buf;
}

int obj_render(const obj_t *obj, const painter_t *painter)
{
    if (obj->klass->render)
        return obj->klass->render(obj, painter);
    else
        return 0;
}

int obj_get_pvo(obj_t *obj, observer_t *obs, double pvo[2][4])
{
    assert(obj->klass->get_pvo);
    return obj->klass->get_pvo(obj, obs, pvo);
}

int obj_get_info(obj_t *obj, observer_t *obs, int info,
                 void *out)
{
    double pvo[2][4];

    switch (info) {
    case INFO_TYPE:
        strncpy(out, obj->type, 4);
        return 0;
    case INFO_PVO:
        return obj_get_pvo(obj, obs, out);
    case INFO_POS: // First component of the PVO info.
        obj_get_info(obj, obs, INFO_PVO, pvo);
        memcpy(out, pvo[0], sizeof(pvo[0]));
        return 0;
    case INFO_DISTANCE:
        obj_get_pvo(obj, obs, pvo);
        *(double*)out = pvo[0][3] ? vec3_norm(pvo[0]) : NAN;
        return 0;
    default:
        break;
    }

    if (obj->klass->get_info) {
        observer_update(obs, true);
        return obj->klass->get_info(obj, obs, info, out);
    }
    return 1;
}

EMSCRIPTEN_KEEPALIVE
const char *obj_get_id(const obj_t *obj)
{
    return obj->id;
}

static int on_name(const obj_t *obj, void *user,
                   const char *cat, const char *value)
{
    void (*f)(const obj_t *obj, void *user,
              const char *cat, const char *value);
    void *u;
    int *nb;
    f = USER_GET(user, 0);
    u = USER_GET(user, 1);
    nb = USER_GET(user, 2);
    f(obj, u, cat, value);
    nb++;
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int obj_get_designations(const obj_t *obj, void *user,
                  void (*f)(const obj_t *obj, void *user,
                            const char *cat, const char *value))
{
    int nb = 0;
    if (obj->klass->get_designations)
        obj->klass->get_designations(obj, USER_PASS(f, user, &nb), on_name);
    return nb;
}

// XXX: cleanup this code.
static json_value *obj_fn_default(obj_t *obj, const attribute_t *attr,
                                  const json_value *args)
{
    assert(attr->type);
    void *p = ((void*)obj) + attr->member.offset;
    // Buffer large enough to contain any kind of property data, including
    // static strings.
    char buf[4096] __attribute__((aligned(8)));
    obj_t *o;

    // If no input arguents, return the value.
    if (!args || (args->type == json_array && !args->u.array.length)) {
        if (attr->type % 16 == TYPE_BOOL)
            return args_value_new(attr->type, *(bool*)p);
        else if (attr->type % 16 == TYPE_INT)
            return args_value_new(attr->type, *(int*)p);
        else if (attr->type % 16 == TYPE_FLOAT) {
            // Works for both float and double.
            if (attr->member.size == sizeof(double)) {
                return args_value_new(attr->type, *(double*)p);
            } else if (attr->member.size == sizeof(float)) {
                return args_value_new(attr->type, *(float*)p);
            } else {
                assert(false);
                return NULL;
            }
        } else if (attr->type % 16 == TYPE_PTR)
            return args_value_new(attr->type, *(void**)p);
        else if (attr->type == TYPE_STRING_PTR)
            return args_value_new(attr->type, *(char**)p);
        else
            return args_value_new(attr->type, p);
    } else { // Set the value.
        assert(attr->member.size <= sizeof(buf));
        args_get(args, NULL, 1, attr->type, buf);
        if (memcmp(p, buf, attr->member.size) != 0) {
            // If we override an object, don't forget to release the
            // previous value and increment the ref to the new one.
            if (attr->type == TYPE_OBJ) {
                o = *(obj_t**)p;
                obj_release(o);
                memcpy(&o, buf, sizeof(o));
                if (o) o->ref++;
            }
            memcpy(p, buf, attr->member.size);
            if (attr->on_changed) attr->on_changed(obj, attr);
            module_changed(obj, attr->name);
        }
        return NULL;
    }
}

EMSCRIPTEN_KEEPALIVE
const attribute_t *obj_get_attr_(const obj_t *obj, const char *attr_name)
{
    attribute_t *attr;
    int i;
    assert(obj);
    ASSERT(obj->klass->attributes, "type '%s' has no attributes '%s'",
           obj->klass->id, attr_name);
    for (i = 0; ; i++) {
        attr = &obj->klass->attributes[i];
        if (!attr->name) return NULL;
        if (strcmp(attr->name, attr_name) == 0) break;
    }
    return attr;
}

EMSCRIPTEN_KEEPALIVE
void obj_foreach_attr(const obj_t *obj,
                      void *user,
                      void (*f)(const char *attr, int is_property, void *user))
{
    int i;
    attribute_t *attr;
    obj_klass_t *klass;

    klass = obj->klass;
    if (!klass) return;
    if (!klass->attributes) return;
    for (i = 0; ; i++) {
        attr = &klass->attributes[i];
        if (!attr->name) break;
        f(attr->name, attr->is_prop, user);
    }
}

// Only to be used in the js code so that we can access object children
// directly using properties.
EMSCRIPTEN_KEEPALIVE
void obj_foreach_child(const obj_t *obj, void (*f)(const char *id))
{
    obj_t *child;
    for (child = obj->children; child; child = child->next) {
        if (!(child->klass->flags & OBJ_IN_JSON_TREE)) continue;
        assert(child->id);
        f(child->id);
    }
}

bool obj_has_attr(const obj_t *obj, const char *attr)
{
    return obj_get_attr_(obj, attr) != NULL;
}

json_value *obj_call_json(obj_t *obj, const char *name,
                          const json_value *args)
{

    const attribute_t *attr;
    attr = obj_get_attr_(obj, name);
    if (!attr) {
        LOG_E("Cannot find attribute %s of object %s", name, obj->id);
        return NULL;
    }
    return (attr->fn ?: obj_fn_default)(obj, attr, args);
}

EMSCRIPTEN_KEEPALIVE
char *obj_call_json_str(obj_t *obj, const char *attr, const char *args)
{
    json_value *jargs, *jret;
    char *ret;
    int size;

    jargs = args ? json_parse(args, strlen(args)) : NULL;
    jret = obj_call_json(obj, attr, jargs);
    size = json_measure(jret);
    ret = calloc(1, size);
    json_serialize(ret, jret);
    json_value_free(jargs);
    json_builder_free(jret);
    return ret;
}

int obj_get_attr(const obj_t *obj, const char *name, ...)
{
    json_value *ret;
    const attribute_t *attr;
    va_list ap;

    attr = obj_get_attr_(obj, name);
    va_start(ap, name);
    ret = obj_call_json(obj, name, NULL);
    assert(ret);
    args_vget(ret, NULL, 1, attr->type, &ap);
    json_builder_free(ret);
    va_end(ap);
    return 0;
}

int obj_set_attr(const obj_t *obj, const char *name, ...)
{
    json_value *arg, *ret;
    va_list ap;
    const attribute_t *attr;

    attr = obj_get_attr_(obj, name);
    va_start(ap, name);
    arg = args_vvalue_new(attr->type, &ap);
    ret = obj_call_json(obj, name, arg);
    json_builder_free(arg);
    json_builder_free(ret);
    va_end(ap);
    return 0;
}

void obj_register_(obj_klass_t *klass)
{
    assert(klass->size);
    LL_PREPEND(g_klasses, klass);
}

static int klass_sort_cmp(void *a, void *b)
{
    obj_klass_t *at, *bt;
    at = a;
    bt = b;
    int ak = at->create_order ?: at->render_order;
    int bk = bt->create_order ?: bt->render_order;
    return cmp(ak, bk);
}

obj_klass_t *obj_get_all_klasses(void)
{
    LL_SORT(g_klasses, klass_sort_cmp);
    return g_klasses;
}

obj_klass_t *obj_get_klass_by_name(const char *name)
{
    obj_klass_t *klass;
    for (klass = g_klasses; klass; klass = klass->next) {
        if (klass->id && strcmp(klass->id, name) == 0) return klass;
    }
    return NULL;
}

void obj_get_pos_observed(obj_t *obj, observer_t *obs, double pos[4])
{
    double pvo[2][4];
    obj_get_pvo(obj, obs, pvo);
    convert_frame(obs, FRAME_ICRF, FRAME_OBSERVED, 0, pvo[0], pos);
}

void obj_get_2d_ellipse(obj_t *obj,  const observer_t *obs,
                        const projection_t *proj,
                        double win_pos[2], double win_size[2],
                        double* win_angle)
{
    double pvo[2][4], p[4];
    double vmag, s, luminance, radius;

    if (obj->klass->get_2d_ellipse) {
        obj->klass->get_2d_ellipse(obj, obs, proj,
                                   win_pos, win_size, win_angle);
        return;
    }

    // Fall back to generic version

    // Ellipse center
    obj_get_pvo(obj, obs, pvo);
    vec3_normalize(pvo[0], pvo[0]);
    convert_frame(obs, FRAME_ICRF, FRAME_VIEW, true, pvo[0], p);
    project(proj, PROJ_TO_WINDOW_SPACE, 2, p, win_pos);

    // Empirical formula to compute the pointer size.
    obj_get_info(obj, obs, INFO_VMAG, &vmag);
    core_get_point_for_mag(vmag, &s, &luminance);
    s *= 2;

    if (obj_has_attr(obj, "radius")) {
        obj_get_attr(obj, "radius", &radius);
        radius = radius / 2.0 * proj->window_size[0] /
                proj->scaling[0];
        s = max(s, radius);
    }

    win_size[0] = s;
    win_size[1] = s;
    *win_angle = 0;
}

const char *obj_info_type_str(int type)
{
    const char *names[] = {
#define X(name, lower, ...) [TYPE_##name] = #lower,
        ALL_TYPES(X)
#undef X
    };
    return names[type];
}

/******** TESTS ***********************************************************/

#if COMPILE_TESTS

typedef struct test {
    obj_t   obj;
    double  alt;
    int     proj;
    double  my_attr;
    int     nb_changes;
} test_t;

static void test_my_attr_changed(obj_t *obj, const attribute_t *attr)
{
    ((test_t*)obj)->nb_changes++;
}

static json_value *test_lookat_fn(obj_t *obj, const attribute_t *attr,
                                  const json_value *args)
{
    return NULL;
}

static obj_klass_t test_klass = {
    .id = "test",
    .attributes = (attribute_t[]) {
        PROPERTY(altitude, TYPE_FLOAT, MEMBER(test_t, alt)),
        PROPERTY(my_attr, TYPE_FLOAT, MEMBER(test_t, my_attr),
                 .on_changed = test_my_attr_changed),
        PROPERTY(projection, TYPE_ENUM, MEMBER(test_t, proj),
                 .desc = "Projection"),
        FUNCTION(lookat, .fn = test_lookat_fn),
        {}
    },
};

static void test_simple(void)
{
    test_t test = {};
    double alt;

    test.obj.klass = &test_klass;
    test.alt = 10.0;
    obj_get_attr(&test.obj, "altitude", &alt);
    assert(alt == 10.0);
    obj_set_attr(&test.obj, "altitude", 5.0);
    assert(test.alt == 5.0);

    obj_set_attr(&test.obj, "my_attr", 20.0);
    assert(test.my_attr == 20.0);
    assert(test.nb_changes == 1);
    obj_set_attr(&test.obj, "my_attr", 20.0);
    assert(test.nb_changes == 1);
    obj_set_attr(&test.obj, "my_attr", 30.0);
    assert(test.nb_changes == 2);
}

TEST_REGISTER(NULL, test_simple, TEST_AUTO);

#endif
