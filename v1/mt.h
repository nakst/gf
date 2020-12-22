// TODO Floating-point values and 64-bit integers.

#ifndef MT_DEFINITIONS
#define MT_DEFINITIONS

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>

#ifdef MT_ALL
#define MT_IPC
#define MT_CONSTRUCT
#define MT_BUFFERED
#endif

#ifndef MT_FUNCTION_PREFIX
#define MT_FUNCTION_PREFIX
#endif

#ifndef MT_ASSERT
#define MT_ASSERT(x)
#endif

#define MT_NULL           (0)
#define MT_ARRAY          (1)
#define MT_MAP            (2)
#define MT_DATA           (3)
#define MT_INTEGER        (4)
#define _MT_SHORT_DATA    (5)
#define _MT_SHORT_INTEGER (6)
#define MT_CLOSE          (7)
#define MT_ERROR          (8)

typedef struct MTEntry {
	uint8_t type, construct;
	const char *key;
	
	union {
		int32_t integer;
		struct { uint8_t *data; uint32_t bytes; };
	
#ifdef MT_CONSTRUCT
		struct MTPair *map;
		struct MTEntry *array;
#endif
	};

#ifdef __cplusplus
	// Convenience functions.

#ifdef MT_CONSTRUCT
	inline MTEntry Get(uintptr_t index);
	inline MTEntry Get(const char *key);
#endif
	
	inline MTEntry Check(uint8_t _type); 
	inline int32_t ToInteger(int32_t defaultValue = 0);
	inline const char *ToString(const char *defaultValue = "");
	inline bool IsEqualTo(const char *cWith);
	inline size_t CopyToBuffer(char *buffer, size_t sizeBytes, bool clear);
#endif
} MTEntry;

typedef struct MTPair {
	char *key;
	MTEntry value;
} MTPair;

typedef struct MTInternal {
	char key[32];
	bool keySpecified;
	uint8_t errorCode;
	int nestLevel;
	uint32_t nestMaps;
	MTEntry pushback;
	bool hasPushback;
} MTInternal;

typedef bool (*MTStreamCallback)(struct MTStream *, void *, size_t);
typedef void *(*MTAllocateCallback)(struct MTStream *, size_t);
typedef void (*MTParseCallback)(struct MTStream *, char const **, uint16_t *);
typedef void (*MTPrintCallback)(const char *, size_t);

typedef struct MTStream {
	MTStreamCallback callback;
	MTAllocateCallback allocate;
	int ci, cj; void *cp; // For use in callbacks.
	uint16_t line;
	bool error;
	MTInternal _internal;
} MTStream;

MT_FUNCTION_PREFIX MTEntry MTRead(MTStream *stream);
MT_FUNCTION_PREFIX MTEntry MTReadPeek(MTStream *stream);
MT_FUNCTION_PREFIX bool MTReadFormat(MTStream *stream, const char *format, ...);
MT_FUNCTION_PREFIX bool MTReadArrayStart(MTStream *stream);
MT_FUNCTION_PREFIX bool MTReadArrayNext(MTStream *stream);

MT_FUNCTION_PREFIX bool MTWrite(MTStream *stream, MTEntry entry);
MT_FUNCTION_PREFIX bool MTWriteInteger(MTStream *stream, const char *key, int32_t value);
MT_FUNCTION_PREFIX bool MTWriteIntegerArray(MTStream *stream, const char *key, int32_t *values, size_t count);
MT_FUNCTION_PREFIX bool MTWriteString(MTStream *stream, const char *key, const char *value);
MT_FUNCTION_PREFIX bool MTWriteStringArray(MTStream *stream, const char *key, const char **values, size_t count);
MT_FUNCTION_PREFIX bool MTWriteData(MTStream *stream, const char *key, const void *data, size_t bytes);
MT_FUNCTION_PREFIX bool MTWriteArrayStart(MTStream *stream, const char *key);
MT_FUNCTION_PREFIX bool MTWriteMapStart(MTStream *stream, const char *key);
MT_FUNCTION_PREFIX bool MTWriteNull(MTStream *stream, const char *key);
MT_FUNCTION_PREFIX bool MTWriteClose(MTStream *stream);
MT_FUNCTION_PREFIX bool MTWriteFormat(MTStream *stream, const char *format, ...);

MT_FUNCTION_PREFIX bool MTParse(MTStream *stream, const char *string, MTParseCallback custom);
MT_FUNCTION_PREFIX bool MTPrint(MTStream *stream, MTPrintCallback);
MT_FUNCTION_PREFIX const char *MTErrorMessage(MTStream *stream);

// Define MT_IPC to access:
#ifdef MT_IPC
typedef void (*MTServerCallback)(MTStream *message, MTStream *response);
typedef void (*MTServerResponseCallback)(MTStream *response);
MT_FUNCTION_PREFIX bool MTServerCreate(const char *id, MTServerCallback callback);
MT_FUNCTION_PREFIX MTStream MTServerCreateMessage();
MT_FUNCTION_PREFIX bool MTServerSendMessage(const char *id, MTStream *message, MTServerResponseCallback callback);
MT_FUNCTION_PREFIX int MTServerEnterMessageLoop();
#endif

// Define MT_CONSTRUCT to access:
#ifdef MT_CONSTRUCT
MT_FUNCTION_PREFIX MTEntry MTReadConstruct(MTStream *stream);
MT_FUNCTION_PREFIX bool MTWriteConstruct(MTStream *stream, MTEntry construct);
MT_FUNCTION_PREFIX void MTFreeConstruct(MTEntry entry, void (*free)(void *));
#endif

// Define MT_BUFFERED to access:
#ifdef MT_BUFFERED
MT_FUNCTION_PREFIX MTStream MTBufferedCreateWriteStream();
MT_FUNCTION_PREFIX MTStream MTBufferedCreateReadStream(const void *buffer, size_t bytes);
MT_FUNCTION_PREFIX void MTBufferedStartReading(MTStream *stream);
MT_FUNCTION_PREFIX void MTBufferedDestroyStream(MTStream *stream);
#endif

#endif

#ifdef MT_IMPLEMENTATION
#ifdef __MT_IMPLEMENTATION
#error Please include mt.h with MT_IMPLEMENTATION defined only once.
#endif
#define __MT_IMPLEMENTATION

#define _MT_CALLBACK(a, b) if (!stream->callback(stream, a, b)) return (stream->error = true, stream->_internal.errorCode = 1, error)
#define _MT_ERROR(code) return (stream->error = true, stream->_internal.errorCode = (code), false)

