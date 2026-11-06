#ifndef BOSONOGA_SERDE_H
#define BOSONOGA_SERDE_H

#include <bosonoga_payload_types.h>

#include "../bosonoga/bosonoga.h"

G_BEGIN_DECLS

#define BOSONOGA_SERDE_VALUE_COUNT BOSONOGA_PAYLOAD_VALUE_COUNT
#define BOSONOGA_SERDE_I32_MIN 10
#define BOSONOGA_SERDE_I32_MAX 99
#define BOSONOGA_SERDE_F32_HUNDREDTHS_MIN 1000
#define BOSONOGA_SERDE_F32_HUNDREDTHS_MAX 9999

G_DEFINE_AUTOPTR_CLEANUP_FUNC(BosonogaPayload, g_object_unref)

BosonogaPayload* bosonoga_serde_random_payload(GRand* rand);

gboolean bosonoga_serde_write(BosonogaPayload* payload, GByteArray* bytes,
                              GError** error);

BosonogaPayload* bosonoga_serde_read(const GByteArray* bytes, GError** error);

gboolean bosonoga_serde_equal(const BosonogaPayload* left,
                              const BosonogaPayload* right);

G_END_DECLS

#endif
