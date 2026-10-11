#include "serde.h"

#include <string.h>
#include <thrift/c_glib/protocol/thrift_compact_protocol.h>
#include <thrift/c_glib/transport/thrift_memory_buffer.h>

G_DEFINE_AUTOPTR_CLEANUP_FUNC(ThriftTransport, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ThriftProtocol, g_object_unref)

// Thrift does not always set a GError when it fails (a read past the end
// of a memory buffer just returns -1), so always leave one behind.
static void propagate_thrift_error(GError** error, GError* thrift_error,
                                   const gchar* action) {
  if (thrift_error != NULL) {
    g_propagate_prefixed_error(error, thrift_error, "payload: %s: ", action);
  } else {
    g_set_error(error, BOSONOGA_ERROR, BOSONOGA_ERROR_SOURCE,
                "payload: %s failed (truncated or malformed data)", action);
  }
}

BosonogaPayload* bosonoga_serde_random_payload(GRand* rand) {
  g_return_val_if_fail(rand != NULL, NULL);

  BosonogaPayload* payload = g_object_new(BOSONOGA_TYPE_PAYLOAD, NULL);
  for (guint i = 0; i < BOSONOGA_SERDE_VALUE_COUNT; i++) {
    const gint32 whole = g_rand_int_range(rand, BOSONOGA_SERDE_I32_MIN,
                                          BOSONOGA_SERDE_I32_MAX + 1);
    const gint32 hundredths =
        g_rand_int_range(rand, BOSONOGA_SERDE_F32_HUNDREDTHS_MIN,
                         BOSONOGA_SERDE_F32_HUNDREDTHS_MAX + 1);
    const gdouble real = (gdouble)((gfloat)hundredths / 100.0f);
    g_array_append_val(payload->i32_values, whole);
    g_array_append_val(payload->f32_values, real);
  }
  return payload;
}

gboolean bosonoga_serde_write(BosonogaPayload* payload, GByteArray* bytes,
                              GError** error) {
  g_return_val_if_fail(BOSONOGA_IS_PAYLOAD(payload), FALSE);
  g_return_val_if_fail(bytes != NULL, FALSE);
  g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

  g_autoptr(ThriftTransport) transport = g_object_new(
      THRIFT_TYPE_MEMORY_BUFFER, "buf", bytes, "owner", FALSE, NULL);
  g_autoptr(ThriftProtocol) protocol =
      g_object_new(THRIFT_TYPE_COMPACT_PROTOCOL, "transport", transport, NULL);
  GError* thrift_error = NULL;
  if (thrift_struct_write(THRIFT_STRUCT(payload), protocol, &thrift_error) <
      0) {
    propagate_thrift_error(error, thrift_error, "write");
    return FALSE;
  }
  return TRUE;
}

static gboolean has_f32_values(const GArray* values) {
  for (guint i = 0; i < values->len; i++) {
    const gdouble value = g_array_index(values, gdouble, i);
    const gdouble narrowed = (gdouble)(gfloat)value;
    if (memcmp(&value, &narrowed, sizeof value) != 0) {
      return FALSE;
    }
  }
  return TRUE;
}

BosonogaPayload* bosonoga_serde_read(const GByteArray* bytes, GError** error) {
  g_return_val_if_fail(bytes != NULL, NULL);
  g_return_val_if_fail(error == NULL || *error == NULL, NULL);

  GByteArray* copy = g_byte_array_sized_new(bytes->len);
  g_byte_array_append(copy, bytes->data, bytes->len);
  g_autoptr(ThriftTransport) transport =
      g_object_new(THRIFT_TYPE_MEMORY_BUFFER, "buf", copy, "owner", TRUE, NULL);
  g_autoptr(ThriftProtocol) protocol =
      g_object_new(THRIFT_TYPE_COMPACT_PROTOCOL, "transport", transport, NULL);

  g_autoptr(BosonogaPayload) payload =
      g_object_new(BOSONOGA_TYPE_PAYLOAD, NULL);
  GError* thrift_error = NULL;
  if (thrift_struct_read(THRIFT_STRUCT(payload), protocol, &thrift_error) < 0) {
    propagate_thrift_error(error, thrift_error, "read");
    return NULL;
  }
  if (copy->len != 0) {
    g_set_error(error, BOSONOGA_ERROR, BOSONOGA_ERROR_SOURCE,
                "payload: %u bytes left over after reading", copy->len);
    return NULL;
  }
  if (payload->i32_values->len != BOSONOGA_SERDE_VALUE_COUNT ||
      payload->f32_values->len != BOSONOGA_SERDE_VALUE_COUNT) {
    g_set_error(error, BOSONOGA_ERROR, BOSONOGA_ERROR_LIMIT,
                "payload: expected %d i32 and %d f32 values, got %u and %u",
                BOSONOGA_SERDE_VALUE_COUNT, BOSONOGA_SERDE_VALUE_COUNT,
                payload->i32_values->len, payload->f32_values->len);
    return NULL;
  }
  if (!has_f32_values(payload->f32_values)) {
    g_set_error(error, BOSONOGA_ERROR, BOSONOGA_ERROR_RANGE,
                "payload: a real value is not exactly representable as f32");
    return NULL;
  }
  return g_steal_pointer(&payload);
}

static gboolean arrays_equal(const GArray* left, const GArray* right,
                             gsize element_size) {
  return left->len == right->len &&
         memcmp(left->data, right->data, left->len * element_size) == 0;
}

gboolean bosonoga_serde_equal(const BosonogaPayload* left,
                              const BosonogaPayload* right) {
  g_return_val_if_fail(left != NULL && right != NULL, FALSE);

  return arrays_equal(left->i32_values, right->i32_values, sizeof(gint32)) &&
         arrays_equal(left->f32_values, right->f32_values, sizeof(gdouble));
}