#ifdef MT_IPC

static void MTIntegerEncode(uint32_t integer, char *string) {
	while (integer) {
		*string = (integer % 10) + '0';
		string++;
		integer /= 10;
	}
	
	*string = 0;
}

#endif

static size_t MTStrlen(const char *string) {
	if (!string) {
		return 0;
	}
	
	size_t bytes = 0;
	
	while (true) {
		char c = *string;
		
		if (c == 0) {
			return bytes;
		} else {
			bytes++;
			string++;
		}
	}
}

static void MTMemcpy(void *_destination, const void *_source, size_t bytes) {
	uint8_t *destination = (uint8_t *) _destination;
	const uint8_t *source = (const uint8_t *) _source;
	
	for (uintptr_t i = 0; i < bytes; i++) {
		destination[i] = source[i];
	}
}

static void MTStrcpy(char *destination, const char *source) {
	while (true) {
		char c = *source;
		*destination = c;
		if (!c) break;
		source++;
		destination++;
	}
}

static bool MTStringsEqual(const char *a, const char *b) {
	while (true) {
		char c1 = *a, c2 = *b;
		if (c1 != c2) return false;
		if (c1 == 0) return true;
		a++, b++;
	}
}

#ifdef __cplusplus

inline bool MTEntry::IsEqualTo(const char *cWith) { 
	MT_ASSERT(this && type == MT_DATA); 
	if (MTStrlen(cWith) != bytes) return false;

	for (uintptr_t i = 0; i < bytes; i++) {
		if (cWith[i] != data[i]) {
			return false;
		}
	}

	return true;
}

inline size_t MTEntry::CopyToBuffer(char *buffer, size_t sizeBytes, bool clear) {
	if (type == MT_DATA) {
		MT_ASSERT(type == MT_DATA);
		buffer[--sizeBytes] = 0;
		if (sizeBytes > bytes) sizeBytes = bytes;
		MTMemcpy(buffer, data, sizeBytes);
		return sizeBytes;
	} else if (type == MT_NULL) {
		if (clear && sizeBytes) buffer[0] = 0;
		return 0; 
	} else {
		MT_ASSERT(false);
		return 0;
	}
}

inline MTEntry MTEntry::Check(uint8_t _type) { 
	(void) _type; 
	MT_ASSERT(_type == type); 
	return *this; 
}

inline int32_t MTEntry::ToInteger(int32_t defaultValue) { 
	return type == MT_NULL ? defaultValue : Check(MT_INTEGER).integer; 
}

inline const char *MTEntry::ToString(const char *defaultValue) { 
	return type == MT_NULL ? defaultValue : (char *) Check(MT_DATA).data; 
}
#endif

MT_FUNCTION_PREFIX bool MTWrite(MTStream *stream, MTEntry entry) {
	if (stream->error) return false;
	
	size_t keyLength = (stream->_internal.nestMaps & (1 << stream->_internal.nestLevel)) ? MTStrlen(entry.key) : 0;
	bool error = false;
	
	if (keyLength >= 32) {
		stream->_internal.errorCode = 2, stream->error = true;
		return false;
	}

	if (entry.construct) {
		stream->_internal.errorCode = 3, stream->error = true;
		return false;
	}
	
	if ((entry.type == MT_INTEGER && entry.integer <= 127 && entry.integer >= -128) || (entry.type == MT_DATA && entry.bytes <= 255)) {
		entry.type += 2;
	}
	
	uint8_t header = (uint8_t) keyLength | ((uint8_t) entry.type << 5);
	_MT_CALLBACK(&header, 1);
	
	if (keyLength) {
		_MT_CALLBACK((char *) entry.key, keyLength);
	}
	
	switch (entry.type) {
		case MT_DATA: {
			_MT_CALLBACK(&entry.bytes, 4);
			_MT_CALLBACK((uint8_t *) entry.data, entry.bytes);
		} break;
		
		case _MT_SHORT_DATA: {
			uint8_t bytes = (uint8_t) entry.bytes;
			_MT_CALLBACK(&bytes, 1);
			_MT_CALLBACK((uint8_t *) entry.data, bytes);
		} break;
		
		case MT_INTEGER: {
			_MT_CALLBACK(&entry.integer, 4);
		} break;
		
		case _MT_SHORT_INTEGER: {
			int8_t integer = (int8_t) entry.integer;
			_MT_CALLBACK(&integer, 1);
		} break;
		
		case MT_CLOSE: {
			stream->_internal.nestMaps &= ~(1 << stream->_internal.nestLevel);
			stream->_internal.nestLevel--;
		} break;
		
		case MT_ARRAY: {
			stream->_internal.nestLevel++;
		} break;
		
		case MT_MAP: {
			stream->_internal.nestLevel++;
			stream->_internal.nestMaps |= (1 << stream->_internal.nestLevel);
		} break;
	}
	
	return true;
}

inline bool MTWriteInteger(MTStream *stream, const char *key, int32_t value) {
	MTEntry entry = { MT_INTEGER }; 
	entry.key = key, entry.integer = value;
	return MTWrite(stream, entry);
}

inline bool MTWriteIntegerArray(MTStream *stream, const char *key, int32_t *values, size_t count) {
	if (!MTWriteArrayStart(stream, key)) return false;
	MTEntry entry = { MT_INTEGER };
	
	for (uintptr_t i = 0; i < count; i++) {
		entry.integer = values[i];
		MTWrite(stream, entry);
	}
	
	return MTWriteClose(stream);
}

inline bool MTWriteString(MTStream *stream, const char *key, const char *value) {
	MTEntry entry = { 0 };
	entry.type = MT_DATA, entry.key = key, entry.data = (uint8_t *) value, entry.bytes = (uint32_t) MTStrlen(value);
	return MTWrite(stream, entry);
}

inline bool MTWriteStringArray(MTStream *stream, const char *key, const char **values, size_t count) {
	if (!MTWriteArrayStart(stream, key)) return false;
	MTEntry entry = { MT_DATA };
	
	for (uintptr_t i = 0; i < count; i++) {
		entry.data = (uint8_t *) values[i];
		entry.bytes = (uint32_t) MTStrlen((const char *) entry.data);
		MTWrite(stream, entry);
	}
	
	return MTWriteClose(stream);
}

inline bool MTWriteData(MTStream *stream, const char *key, const void *data, size_t bytes) {
	MTEntry entry = { 0 };
	entry.type = MT_DATA, entry.key = key, entry.data = (uint8_t *) data, entry.bytes = (uint32_t) bytes;
	return MTWrite(stream, entry);
}

