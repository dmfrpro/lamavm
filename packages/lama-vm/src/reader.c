#include "reader.h"

#include "unit.h"
#include "util.h"

#include <string.h>

static void reader_need(struct byte_reader *reader, size_t count)
{
	if (reader->pos > reader->size || count > reader->size - reader->pos)
		die_at(unit_name(unit_get(reader->unit_id)),
		       reader->origin + reader->pos, "truncated %zu-byte field",
		       count);
}

void reader_init(struct byte_reader *reader, size_t unit_id, const void *data,
		 size_t size, size_t origin)
{
	reader->data = data;
	reader->size = size;
	reader->pos = 0;
	reader->origin = origin;
	reader->unit_id = unit_id;
}

void reader_seek(struct byte_reader *reader, size_t pos)
{
	if (pos > reader->size)
		die_at(unit_name(unit_get(reader->unit_id)),
		       reader->origin + pos,
		       "offset is outside the %zu-byte section", reader->size);
	reader->pos = pos;
}

const uint8_t *reader_read(struct byte_reader *reader, size_t count)
{
	const uint8_t *p;

	reader_need(reader, count);
	p = reader->data + reader->pos;
	reader->pos += count;
	return p;
}

uint8_t reader_u8(struct byte_reader *reader)
{
	return *reader_read(reader, 1);
}

int32_t reader_i32(struct byte_reader *reader)
{
	const uint8_t *p = reader_read(reader, sizeof(int32_t));
	int32_t v;

	memcpy(&v, p, sizeof(v));
	return v;
}
