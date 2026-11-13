#ifndef LAMA_VM_READER_H
#define LAMA_VM_READER_H

#include "vm.h"

struct byte_reader {
	const uint8_t *data;
	size_t size;
	size_t pos;
	size_t origin;
	size_t unit_id;
};

void reader_init(struct byte_reader *reader, size_t unit_id, const void *data,
		 size_t size, size_t origin);
void reader_seek(struct byte_reader *reader, size_t pos);
const uint8_t *reader_read(struct byte_reader *reader, size_t count);
uint8_t reader_u8(struct byte_reader *reader);
int32_t reader_i32(struct byte_reader *reader);

#endif