inline bool MTWriteArrayStart(MTStream *stream, const char *key) {
	MTEntry entry = { MT_ARRAY };
	entry.key = key;
	return MTWrite(stream, entry);
}

inline bool MTWriteMapStart(MTStream *stream, const char *key) {
	MTEntry entry = { MT_MAP };
	entry.key = key;
	return MTWrite(stream, entry);
}

inline bool MTWriteNull(MTStream *stream, const char *key) {
	MTEntry entry = { MT_NULL };
	entry.key = key;
	return MTWrite(stream, entry);
}

inline bool MTWriteClose(MTStream *stream) {
	MTEntry entry = { MT_CLOSE };
	return MTWrite(stream, entry);
}

#define MT_TOKEN_EOF         (0)     
#define MT_TOKEN_ARRAY_START (1)     
#define MT_TOKEN_MAP_START   (2)     
#define MT_TOKEN_ARRAY_CLOSE (3)     
#define MT_TOKEN_MAP_CLOSE   (4)     
#define MT_TOKEN_IDENTIFIER  (5)     
#define MT_TOKEN_STRING      (6)     
#define MT_TOKEN_INTEGER     (7)     
#define MT_TOKEN_ARGUMENT    (8)     
#define MT_TOKEN_EQUALS      (9)     
#define MT_TOKEN_ERROR       (10)    
#define MT_TOKEN_SKIP        (11)
#define MT_TOKEN_OPTIONAL    (12)
#define MT_TOKEN_CUSTOM      (13)

typedef struct MTToken {
	int type;
	int32_t integer;
	const char *text;
	size_t bytes;
} MTToken;

static MTToken MTNextToken(char const **_format, uint16_t *line) {
	const char *format = *_format;
	MTToken token = { 0 }; token.type = MT_TOKEN_EOF, token.integer = 0, token.text = NULL, token.bytes = 0;
	
	while (true) {
		char c = *format;
		
		if (c == ' ' || c == '\t' || c == ',' || c == ';') {
			format++;
			continue;
		} else if (c == '#') {
			while (*format != 0 && *format != '\n') format++;
			continue;
		} else if (c == '\n') {
			format++;
			if (line) *line = *line + 1;
			continue;
		} else if (format[0] == 't' && format[1] == 'r' && format[2] == 'u' && format[3] == 'e') {
			token.type = MT_TOKEN_INTEGER;
			token.integer = 1;
			format += 4;
		} else if (format[0] == 'f' && format[1] == 'a' && format[2] == 'l' && format[3] == 's' && format[4] == 'e') {
			token.type = MT_TOKEN_INTEGER;
			token.integer = 0;
			format += 5;
		} else if (c == 0) {
			token.type = MT_TOKEN_EOF;
		} else if (c == '[') {
			token.type = MT_TOKEN_ARRAY_START;
			format++;
		} else if (c == '{') {
			token.type = MT_TOKEN_MAP_START;
			format++;
		} else if (c == ']') {
			token.type = MT_TOKEN_ARRAY_CLOSE;
			format++;
		} else if (c == '}') {
			token.type = MT_TOKEN_MAP_CLOSE;
			format++;
		} else if (c == '=' || c == ':') {
			token.type = MT_TOKEN_EQUALS;
			format++;
		} else if (c == '*') {
			token.type = MT_TOKEN_SKIP;
			format++;
		} else if (c == '@') {
			token.type = MT_TOKEN_CUSTOM;
			format++;
		} else if (c == '\'') {
			token.type = MT_TOKEN_STRING;
			format++;
			token.text = format;
			
			while (true) {
				c = *format;
				
				if (c == '\'') {
					format++;
					break;
				} else if (c == 0) {
					token.type = MT_TOKEN_ERROR;
					break;
				} else {
					format++;
					token.bytes++;
				}
			}
		} else if (c == '\"') {
			token.type = MT_TOKEN_STRING;
			format++;
			token.text = format;
			
			while (true) {
				c = *format;
				
				if (c == '\"') {
					format++;
					break;
				} else if (c == 0) {
					token.type = MT_TOKEN_ERROR;
					break;
				} else {
					format++;
					token.bytes++;
				}
			}
		} else if (c == '%' || c == '?') {
			token.type = c == '?' ? MT_TOKEN_OPTIONAL : MT_TOKEN_ARGUMENT;
			format++;
			token.text = format;
			
			while (true) {
				c = *format;
				
				if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
					format++;
					token.bytes++;
				} else {
					break;
				}
			}
		} else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
			token.text = format;
			token.type = MT_TOKEN_IDENTIFIER;
			
			while (true) {
				c = *format;
				
				if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
					format++;
					token.bytes++;
				} else {
					break;
				}
			}
		} else if ((c >= '0' && c <= '9') || c == '-') {
			token.type = MT_TOKEN_INTEGER;
			int negative = (c == '-') ? 1 : 0;
			if (negative) format++;
			int64_t value = 0;
			int index = 0;
			bool hex = false;
			
			while (true) {
				c = *format;
				
				if ((c >= '0' && c <= '9') || (hex && c >= 'a' && c <= 'f') || (hex && c >= 'A' && c <= 'F')) {
					format++;
					value *= hex ? 16 : 10;
					value += (c >= '0' && c <= '9') ? (c - '0') : (c >= 'a' && c <= 'f') ? (c - 'a' + 10) : (c - 'A' + 10);
					index++;
					
					if (value >= 0x80000000 + negative) {
						token.type = MT_TOKEN_ERROR;
						break;
					}
				} else if ((c == 'x' || c == 'X') && index == 1) {
					hex = true;
					format++, index++;
				} else {
					break;
				}
			}
			
			token.integer = (int32_t) (negative ? -value : value);
		} else {
			token.type = MT_TOKEN_ERROR;
		}
		
		break;
	}
	
	*_format = format;
	return token;
}

static bool MTCompareToken(MTToken token, const char *string) {
	size_t bytes = MTStrlen(string);
	
	if (bytes != token.bytes) {
		return false;
	}
	
	for (uintptr_t i = 0; i < bytes; i++) {
		if (token.text[i] != string[i]) {
			return false;
		}
	}
	
	return true;
}

