#ifndef _JSON_H_
#define _JSON_H_

#include <stdint.h>

#define JSON_TYPE_ERROR  0
#define JSON_TYPE_NULL   1
#define JSON_TYPE_FALSE  2
#define JSON_TYPE_TRUE   3
#define JSON_TYPE_STRING 4
#define JSON_TYPE_INT    5
#define JSON_TYPE_DOUBLE 6
#define JSON_TYPE_MAP    7
#define JSON_TYPE_ARRAY  8
#define JSON_TYPE_INDEX  9

typedef uint32_t json_utf8_t;

typedef struct json_object{
	int type;
	uint8_t *start;
	int len;
	int count;
}__attribute__((packed)) json_object_t;

int json_check(uint8_t *ptr, int len, json_object_t *object);
int json_get(uint8_t *ptr, int len, int index, json_object_t *key, json_object_t *value);
int json_get_value(uint8_t *ptr, int len, char *key, json_object_t *value);
int json_isequal(json_object_t *jstr, char *str);
int json_value_isequal(uint8_t *ptr, int len, char *key, char *value);

int json_decode_string(const char *ptr, int len, json_utf8_t *utf8_char);

long long int json_int(json_object_t *ob);
double json_double(json_object_t *ob);

#ifdef MAIN_CONTROLLER
#import <Foundation/Foundation.h>
@interface JSON_INIT: NSObject{
}
@end
@interface NSData (Json)
+(NSData *)dataFromDataToJson: (id)data;
@end
@interface NSString (Json)
-(id)jsonToData;
@end
id json_unserialize(const void *str, int len);
#endif

#endif
