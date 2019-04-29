/* Stellarium Web Engine - Copyright (c) 2018 - Noctua Software Ltd
 *
 * This program is licensed under the terms of the GNU AGPL v3, or
 * alternatively under a commercial licence.
 *
 * The terms of the AGPL v3 license can be found in the main directory of this
 * repository.
 */

#ifndef OBJ_H
#define OBJ_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "obj_info.h"

/*
 * Base class for all the objects in swe (modules, sky objects, etc).
 *
 * Inspired by linux kernel kobj.
 *
 * An object is any C structure that starts with an obj_t attribute.
 * The obj_t structure contains a reference counter (for automatic memory
 * managment), an uniq id, a vtable pointer, and various attributes
 * specific to sky objects (J2000 position, ra/de...)
 *
 * The vtable for a given class is specified in a static obj_klass instance.
 */

/*
 * Enum: OBJ_FLAG
 *
 * Flags that can be set to object.
 *
 * OBJ_IN_JSON_TREE     - The object show up in the json tree dump.
 * OBJ_MODULE           - The object is a module.
 * OBJ_LISTABLE         - For modules that maintain a list of children objects,
 *                        like comets, this allows obj_list to directly do
 *                        the listing.
 */
enum {
    OBJ_IN_JSON_TREE = 1 << 0,
    OBJ_MODULE       = 1 << 1,
    OBJ_LISTABLE     = 1 << 2,
};

enum {
    OBJ_AGAIN       = 1 // Returned if obj_list can be called again.
};

typedef struct _json_value json_value;
typedef struct attribute attribute_t;
typedef struct obj obj_t;
typedef struct observer observer_t;
typedef struct projection projection_t;
typedef struct painter painter_t;
typedef struct obj_klass obj_klass_t;

/*
 * Type: obj_klass
 * Info structure that represents a given class of objects.
 *
 * Each object should have a pointer to an obj_klass instance that defines
 * his type.  This is the equivalent of a C++ class.
 *
 * The klass allow object to define some basic methods for initializing,
 * updating, rendering... and also the list of public attributes for the
 * object.
 *
 * Attributes:
 *   id     - Name of the class.
 *   size   - Must be set to the size of the instance object struct.  This
 *            is used for the initial calloc done by the constructor.
 *   flags  - Some of <OBJ_FLAG>.
 *   attributes - List of <attribute_t>, NULL terminated.
 *
 * Methods:
 *   init   - Called when a new instance is created.
 *   del    - Called when an instance is destroyed.
 *   render - Render the object.
 *   post_render - Called after all modules are rendered, but with a still
 *                 valid OpenGL context. Useful for e.g. GUI rendering.
 *   clone  - Create a copy of the object.
 *   get    - Find a sub-object for a given query.
 *   get_by_oid  - Find a sub-object for a given oid.
 *
 * Module Attributes:
 *   render_order - Used to sort the modules before rendering.
 *   create_order - Used at creation time to make sure the modules are
 *                  created in the right order.
 *
 * Module Methods:
 *   update  - Update the module.
 *   list    - List all the sky objects children from this module.
 *   get_render_order - Return the render order.
 *   on_mouse   - Called when there is a mouse event.
 */
struct obj_klass
{
    const char *id;
    const char *model; // Model name in noctua server.
    size_t   size; // must be set to the size of the object struct.
    uint32_t flags;
    // Various methods to manipulate objects of this class.
    int (*init)(obj_t *obj, json_value *args);
    void (*del)(obj_t *obj);
    int (*get_pvo)(const obj_t *obj, const observer_t *obs, double pvo[2][4]);
    int (*get_info)(const obj_t *obj, const observer_t *obs, int info,
                    void *out);
    int (*render)(const obj_t *obj, const painter_t *painter);
    int (*post_render)(const obj_t *obj, const painter_t *painter);
    int (*render_pointer)(const obj_t *obj, const painter_t *painter);
    void (*get_2d_ellipse)(const obj_t *obj, const observer_t *obs,
                           const projection_t *proj,
                           double win_pos[2], double win_size[2],
                           double* win_angle);

    // For modules objects.
    int (*on_mouse)(obj_t *obj, int id, int state, double x, double y);

    int (*update)(obj_t *module, double dt);
    // Find a sky object given an id.
    obj_t *(*get)(const obj_t *obj, const char *id, int flags);
    // Find a sky object given an oid
    obj_t *(*get_by_oid)(const obj_t *obj, uint64_t oid, uint64_t hint);