static bool MTParseKey(MTStream *stream, MTInternal *internal, va_list arguments, MTToken token, const char **format, bool noArguments) {
	if (token.type == MT_TOKEN_IDENTIFIER) {
		if (token.bytes >= 32) _MT_ERROR(2);
		MTMemcpy(internal->key, token.text, token.bytes);
		internal->key[token.bytes] = 0;
		internal->keySpecified = true;
		MTToken equals = MTNextToken(format, &stream->line);
		if (equals.type != MT_TOKEN_EQUALS) _MT_ERROR(7);
	} else if (token.type == MT_TOKEN_ARGUMENT && MTCompareToken(token, "s") && !noArguments) {
		size_t bytes = va_arg(arguments, size_t);
		const char *text = va_arg(arguments, const char *);
		if (bytes >= 32) _MT_ERROR(2);
		MTMemcpy(internal->key, text, bytes);
		internal->key[bytes] = 0;
		internal->keySpecified = true;
		MTToken equals = MTNextToken(format, &stream->line);
		if (equals.type != MT_TOKEN_EQUALS) _MT_ERROR(7);
	} else if (token.type == MT_TOKEN_ARGUMENT && MTCompareToken(token, "z") && !noArguments) {
		const char *text = va_arg(arguments, const char *);
		if (MTStrlen(text) >= 32) _MT_ERROR(2);
		MTStrcpy(internal->key, text);
		internal->keySpecified = true;
		MTToken equals = MTNextToken(format, &stream->line);
		if (equals.type != MT_TOKEN_EQUALS) _MT_ERROR(7);
	} else {
		_MT_ERROR(8);
	}
	
	return true;
}

static bool MTWriteFormatInternal(MTStream *stream, const char *format, va_list arguments, bool noArguments, MTParseCallback custom) {
	MTInternal *internal = &stream->_internal;
	
	while (true) {
		if (stream->error) return false;

		bool isMap = internal->nestMaps & (1 << internal->nestLevel);
		if (!isMap) internal->key[0] = 0;
		MTToken token = MTNextToken(&format, &stream->line);
		
		if (token.type == MT_TOKEN_EOF) {
			return true;
		}
		
		if (isMap && !internal->keySpecified && token.type != MT_TOKEN_MAP_CLOSE) {
			if (!MTParseKey(stream, internal, arguments, token, &format, noArguments)) {
				return false;
			}
		} else {
			if (token.type == MT_TOKEN_MAP_CLOSE && internal->nestLevel && isMap) {
				MTWriteClose(stream);
			} else if (token.type == MT_TOKEN_ARRAY_CLOSE && internal->nestLevel && !isMap) {
				MTWriteClose(stream);
			} else if (token.type == MT_TOKEN_ARRAY_START && internal->nestLevel < 31) {
				MTWriteArrayStart(stream, internal->key);
			} else if (token.type == MT_TOKEN_MAP_START && internal->nestLevel < 31) {
				MTWriteMapStart(stream, internal->key);
			} else if (token.type == MT_TOKEN_INTEGER) {
				MTWriteInteger(stream, internal->key, token.integer);
			} else if (token.type == MT_TOKEN_STRING || token.type == MT_TOKEN_IDENTIFIER) {
				MTWriteData(stream, internal->key, token.text, token.bytes);
			} else if (token.type == MT_TOKEN_ARGUMENT && MTCompareToken(token, "s") && !noArguments) {
				size_t bytes = va_arg(arguments, size_t);
				const char *text = va_arg(arguments, const char *);
				MTWriteData(stream, internal->key, text, bytes);
			} else if (token.type == MT_TOKEN_ARGUMENT && MTCompareToken(token, "z") && !noArguments) {
				const char *text = va_arg(arguments, const char *);
				MTWriteString(stream, internal->key, text);
			} else if (token.type == MT_TOKEN_ARGUMENT && MTCompareToken(token, "i8") && !noArguments) {
				int8_t integer = va_arg(arguments, int);
				MTWriteInteger(stream, internal->key, (int32_t) integer);
			} else if (token.type == MT_TOKEN_ARGUMENT && MTCompareToken(token, "i16") && !noArguments) {
				int16_t integer = va_arg(arguments, int);
				MTWriteInteger(stream, internal->key, (int32_t) integer);
			} else if (token.type == MT_TOKEN_ARGUMENT && MTCompareToken(token, "i32") && !noArguments) {
				int32_t integer = va_arg(arguments, int32_t);
				MTWriteInteger(stream, internal->key, (int32_t) integer);
			} else if (token.type == MT_TOKEN_ARGUMENT && MTCompareToken(token, "i64") && !noArguments) {
				int64_t integer = va_arg(arguments, int64_t);
				if (integer >= 0x80000000LL || integer < -0x80000000LL) _MT_ERROR(9);
				MTWriteInteger(stream, internal->key, (int32_t) integer);
			} else if (token.type == MT_TOKEN_ARGUMENT && MTCompareToken(token, "p") && !noArguments) {
				intptr_t integer = va_arg(arguments, intptr_t);
				if (integer >= 0x80000000LL || integer < -0x80000000LL) _MT_ERROR(9);
				MTWriteInteger(stream, internal->key, (int32_t) integer);
			} else if (token.type == MT_TOKEN_ARGUMENT && MTCompareToken(token, "b") && !noArguments) {
				bool integer = va_arg(arguments, int);
				MTWriteInteger(stream, internal->key, (int32_t) integer);
			} else if (token.type == MT_TOKEN_CUSTOM && custom) {
				custom(stream, &format, &stream->line);
			} else {
				_MT_ERROR(10);
			}

			internal->keySpecified = false;
		}
	}
}

MT_FUNCTION_PREFIX bool MTWriteFormat(MTStream *stream, const char *format, ...) {
	va_list arguments;
	va_start(arguments, format);
	stream->line = 1;
	bool result = MTWriteFormatInternal(stream, format, arguments, false, NULL);
	va_end(arguments);
	return result;
}

MT_FUNCTION_PREFIX bool MTParse(MTStream *stream, const char *string, MTParseCallback custom) {
#ifdef __cplusplus
	va_list unused = {};
#else
	va_list unused = { 0 };
#endif
	stream->line = 1;
	bool result = MTWriteFormatInternal(stream, string, unused, true, custom);
	return result;
}

