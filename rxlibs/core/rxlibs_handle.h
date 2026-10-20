/* rxlibs_handle.h - opaque-handle glue shared by the rxlibs plugins (rxnumpy, rxscipy, rxpandas).
 * Included once per plugin translation unit AFTER crexxpa.h.  A handle is a `.binary` value carrying
 * a native payload {magic, kind, void *obj}; copies share the object through a reference count
 * supplied by the owner (obj layout: first member `long refs`), release via the kind's destroy fn.
 *
 * Cross-plugin interchange: a plugin recognises a handle of another plugin by the payload ops
 * type_name string (e.g. "crexx.rxnumpy.array.v1"), not by ops pointer.  The array struct (rxnp)
 * is shared by compiling rxnp.c into every plugin; malloc/free are the common libc ones. */
#ifndef RXLIBS_HANDLE_H
#define RXLIBS_HANDLE_H
#include <string.h>

typedef struct { unsigned int magic; void *obj; } rxl_payload;
#define RXL_MAGIC 0x52584C42u

typedef struct rxl_kind {
    rxpa_native_payload_ops ops;      /* ops.type_name identifies the kind */
    void (*retain)(void *obj);
    void (*release)(void *obj);
} rxl_kind;

/* returns payload when the attribute holds a handle whose type_name equals `type_name` */
static rxl_payload *rxl_get(rxpa_attribute_value v, const char *type_name)
{
    const rxpa_native_payload_ops *ops = NULL; size_t len = 0;
    rxl_payload *p = (rxl_payload *)GETNATIVEPAYLOAD(v, &len, &ops, NULL);
    if (!p || len != sizeof *p || !ops || !ops->type_name || strcmp(ops->type_name, type_name) != 0 ||
        p->magic != RXL_MAGIC || !p->obj) return NULL;
    return p;
}
static int rxl_publish(rxpa_attribute_value dest, const rxl_kind *k, void *obj)
{
    rxl_payload p;
    p.magic = RXL_MAGIC; p.obj = obj;
    if (SETNATIVEPAYLOAD(dest, &p, sizeof p, &k->ops, 0u) != 0) { k->release(obj); return 0; }
    return 1;
}
#endif