    // List all the names associated with an object.
    void (*get_designations)(const obj_t *obj, void *user,
                      int (*f)(const obj_t *obj, void *user,
                               const char *cat, const char *str));

    void (*gui)(obj_t *obj, int location);

    obj_t* (*clone)(const obj_t *obj);
    // List all the sky objects children from this module.
    int (*list)(const obj_t *obj, observer_t *obs, double max_mag,
                uint64_t hint, void *user,
                int (*f)(void *user, obj_t *obj));

    // Add a source of data.
    int (*add_data_source)(obj_t *obj, const char *url, const char *type,
                           json_value *args);

    // Return the render order.
    // By default this return the class attribute `render_order`.
    double (*get_render_order)(const obj_t *obj);

    // Used to sort the modules when we render and create them.
    double render_order;
    double create_order;

    // List of object attributes that can be read, set or called with the
    // obj_call and obj_toogle_attr functions.
    attribute_t *attributes;

    // All the registered klass are put in a list, sorted by create_order.
    obj_klass_t *next;
};

/*
 * Type: obj_t
 * The base structure for all sky objects and modules
 *
 * All the module and sky objects should 'inherit' from this structure,
 * that is must have an obj_t as a first attribute, so that we can cast
 * any object structure to an obj_t.
 *
 * Attributes:
 *   klass      - Pointer to an <obj_klass_t> structure.
 *   ref        - Reference counter.
 *   id         - String id of the object.
 *   oid        - Internal uniq id.
 *   type       - Four bytes type id of the object.  Should follow the
 *                condensed values defined by Simbad:
 *                http://simbad.u-strasbg.fr/simbad/sim-display?data=otypes
 *   parent     - Parent object.
 *   children   - Pointer to the list of children.
 *   prev       - Pointer to the previous sibling.
 *   next       - Pointer to the next sibling.
 *   observer_hash - Hash of the observer the last time obj_update was called.
 *                   Can be used to skip update.
 *   vmag       - Visual magnitude.
 *   pos        - TODO: for the moment this is a struct with various computed
 *                positions.  I want to replace it with a single ICRS
 *                pos + speed argument.
 */
struct obj
{
    obj_klass_t *klass;
    int         ref;
    char        *id;    // To be removed.  Use oid instead.
    uint64_t    oid;
    char        type[4];
    char        type_padding_; // Ensure that type is null terminated.
    obj_t       *parent;
    obj_t       *children, *prev, *next;
};

/*
 * Type: attribute_t
 * Represent an object attribute.
 *
 * An attribute can either be a property (a single value that we can get or
 * set), or a function that we can call.  Attributes also emit signals
 * when they change so that other object can react to it.
 *
 * Attributes allows to manipulate objects without having to expose the
 * underlying C struct.  This is important for javascript binding.
 *
 * We should never create attributes directly, instead use the
 * <PROPERTY> and <FUNCTION> macros inside the <obj_klass_t> declaration.
 *
 * Attributes:
 *   name       - Name of this attribute.
 *   type       - Base type (one of the TYPE values from obj_info.h).
 *   is_prop    - Set to true if the attribute is a property.
 *   fn         - Attribute function (setter/getter for property).
 *   member     - Member info for common case of attributes that map directly
 *                to an object struct member.
 *   desc       - Description of the attribute.
 *   on_changed - Callback called when a property changed.
 */
struct attribute {
    const char *name;
    int type;
    int info;       // Sky object info id (see obj_info.h)
    bool is_prop;
    json_value *(*fn)(obj_t *obj, const attribute_t *attr,
                      const json_value *args);
    struct {
        int offset;
        int size;
    } member;
    const char *desc;
    void (*on_changed)(obj_t *obj, const attribute_t *attr);
};

/*
 * Macro: PROPERTY
 * Convenience macro to define a property attribute.
 */
#define PROPERTY(name, ...) {#name, ##__VA_ARGS__, .is_prop = true}

/*
 * Macro: FUNCTION
 * Convenience macro to define a function attribute.
 */
#define FUNCTION(name, ...) {#name, ##__VA_ARGS__}

/*
 * Macro: MEMBER
 * Convenience macro to define that a property map to a C struct attribute.
 */
#define MEMBER(k, m) .member = {offsetof(k, m), sizeof(((k*)0)->m)}

/*
 * Function: obj_create
 * Create a new object.
 *
 * Properties:
 *   type   - The type of the object (must have been registered).
 *   id     - Unique id to give the object.
 *   parent - The new object is added to the parent.
 *   args   -  Attributes to set.
 */
obj_t *obj_create(const char *type, const char *id, obj_t *parent,
                  json_value *args);