static MTEntry MTReadInternal(MTStream *stream, bool updateNest, bool allocateData) {
	MTEntry error = { MT_ERROR };
	if (stream->error) return error;
	
	MTEntry entry = { 0 };
	
	if (stream->_internal.hasPushback) {
		stream->_internal.hasPushback = false;
		entry = stream->_internal.pushback;
		goto gotEntry;
	}
	
	{
		uint8_t header;
		_MT_CALLBACK(&header, 1);
		
		entry.type = header >> 5;
		entry.key = stream->_internal.key;
		_MT_CALLBACK(stream->_internal.key, header & 31);
		stream->_internal.key[header & 31] = 0;
	}
	
	switch (entry.type) {
		case MT_DATA: {
			_MT_CALLBACK(&entry.bytes, 4);
			entry.data = NULL;
		} break;
		
		case _MT_SHORT_DATA: {
			uint8_t bytes;
			_MT_CALLBACK(&bytes, 1);
			entry.bytes = bytes;
			entry.type -= 2;
			entry.data = NULL;
		} break;
		
		case MT_INTEGER: {
			_MT_CALLBACK(&entry.integer, 4);
		} break;
		
		case _MT_SHORT_INTEGER: {
			int8_t integer;
			_MT_CALLBACK(&integer, 1);
			entry.integer = integer;
			entry.type -= 2;
		} break;
	}

	gotEntry:;
	
	if (allocateData && entry.type == MT_DATA && stream->allocate) {
		if ((entry.data = error.data = (uint8_t *) stream->allocate(stream, entry.bytes + 1))) {
			_MT_CALLBACK((void *) entry.data, entry.bytes);
			entry.data[entry.bytes] = 0;
		} else {
			return stream->error = true, stream->_internal.errorCode = 4, error;
		}
	}
	
	if (updateNest) {
		switch (entry.type) {
			case MT_CLOSE: {
				if (updateNest) {
					stream->_internal.nestMaps &= ~(1 << stream->_internal.nestLevel);
					stream->_internal.nestLevel--;
				}
			} break;
			
			case MT_ARRAY: {
				if (updateNest) {
					stream->_internal.nestLevel++;
				}
			} break;
			
			case MT_MAP: {
				if (updateNest) {
					stream->_internal.nestLevel++;
					stream->_internal.nestMaps |= (1 << stream->_internal.nestLevel);
				}
			} break;
		}
	}
	
	return entry;
}

MT_FUNCTION_PREFIX MTEntry MTRead(MTStream *stream) {
	return MTReadInternal(stream, true, true);
}

MT_FUNCTION_PREFIX MTEntry MTReadPeek(MTStream *stream) {
	MTEntry entry = MTReadInternal(stream, false, false);
	stream->_internal.hasPushback = true;
	stream->_internal.pushback = entry;
	return entry;
}

static bool MTReadFormatInternal(MTStream *stream, const char *format, va_list arguments) {
	bool error = false;
	if (stream->error) return false;
	
	MTInternal *internal = &stream->_internal;
	char keyBuffer[32];
	
	while (true) {
		MTToken token = MTNextToken(&format, NULL);
		if (token.type == MT_TOKEN_EOF) return true;
		
		bool isMap = internal->nestMaps & (1 << internal->nestLevel);
		bool isArgument = token.type == MT_TOKEN_ARGUMENT || token.type == MT_TOKEN_OPTIONAL;
		
		if (isMap && !internal->keySpecified && token.type != MT_TOKEN_MAP_CLOSE && token.type != MT_TOKEN_SKIP) {
			if (!MTParseKey(stream, internal, arguments, token, &format, false)) {
				return false;
			}
			
			MTStrcpy(keyBuffer, internal->key);
		} else {
			if (token.type == MT_TOKEN_MAP_CLOSE && internal->nestLevel && isMap) {
				MTEntry entry = MTReadInternal(stream, true, false);
				if (entry.type != MT_CLOSE) _MT_ERROR(11);
			} else if (token.type == MT_TOKEN_ARRAY_CLOSE && internal->nestLevel && !isMap) {
				MTEntry entry = MTReadInternal(stream, true, false);
				if (entry.type != MT_CLOSE) _MT_ERROR(11);
			} else if (token.type == MT_TOKEN_SKIP) {
				const char *backup = format;
				token = MTNextToken(&format, NULL);
				bool skipUntilKey = false;
				int nest = 0;
				
				if (isMap) {
					if (token.type != MT_TOKEN_MAP_CLOSE) {
						if (!MTParseKey(stream, internal, arguments, token, &format, false)) {
							return false;
						}
						
						skipUntilKey = true;
					}
				} else if (token.type != MT_TOKEN_ARRAY_CLOSE) {
					_MT_ERROR(11);
				}
				
				format = backup;
				if (skipUntilKey) MTStrcpy(keyBuffer, internal->key);
				
				while (true) {
					MTEntry entry = MTReadInternal(stream, false, false);
					
					if (entry.type == MT_ERROR) {
						return false;
					}
					
					if (skipUntilKey) {
						if (entry.type == MT_CLOSE && nest) {
							nest--;
						} else if (entry.type == MT_CLOSE && !nest) {
							_MT_ERROR(14);
						} else if (entry.type == MT_ARRAY || entry.type == MT_MAP) {
							nest++;
						} else if (!nest && MTStringsEqual(entry.key, keyBuffer)) {
							internal->pushback = entry, internal->hasPushback = true;
							break;
						} 
					} else /* Skip until MT_CLOSE */ {
						if (entry.type == MT_CLOSE && nest) {
							nest--;
						} else if (entry.type == MT_CLOSE && !nest) {
							internal->pushback = entry, internal->hasPushback = true;
							break;
						} else if (entry.type == MT_ARRAY || entry.type == MT_MAP) {
							nest++;
						}
					}
					
					if (entry.type == MT_DATA) {
						_MT_CALLBACK(NULL, entry.bytes);
					}
				}
				
				internal->keySpecified = false;
			} else {
				internal->keySpecified = false;
				MTEntry entry = MTReadInternal(stream, true, false);
				if (entry.type == MT_ERROR) return false;
				bool ignored = false;
				
				if (isMap && !MTStringsEqual(entry.key, keyBuffer)) {
					if (token.type == MT_TOKEN_OPTIONAL) {
						stream->_internal.pushback = entry, stream->_internal.hasPushback = true;
						ignored = true;
					} else {
						_MT_ERROR(12);
					}
				}
					
				if (token.type == MT_TOKEN_ARRAY_START && internal->nestLevel < 31) {
					if (entry.type != MT_ARRAY) _MT_ERROR(13);
				} else if (token.type == MT_TOKEN_MAP_START && internal->nestLevel < 31) {
					if (entry.type != MT_MAP) _MT_ERROR(13);
				} else if (isArgument && MTCompareToken(token, "s")) {
					size_t *bytes = va_arg(arguments, size_t *);
					void **buffer = va_arg(arguments, void **);
					
					if (!ignored) {
						if (entry.type != MT_DATA) _MT_ERROR(13);
						*bytes = entry.bytes;
						if (!(*buffer = stream->allocate(stream, entry.bytes))) _MT_ERROR(4);
						_MT_CALLBACK(*buffer, entry.bytes);
					}
				} else if (isArgument && MTCompareToken(token, "z")) {
					char **buffer = va_arg(arguments, char **);
					
					if (!ignored) {
						if (entry.type != MT_DATA) _MT_ERROR(13);
						if (!(*buffer = (char *) stream->allocate(stream, entry.bytes + 1))) _MT_ERROR(4);
						_MT_CALLBACK(*buffer, entry.bytes);
						(*buffer)[entry.bytes] = 0;
					}
				} else if (isArgument && MTCompareToken(token, "a")) {
					size_t bytes = va_arg(arguments, size_t) - 1;
					char *text = va_arg(arguments, char *);
					
					if (!ignored) {
						if (entry.type != MT_DATA) _MT_ERROR(13);
						if (bytes > entry.bytes) bytes = entry.bytes;
						_MT_CALLBACK((void *) text, bytes);
						_MT_CALLBACK(NULL, entry.bytes - bytes);
						text[bytes] = 0;
					}
				} else if (isArgument && MTCompareToken(token, "A")) {
					size_t bytes = va_arg(arguments, size_t);
					char *text = va_arg(arguments, char *);
					
					if (!ignored) {
						if (entry.type != MT_DATA) _MT_ERROR(13);
						if (bytes > entry.bytes) bytes = entry.bytes;
						_MT_CALLBACK((void *) text, bytes);
						_MT_CALLBACK(NULL, entry.bytes - bytes);
					}
				} else if (isArgument && MTCompareToken(token, "i8")) {
					int8_t *integer = va_arg(arguments, int8_t *);
					
					if (!ignored) {
						if (entry.type != MT_INTEGER) _MT_ERROR(13);
						*integer = (int8_t) entry.integer;
					}
				} else if (isArgument && MTCompareToken(token, "i16")) {
					int16_t *integer = va_arg(arguments, int16_t *);
					
					if (!ignored) {
						if (entry.type != MT_INTEGER) _MT_ERROR(13);
						*integer = (int16_t) entry.integer;
					}
				} else if (isArgument && MTCompareToken(token, "i32")) {
					int32_t *integer = va_arg(arguments, int32_t *);
					
					if (!ignored) {
						if (entry.type != MT_INTEGER) _MT_ERROR(13);
						*integer = (int32_t) entry.integer;
					}
				} else if (isArgument && MTCompareToken(token, "i64")) {
					int64_t *integer = va_arg(arguments, int64_t *);
					
					if (!ignored) {
						if (entry.type != MT_INTEGER) _MT_ERROR(13);
						*integer = (int64_t) entry.integer;
					}
				} else if (isArgument && MTCompareToken(token, "p")) {
					intptr_t *integer = va_arg(arguments, intptr_t *);
					
					if (!ignored) {
						if (entry.type != MT_INTEGER) _MT_ERROR(13);
						*integer = (intptr_t) entry.integer;
					}
				} else if (isArgument && MTCompareToken(token, "b")) {
					bool *integer = va_arg(arguments, bool *);
					
					if (!ignored) {
						if (entry.type != MT_INTEGER) _MT_ERROR(13);
						*integer = (bool) entry.integer;
					}
				} else {
					_MT_ERROR(10);
				}
			}
		}
	}
}

MT_FUNCTION_PREFIX bool MTReadFormat(MTStream *stream, const char *format, ...) {
	va_list arguments;
	va_start(arguments, format);
	bool result = MTReadFormatInternal(stream, format, arguments);
	va_end(arguments);
	return result;
}

inline bool MTReadArrayStart(MTStream *stream) {
	return MTReadFormat(stream, "[");
}

MT_FUNCTION_PREFIX bool MTReadArrayNext(MTStream *stream) {
	MTEntry entry = MTReadInternal(stream, false, false);
	bool result = entry.type == MT_CLOSE || stream->error;
	stream->_internal.hasPushback = true;
	stream->_internal.pushback = entry;
	if (result) MTRead(stream);
	return result;
}

static const char *_mtErrorMessages[] = {
	"Unknown error",		// 0
	"Callback failed",		// 1
	"Key too long",			// 2
	"Cannot write construct",	// 3
	"Allocate failed",		// 4
	"Server error",			// 5
	"Cannot read file",		// 6
	"Expected equals",		// 7
	"Expected key",			// 8
	"Value out of range",		// 9
	"Unrecognised token",		// 10
	"Expected closing bracket",	// 11
	"Expected optional marker",	// 12
	"Wrong entry type",		// 13
	"Too many closing brackets",	// 14
#define _MT_ERROR_MESSAGE_COUNT (15)
};		

MT_FUNCTION_PREFIX const char *MTErrorMessage(MTStream *stream) {
	if (stream->_internal.errorCode >= _MT_ERROR_MESSAGE_COUNT) {
		stream->_internal.errorCode = 0;
	}

	return _mtErrorMessages[stream->_internal.errorCode];
}

MT_FUNCTION_PREFIX bool MTPrint(MTStream *stream, void (*callback)(const char *, size_t)) {
	if (stream->error) return false;
	
	const char *indent = "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t";
	MTInternal *internal = &stream->_internal;
	bool error = false;
	int startNestLevel = internal->nestLevel;
	
	do {
		bool isMap = internal->nestMaps & (1 << internal->nestLevel);
		int nestLevel = internal->nestLevel;
		
		MTEntry entry = MTReadInternal(stream, true, false);
		if (stream->error) return false;
		
		if (entry.type == MT_CLOSE) nestLevel--;
		callback(indent, nestLevel);
		
		if (entry.key[0]) {
			callback(entry.key, MTStrlen(entry.key));
			callback(" = ", 3);
		}
		
		if (entry.type == MT_NULL) {
			callback("null", 4);
		} else if (entry.type == MT_MAP) {
			callback("{", 1);
		} else if (entry.type == MT_ARRAY) {
			callback("[", 1);
		} else if (entry.type == MT_CLOSE) {
			callback(isMap ? "}" : "]", 1);
		} else if (entry.type == MT_DATA) {
			char buffer[64] = { 0 };
			int bytesToRead = sizeof(buffer) - 1;
			if (entry.bytes < (size_t) bytesToRead) bytesToRead = entry.bytes;
			_MT_CALLBACK((void *) buffer, bytesToRead);
			_MT_CALLBACK(NULL, entry.bytes - bytesToRead);
			
			for (int i = 0; i < bytesToRead; i++) {
				callback(buffer + i, 1);
			}
			
			if ((size_t) bytesToRead < entry.bytes) {
				callback("...", 3);
			}
		} else if (entry.type == MT_INTEGER) {
			int32_t integer = entry.integer;
				
			if (integer == 0) {
				callback("0", 1);
			} else {
				char buffer[16];
				int i = 0;
				bool negative = integer < 0;
				
				if (negative) {
					callback("-", 1);
					integer = -integer;
				}
				
				while (integer) {
					buffer[i++] = (integer % 10) + '0';
					integer /= 10;
				}
				
				while (i) {
					callback(buffer + (--i), 1);
				}
			}
		}
		
		callback("\n", 1);
	} while (internal->nestLevel != startNestLevel);
	
	return true;
}