/*
 * Function: obj_create
 * Same as obj_create but the json arguments are passed as a string.
 */
obj_t *obj_create_str(const char *type, const char *id, obj_t *parent,
                      const char *args);

/*
 * Function: obj_release
 * Decrement object ref count and delete it if needed.
 */
void obj_release(obj_t *obj);

/*
 * Function: obj_clone
 * Create a clone of the object.
 *
 * Fails if the object doesn't support cloning.
 */
obj_t *obj_clone(const obj_t *obj);


/*
 * Function: obj_render
 * Render an object.
 */
int obj_render(const obj_t *obj, const painter_t *painter);

int obj_get_pvo(obj_t *obj, observer_t *obs, double pvo[2][4]);

int obj_get_info(obj_t *obj, observer_t *obs, int info, void *out);
char *obj_get_info_json(const obj_t *obj, observer_t *obs, int info);

/*
 * Function: obj_get_pos_observed
 * Conveniance function that updates an object and return its observed
 * position (az/alt frame, but as a cartesian vector).
 *
 * Parameters:
 *   obj - An object.
 *   obs - An observer.
 *   pos - Get the observed position in homogeneous coordinates (xyzw, AU).
 */
void obj_get_pos_observed(obj_t *obj, observer_t *obs, double pos[4]);

/*
 * Function: obj_get_2d_ellipse
 * Return the ellipse containing the rendered object in screen coordinates (px).
 *
 * Parameters:
 *   obj       - An object.
 *   obs       - The observer.
 *   proj      - The projection
 *   win_pos   - The ellipse center in screen coordinates (px).
 *   win_size  - The ellipse small and large sizes in screen coordinates (px).
 *   win_angle - The ellipse angle in screen coordinates (radian).
 */
void obj_get_2d_ellipse(obj_t *obj, const observer_t *obs,
                        const projection_t *proj,
                        double win_pos[2], double win_size[2],
                        double* win_angle);

/*
 * Function: obj_get_name
 * Return the given name for an object or its first designation.
 */
const char *obj_get_name(const obj_t *obj, char out[128]);

/*
 * Function: obj_get_designations
 * Return the list of all the identifiers of an object.
 *
 * Parameters:
 *   obj    - A sky object.
 *   user   - User data passed to the callback.
 *   f      - A callback function called once per designation.
 *
 * Return:
 *   The number of designations processed.
 */
int obj_get_designations(const obj_t *obj, void *user,
                         void (*f)(const obj_t *obj, void *user,
                                   const char *cat, const char *value));

/*
 * Section: attributes manipulation.
 */

/*
 * Function: obj_get_attr
 * Get an attribute from an object.
 *
 * Parameters:
 *   obj    - An object.
 *   attr   - The name of the attribute.
 *   ...    - Pointer to the output value.
 */
int obj_get_attr(const obj_t *obj, const char *attr, ...);

/*
 * Function: obj_set_attr
 * Set an attribute from an object.
 *
 * Parameters:
 *   obj    - An object.
 *   attr   - The name of the attribute.
 *   ...    - Pointer to the new value.
 */
int obj_set_attr(const obj_t *obj, const char *attr, ...);


/*
 * Function: obj_has_attr
 * Check whether an object has a given attribute.
 */
bool obj_has_attr(const obj_t *obj, const char *attr);

/*
 * Function: obj_call_json
 * Call an object attribute function passing json arguments.
 */
json_value *obj_call_json(obj_t *obj, const char *attr,
                          const json_value *args);

/*
 * Function: obj_call_json_str
 * Same as obj_call_json, but the json is passed as a string.
 */
char *obj_call_json_str(obj_t *obj, const char *attr, const char *args);

/*
 * Function: obj_get_attr_
 * Return the actual <attribute_t> pointer for a given attr name
 *
 * XXX: need to use a proper name!
 */
const attribute_t *obj_get_attr_(const obj_t *obj, const char *attr);


// Register an object klass, so that we can create instances dynamically
#define OBJ_REGISTER(klass) \
    static void obj_register_##klass##_(void) __attribute__((constructor)); \
    static void obj_register_##klass##_(void) { obj_register_(&klass); }
void obj_register_(obj_klass_t *klass);

// Return a pointer to the registered object klasses list.
obj_klass_t *obj_get_all_klasses(void);

void obj_foreach_attr(const obj_t *obj,
                      void *user,
                      void (*f)(const char *attr, int is_property, void *user));

obj_klass_t *obj_get_klass_by_name(const char *name);

#endif // OBJ_H