#ifdef MT_IPC

#ifdef _WIN32

#include <windows.h>

static bool MTServerWriteCallback(MTStream *stream, void *buffer, size_t bytes) {
	if (stream->ci + bytes > stream->cj) {
		stream->cj = (stream->ci + bytes) * 2;
		if (stream->cp) stream->cp = HeapReAlloc(GetProcessHeap(), 0, stream->cp, stream->cj);
		else stream->cp = HeapAlloc(GetProcessHeap(), 0, stream->cj);
		if (!stream->cp) return false;
	}
	
	MTMemcpy((uint8_t *) stream->cp + stream->ci, buffer, bytes);
	stream->ci += bytes;
	return true;
}

static bool MTServerReadCallback(MTStream *stream, void *buffer, size_t bytes) {
	COPYDATASTRUCT *copy = (COPYDATASTRUCT *) stream->cp;
	
	if (bytes + stream->ci > copy->cbData) {
		return false;
	} else if (buffer) {
		MTMemcpy(buffer, (uint8_t *) copy->lpData + stream->ci, bytes);
	}
	
	stream->ci += bytes;
	return true;
}

static LRESULT CALLBACK MTServerResponseWindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
	if (message == WM_COPYDATA) {
		COPYDATASTRUCT *copy = (COPYDATASTRUCT *) lParam;
		MTStream stream = { 0 };
		stream.callback = MTServerReadCallback, stream.cp = (void *) lParam;
		MTServerResponseCallback callback = ((MTServerResponseCallback) GetWindowLongPtr(window, GWLP_USERDATA));
		if (callback) callback(&stream);
		return TRUE;
	} else {
		return DefWindowProc(window, message, wParam, lParam);
	}
}

MT_FUNCTION_PREFIX bool MTServerSendMessage(const char *id, MTStream *stream, MTServerResponseCallback callback) {
	WNDCLASS windowClass = { 0 };
	windowClass.lpfnWndProc = MTServerResponseWindowProcedure;
	windowClass.lpszClassName = "MTServerResponseWC";
	RegisterClass(&windowClass);
	char className[100];
	if (stream->error) return false;
	stream->error = true;
	stream->_internal.errorCode = 5;
	if (MTStrlen(id) > 80) return false;
	MTStrcpy(className, "MTServerWC");
	MTStrcpy(className + 10, id);
	HWND target = FindWindow(className, NULL);
	if (!target) return false;
	DWORD receiveID = GetTickCount();
	char title[16];
	MTIntegerEncode(receiveID, title);
	COPYDATASTRUCT copy = { receiveID, (DWORD) stream->ci, stream->cp };
	HWND response = CreateWindow("MTServerResponseWC", title, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	if (!response) return false;
	SetWindowLongPtr(response, GWLP_USERDATA, (LONG_PTR) callback);
	SendMessage(target, WM_COPYDATA, 0, (LPARAM) &copy);
	DestroyWindow(response);
	if (stream->cj) HeapFree(GetProcessHeap(), 0, stream->cp);
	stream->error = false;
	stream->_internal.errorCode = 0;
	return true;
}

MT_FUNCTION_PREFIX MTStream MTServerCreateMessage() {
	MTStream stream = { 0 }; 
	stream.callback = MTServerWriteCallback;
	return stream;
}

static LRESULT CALLBACK MTServerWindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
	if (message == WM_COPYDATA) {
		COPYDATASTRUCT *copy = (COPYDATASTRUCT *) lParam;
		MTStream stream = { 0 };
		stream.callback = MTServerReadCallback, stream.cp = (void *) lParam;
		MTStream response = { 0 };
		response.callback = MTServerWriteCallback;
		((MTServerCallback) GetWindowLongPtr(window, GWLP_USERDATA))(&stream, &response);
		
		char title[16];
		MTIntegerEncode(copy->dwData, title);
		HWND responseWindow = FindWindow("MTServerResponseWC", title);
		
		if (responseWindow) {
			COPYDATASTRUCT copy = { 0, (DWORD) response.ci, response.cp };
			if (response.error) copy.cbData = 0, copy.lpData = NULL;
			SendMessage(responseWindow, WM_COPYDATA, 0, (LPARAM) &copy);
		}
		
		if (response.cj) HeapFree(GetProcessHeap(), 0, response.cp);
		return TRUE;
	} else {
		return DefWindowProc(window, message, wParam, lParam);
	}
}

MT_FUNCTION_PREFIX bool MTServerCreate(const char *id, MTServerCallback callback) {
	char className[100];
	if (MTStrlen(id) > 80) return false;
	MTStrcpy(className, "MTServerWC");
	MTStrcpy(className + 10, id);
	WNDCLASS windowClass = { 0 };
	windowClass.lpfnWndProc = MTServerWindowProcedure;
	windowClass.lpszClassName = className;
	HWND window = CreateWindow((LPCSTR) RegisterClass(&windowClass), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	if (!window) return false;
	SetWindowLongPtr(window, GWLP_USERDATA, (LONG_PTR) callback);
	return true;
}

MT_FUNCTION_PREFIX int MTServerEnterMessageLoop() {
	MSG message;
	
	while (GetMessage(&message, NULL, 0, 0) > 0) {
		TranslateMessage(&message);
		DispatchMessage(&message);
	}

	return message.wParam;
}

#endif

#ifdef OS_ESSENCE
// TODO.
#endif

#endif

#ifdef MT_CONSTRUCT

inline MTEntry MTEntry::Get(uintptr_t index) { 
	MT_ASSERT(type == MT_ARRAY && index < arrlenu(array)); 
	return array[index]; 
}
	
inline MTEntry MTEntry::Get(const char *key) { 
	MT_ASSERT(type == MT_MAP); 
	return shget(map, key); 
}

static MTEntry MTReadConstructInternal(MTStream *stream, char *key) {
	MTEntry entry = MTRead(stream);
	if (entry.key) MTMemcpy(key, entry.key, 32);
	entry.key = key;
	
	if (!stream->error) {
		char key2[32];
	
		if (entry.type == MT_ARRAY) {
			entry.array = NULL;
			entry.construct = true;
			
			while (true) {
				MTEntry subentry = MTReadConstructInternal(stream, key2);
				if (subentry.type == MT_CLOSE || stream->error) break;
				stbds_arrput(entry.array, subentry);
			}
		} else if (entry.type == MT_MAP) {
			entry.map = NULL;
			entry.construct = true;
			stbds_sh_new_strdup(entry.map);
			
			while (true) {
				MTEntry subentry = MTReadConstructInternal(stream, key2);
				if (subentry.type == MT_CLOSE || stream->error) break;
				stbds_shput(entry.map, subentry.key, subentry);
			}
		}
	}
		
	return entry;
}

MT_FUNCTION_PREFIX MTEntry MTReadConstruct(MTStream *stream) {
	if (!stream->allocate) {
		MTEntry error = { MT_ERROR };
		stream->error = true;
		stream->_internal.errorCode = 4;
		return error;
	}
	
	char key[32];
	return MTReadConstructInternal(stream, key);
}

MT_FUNCTION_PREFIX bool MTWriteConstruct(MTStream *stream, MTEntry entry) {
	if (stream->error) return false;
	
	if (entry.type == MT_ARRAY && entry.construct) {
		MTWriteArrayStart(stream, entry.key);
		
		for (uintptr_t i = 0; i < stbds_arrlenu(entry.array); i++) {
			MTWriteConstruct(stream, entry.array[i]);
		}
		
		MTWriteClose(stream);
	} else if (entry.type == MT_MAP && entry.construct) {
		MTWriteMapStart(stream, entry.key);
		
		for (uintptr_t i = 0; i < stbds_shlenu(entry.map); i++) {
			entry.map[i].value.key = entry.map[i].key;
			MTWriteConstruct(stream, entry.map[i].value);
		}
		
		MTWriteClose(stream);
	} else {
		MTWrite(stream, entry);
	}
	
	return true;
}

MT_FUNCTION_PREFIX void MTFreeConstruct(MTEntry entry, void (*_free)(void *)) {
	if (entry.type == MT_ARRAY && entry.construct) {
		for (uintptr_t i = 0; i < stbds_arrlenu(entry.array); i++) {
			MTFreeConstruct(entry.array[i], _free);
		}
		
		stbds_arrfree(entry.array);
	} else if (entry.type == MT_MAP && entry.construct) {
		for (uintptr_t i = 0; i < stbds_shlenu(entry.map); i++) {
			MTFreeConstruct(entry.map[i].value, _free);
		}
		
		stbds_shfree(entry.map);
	} else if (entry.type == MT_DATA && _free) {
		_free((void *) entry.data);
	}
}

#endif

#ifdef MT_BUFFERED

#ifdef OS_ESSENCE
#ifndef _MT_REALLOC
#define _MT_REALLOC(x, y) EsHeapReallocate(x, y, false)
#endif
#ifndef _MT_ALLOC
#define _MT_ALLOC(x) EsHeapAllocate(x, false)
#endif
#ifndef _MT_FREE
#define _MT_FREE EsHeapFree
#endif
#else
#include <stdlib.h>
#include <string.h>
#ifndef _MT_REALLOC
#define _MT_REALLOC realloc
#endif
#ifndef _MT_ALLOC
#define _MT_ALLOC malloc
#endif
#ifndef _MT_FREE
#define _MT_FREE free
#endif
#endif

static bool MTBufferedWriteCallback(MTStream *stream, void *buffer, size_t bytes) {
	if (stream->ci + (int) bytes > stream->cj) {
		stream->cj = (stream->ci + bytes) * 2;
		void *old = stream->cp;
		stream->cp = _MT_REALLOC(stream->cp, stream->cj);

		if (!stream->cp) {
			_MT_FREE(old);
			return false;
		}
	}
	
	MTMemcpy((uint8_t *) stream->cp + stream->ci, buffer, bytes);
	stream->ci += bytes;
	return true;
}

static bool MTBufferedReadCallback(MTStream *stream, void *buffer, size_t bytes) {
	if (stream->ci + (int) bytes > stream->cj) {
		return false;
	}

	if (buffer) {
		MTMemcpy(buffer, (uint8_t *) stream->cp + stream->ci, bytes);
	}
	
	stream->ci += bytes;
	return true;
}

static void *MTBufferedAllocateCallback(MTStream *stream, size_t size) {
	(void) stream;
	return _MT_ALLOC(size);
}

MT_FUNCTION_PREFIX MTStream MTBufferedCreateWriteStream() {
	MTStream stream = { 0 };
	stream.callback = MTBufferedWriteCallback;
	stream.allocate = MTBufferedAllocateCallback;
	return stream;
}

MT_FUNCTION_PREFIX MTStream MTBufferedCreateReadStream(const void *buffer, size_t bytes) {
	MTStream stream = { 0 };
	stream.callback = MTBufferedReadCallback;
	stream.allocate = MTBufferedAllocateCallback;
	stream.cj = bytes, stream.cp = (void *) buffer;
	return stream;
}

MT_FUNCTION_PREFIX void MTBufferedStartReading(MTStream *stream) {
	stream->cj = stream->ci, stream->ci = 0, stream->callback = MTBufferedReadCallback;
}

MT_FUNCTION_PREFIX void MTBufferedDestroyStream(MTStream *stream) {
	_MT_FREE(stream->cp);
}

#endif

#endif

/*
------------------------------------------------------------------------------
This software is available under 2 licenses -- choose whichever you prefer.
------------------------------------------------------------------------------
ALTERNATIVE A - MIT License
Copyright (c) 2019 nakst
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
------------------------------------------------------------------------------
ALTERNATIVE B - Public Domain (www.unlicense.org)
This is free and unencumbered software released into the public domain.
Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
software, either in source code form or as a compiled binary, for any purpose,
commercial or non-commercial, and by any means.
In jurisdictions that recognize copyright laws, the author or authors of this
software dedicate any and all copyright interest in the software to the public
domain. We make this dedication for the benefit of the public at large and to
the detriment of our heirs and successors. We intend this dedication to be an
overt act of relinquishment in perpetuity of all present and future rights to
this software under copyright law.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
------------------------------------------------------------------------------
*/
