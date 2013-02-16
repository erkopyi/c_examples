#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "json.h"

#ifdef MAIN_CONTROLLER 
#import <Foundation/Foundation.h>
#import "NSUtil.h"
#endif

#define JSON_STACK_LEN 32

#define true  1
#define false 0
#define ___   -1     /* the universal error code */

struct JSON_struct{
	int state;
	int top;
	int stack[JSON_STACK_LEN];
	uint8_t *start;
	uint8_t *key_start;
	int key_len;
	uint8_t *value_start;
	int value_len;
	int value_count;
	int value_type;
	int len;
	int type;
	int count;
};

static json_utf8_t hex_to_utf8(uint8_t c);
static int JSON_checker_char(struct JSON_struct *jc, uint8_t *next_char);

/*
    Characters are mapped into these 31 character classes. This allows for
    a significant reduction in the size of the state transition table.
*/

enum classes {
    C_SPACE,  /* space */
    C_WHITE,  /* other whitespace */
    C_LCURB,  /* {  */
    C_RCURB,  /* } */
    C_LSQRB,  /* [ */
    C_RSQRB,  /* ] */
    C_COLON,  /* : */
    C_COMMA,  /* , */
    C_QUOTE,  /* " */
    C_BACKS,  /* \ */
    C_SLASH,  /* / */
    C_PLUS,   /* + */
    C_MINUS,  /* - */
    C_POINT,  /* . */
    C_ZERO ,  /* 0 */
    C_DIGIT,  /* 123456789 */
    C_LOW_A,  /* a */
    C_LOW_B,  /* b */
    C_LOW_C,  /* c */
    C_LOW_D,  /* d */
    C_LOW_E,  /* e */
    C_LOW_F,  /* f */
    C_LOW_L,  /* l */
    C_LOW_N,  /* n */
    C_LOW_R,  /* r */
    C_LOW_S,  /* s */
    C_LOW_T,  /* t */
    C_LOW_U,  /* u */
    C_ABCDF,  /* ABCDF */
    C_E,      /* E */
    C_ETC,    /* everything else */
    C_END,    /* buffer end */
    NR_CLASSES
};

static int ascii_class[128] = {
/*
    This array maps the 128 ASCII characters into character classes.
    The remaining Unicode characters should be mapped to C_ETC.
    Non-whitespace control characters are errors.
*/
    ___,      ___,      ___,      ___,      ___,      ___,      ___,      ___,
    ___,      C_WHITE, C_WHITE, ___,      ___,      C_WHITE, ___,      ___,
    ___,      ___,      ___,      ___,      ___,      ___,      ___,      ___,
    ___,      ___,      ___,      ___,      ___,      ___,      ___,      ___,

    C_SPACE, C_ETC,   C_QUOTE, C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,
    C_ETC,   C_ETC,   C_ETC,   C_PLUS,  C_COMMA, C_MINUS, C_POINT, C_SLASH,
    C_ZERO,  C_DIGIT, C_DIGIT, C_DIGIT, C_DIGIT, C_DIGIT, C_DIGIT, C_DIGIT,
    C_DIGIT, C_DIGIT, C_COLON, C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,

    C_ETC,   C_ABCDF, C_ABCDF, C_ABCDF, C_ABCDF, C_E,     C_ABCDF, C_ETC,
    C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,
    C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,
    C_ETC,   C_ETC,   C_ETC,   C_LSQRB, C_BACKS, C_RSQRB, C_ETC,   C_ETC,

    C_ETC,   C_LOW_A, C_LOW_B, C_LOW_C, C_LOW_D, C_LOW_E, C_LOW_F, C_ETC,
    C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_LOW_L, C_ETC,   C_LOW_N, C_ETC,
    C_ETC,   C_ETC,   C_LOW_R, C_LOW_S, C_LOW_T, C_LOW_U, C_ETC,   C_ETC,
    C_ETC,   C_ETC,   C_ETC,   C_LCURB, C_ETC,   C_RCURB, C_ETC,   C_ETC
};


/*
    The state codes.
*/
enum states {
    GO,  /* start    */
    OK,  /* ok       */
    OB,  /* object   */
    KE,  /* key      */
    CO,  /* colon    */
    VA,  /* value    */
    AR,  /* array    */
    ST,  /* string   */
    ES,  /* escape   */
    U1,  /* u1       */
    U2,  /* u2       */
    U3,  /* u3       */
    U4,  /* u4       */
    MI,  /* minus    */
    ZE,  /* zero     */
    IN,  /* integer  */
    FR,  /* fraction */
    E1,  /* e        */
    E2,  /* ex       */
    E3,  /* exp      */
    T1,  /* tr       */
    T2,  /* tru      */
    T3,  /* true     */
    F1,  /* fa       */
    F2,  /* fal      */
    F3,  /* fals     */
    F4,  /* false    */
    N1,  /* nu       */
    N2,  /* nul      */
    N3,  /* null     */
    NR_STATES
};


static int state_transition_table[NR_STATES][NR_CLASSES] = {
/*
    The state transition table takes the current state and the current symbol,
    and returns either a new state or an action. An action is represented as a
    negative number. A JSON text is accepted if at the end of the text the
    state is OK and if the mode is MODE_DONE.

                 white                                                     1-9                                                ABCDF    etc
             space  |   {   }   [   ]   :   ,   "   \   /   +   -   .   0   |   a   b   c   d   e   f   l   n   r   s   t   u   |   E   |  END */
/*start  GO*/ { GO, GO, -6,___, -5,___,___,___, ST,___,___, MI, MI,___, ZE, IN,___,___,___,___,___, F1,___, N1,___,___, T1,___,___,___,___,___},
/*ok     OK*/ { OK, OK,___, -8,___, -7,___, -3,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___, OK},
/*object OB*/ { OB, OB,___, -9,___,___,___,___, ST,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___},
/*key    KE*/ { KE, KE,___,___,___,___,___,___, ST,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___},
/*colon  CO*/ { CO, CO,___,___,___,___, -2,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___},
/*value  VA*/ { VA, VA, -6,___, -5,___,___,___, ST,___,___, MI, MI,___, ZE, IN,___,___,___,___,___, F1,___, N1,___,___, T1,___,___,___,___,___},
/*array  AR*/ { AR, AR, -6,___, -5, -7,___,___, ST,___,___, MI, MI,___, ZE, IN,___,___,___,___,___, F1,___, N1,___,___, T1,___,___,___,___,___},
/*string ST*/ { ST,___, ST, ST, ST, ST, ST, ST, -4, ES, ST, ST, ST, ST, ST, ST, ST, ST, ST, ST, ST, ST, ST, ST, ST, ST, ST, ST, ST, ST, ST,___},
/*escape ES*/ {___,___,___,___,___,___,___,___, ST, ST, ST,___,___,___,___,___,___, ST,___,___,___, ST,___, ST, ST,___, ST, U1,___,___,___,___},
/*u1     U1*/ {___,___,___,___,___,___,___,___,___,___,___,___,___,___, U2, U2, U2, U2, U2, U2, U2, U2,___,___,___,___,___,___, U2, U2,___,___},
/*u2     U2*/ {___,___,___,___,___,___,___,___,___,___,___,___,___,___, U3, U3, U3, U3, U3, U3, U3, U3,___,___,___,___,___,___, U3, U3,___,___},
/*u3     U3*/ {___,___,___,___,___,___,___,___,___,___,___,___,___,___, U4, U4, U4, U4, U4, U4, U4, U4,___,___,___,___,___,___, U4, U4,___,___},
/*u4     U4*/ {___,___,___,___,___,___,___,___,___,___,___,___,___,___, ST, ST, ST, ST, ST, ST, ST, ST,___,___,___,___,___,___, ST, ST,___,___},
/*minus  MI*/ {___,___,___,___,___,___,___,___,___,___,___,___,___,___, ZE, IN,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___},
/*zero   ZE*/ { OK, OK,___, -8,___, -7,___, -3,___,___,___,___,___, FR,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___, OK},
/*int    IN*/ { OK, OK,___, -8,___, -7,___, -3,___,___,___,___,___, FR, IN, IN,___,___,___,___, E1,___,___,___,___,___,___,___,___, E1,___, OK},
/*frac   FR*/ { OK, OK,___, -8,___, -7,___, -3,___,___,___,___,___,___, FR, FR,___,___,___,___, E1,___,___,___,___,___,___,___,___, E1,___, OK},
/*e      E1*/ {___,___,___,___,___,___,___,___,___,___,___, E2, E2,___, E3, E3,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___},
/*ex     E2*/ {___,___,___,___,___,___,___,___,___,___,___,___,___,___, E3, E3,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___},
/*exp    E3*/ { OK, OK,___, -8,___, -7,___, -3,___,___,___,___,___,___, E3, E3,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___, OK},
/*tr     T1*/ {___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___, T2,___,___,___,___,___,___,___},
/*tru    T2*/ {___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___, T3,___,___,___,___},
/*true   T3*/ {___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___, OK,___,___,___,___,___,___,___,___,___,___,___},
/*fa     F1*/ {___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___, F2,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___},
/*fal    F2*/ {___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___, F3,___,___,___,___,___,___,___,___,___},
/*fals   F3*/ {___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___, F4,___,___,___,___,___,___},
/*false  F4*/ {___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___, OK,___,___,___,___,___,___,___,___,___,___,___},
/*nu     N1*/ {___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___, N2,___,___,___,___},
/*nul    N2*/ {___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___, N3,___,___,___,___,___,___,___,___,___},
/*null   N3*/ {___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___,___, OK,___,___,___,___,___,___,___,___,___},
};


/*
    These modes can be pushed on the stack.
*/
enum modes {
    MODE_ARRAY, 
    MODE_DONE,  
    MODE_KEY,   
    MODE_OBJECT,
};

static int push(struct JSON_struct *jc, int mode){
/*
    Push a mode onto the stack. Return false if there is overflow.
*/
    jc->top += 1;
    if (jc->top >= JSON_STACK_LEN) {
        return false;
    }
    jc->stack[jc->top] = mode;
//    printf("MODE: %d\n", mode);
    return true;
}

static int pop(struct JSON_struct *jc, int mode){
/*
    Pop the stack, assuring that the current mode matches the expectation.
    Return false if there is underflow or if the modes mismatch.
*/
    if (jc->top < 0 || jc->stack[jc->top] != mode) {
        return false;
    }
    jc->top -= 1;
    return true;
}

static void JSON_set_type(int state, uint8_t *start, uint8_t *end, int *len, int *type);

/*
int JSON_check(uint8_t *ptr, int len){
	struct JSON_struct jc;
	int i, next_state;
	jc.state = GO;
	jc.top = -1;
	jc.type = JSON_TYPE_ERROR;
	jc.count = 0;
	jc.get = 1;
	push(&jc, MODE_DONE);
	for(i = 0; i < len; i++){
		if(!JSON_checker_char(&jc, &ptr[i])){
			jc.state = ___;
			break;
		}
	}
	if(jc.state >= 0){
		next_state = state_transition_table[jc.state][C_END];
		if((jc.state != OK) && (next_state == OK)){
	    		JSON_set_type(jc.state, jc.start, &ptr[len - 1], &jc.len, &jc.type);
		}
		jc.state = next_state;
	}
	if((jc.state == OK) && pop(&jc, MODE_DONE) && (jc.type != JSON_TYPE_ERROR)){
		printf("TYPE: %d, COUNT: %d, DATA: ", jc.type, jc.count);
		for(i = 0; i < jc.len; i++){
			printf("%c", (char)jc.start[i]);
		}
		printf(":\n");
	}
}*/

int json_check(uint8_t *ptr, int len, json_object_t *object){
	struct JSON_struct jc;
	int i, next_state;
	jc.state = GO;
	jc.top = -1;
	jc.type = JSON_TYPE_ERROR;
	jc.count = 0;
	push(&jc, MODE_DONE);
	for(i = 0; i < len; i++){
		if(!JSON_checker_char(&jc, &ptr[i])){
			jc.state = ___;
			break;
		}
	}
	if(jc.state >= 0){
		next_state = state_transition_table[jc.state][C_END];
		if((jc.state != OK) && (next_state == OK)){
	    		JSON_set_type(jc.state, jc.start, &ptr[len - 1], &jc.len, &jc.type);
		}
		jc.state = next_state;
	}
	if((jc.state == OK) && pop(&jc, MODE_DONE) && (jc.type != JSON_TYPE_ERROR)){
		object->type = jc.type;
		object->start = jc.start;
		object->len = jc.len;
		object->count = jc.count;
		return(1);
	}
	return(0);
}

int json_get(uint8_t *ptr, int len, int index, json_object_t *key, json_object_t *value){
	struct JSON_struct jc;
	int i;
	jc.state = GO;
	jc.top = -1;
	jc.type = JSON_TYPE_ERROR;
	jc.count = 0;
	jc.key_start = NULL;
	push(&jc, MODE_DONE);
	for(i = 0; i < len; i++){
		if(!JSON_checker_char(&jc, &ptr[i])){
			jc.state = ___;
			break;
		}
		if((jc.value_type != JSON_TYPE_ERROR) && jc.count && ((jc.count - 1) == index)){
			if(key != NULL){
				if(jc.key_start == NULL){
					key->type = JSON_TYPE_INDEX;
					key->start = NULL;
					key->len = 0;
					key->count = index;
				}else{
					key->type = JSON_TYPE_STRING;
					key->start = jc.key_start;
					key->len = jc.key_len;
					key->count = 0;
				}
			}
			value->type = jc.value_type;
			value->start = jc.value_start;
			value->len = jc.value_len;
			value->count = jc.value_count;
			return(1);
		}
	}
	return(0);
}

int json_get_value(uint8_t *ptr, int len, char *key, json_object_t *value){
	json_object_t table, _key;
	int i;
	if(!json_check(ptr, len, &table))return(0);
	if(table.type != JSON_TYPE_MAP)return(0);
	if(table.count < 1)return(0);
	for(i = 0; i < table.count; i++){
		if(!json_get(ptr, len, i, &_key, value))return(0);
		if(json_isequal(&_key, key))return(1);
	}
	return(0);
}

int json_isequal(json_object_t *jstr, char *str){
	int l;
	if(jstr->type != JSON_TYPE_STRING)return(0);
	l = strlen(str);
	if(jstr->len != (l + 2))return(0);
	return(!memcmp(jstr->start + 1, str, l));
}

int json_value_isequal(uint8_t *ptr, int len, char *key, char *value){
	json_object_t table, _key, _value;
	int i;
	if(!json_check(ptr, len, &table))return(0);
	if(table.type != JSON_TYPE_MAP)return(0);
	if(table.count < 1)return(0);
	for(i = 0; i < table.count; i++){
		if(!json_get(ptr, len, i, &_key, &_value))return(0);
		if(json_isequal(&_key, key)){
			if(json_isequal(&_value, value))return(1);
			return(0);
		}
	}
	return(0);
}

long long int json_int(json_object_t *ob){
	int l;
	char buffer[32];
	if(ob->type == JSON_TYPE_STRING){
		if(ob->len < 2)return(0);
		l = ob->len > 33 ? 31 : ob->len - 2;
		memcpy(&buffer[0], &ob->start[1], l);
	}else{
		if(!ob->len)return(0);
		l = ob->len > 31 ? 31 : ob->len;
		memcpy(&buffer[0], &ob->start[0], l);
	}
	buffer[l] = 0;
	return(strtoll(&buffer[0], NULL, 10));
}

double json_double(json_object_t *ob){
	int l;
	char buffer[32];
	if(ob->type == JSON_TYPE_STRING){
		if(ob->len < 2)return(0);
		l = ob->len > 33 ? 31 : ob->len - 2;
		memcpy(&buffer[0], &ob->start[1], l);
	}else{
		if(!ob->len)return(0);
		l = ob->len > 31 ? 31 : ob->len;
		memcpy(&buffer[0], &ob->start[0], l);
	}
	buffer[l] = 0;
	return(strtod(&buffer[0], NULL));
}

#ifdef MAIN_CONTROLLER 
static NSString *JSON_string(uint8_t *ptr, int len){
	json_utf8_t c;
	int i;
	NSMutableString *str;
	if(len < 2)return(@"");
	len -= 2;
	ptr += 1;
	str = [NSMutableString string];
	while(len > 0){
		i = json_decode_string((char *)ptr, len, &c);
		if(i == 0)break;
		len -= i;
		ptr += i;
		// [str appendFormat: @"%C", (unichar)c];
		[str appendCharacter: (unichar)c];
	}
	return(str);
}

static NSNumber *JSON_number(uint8_t *ptr, int len, int type){
	char numbuf[128];
	if(len > 126)len = 126;
	memcpy(&numbuf[0], ptr, len);
	numbuf[len] = 0;
	if(type == JSON_TYPE_INT){
		return([NSNumber numberWithLongLong: strtoll(&numbuf[0], NULL, 10)]);
	}else if(type == JSON_TYPE_DOUBLE){
		return([NSNumber numberWithDouble: strtod(&numbuf[0], NULL)]);
	}
	return([NSNumber numberWithInt: 0]);
}

static id JSON_unserialize_object(uint8_t *ptr, int len){
	BOOL next;
	struct JSON_struct jc;
	id table, t;
	int i, next_state;
	jc.state = GO;
	jc.top = -1;
	jc.type = JSON_TYPE_ERROR;
	jc.count = 0;
	jc.key_start = NULL;
	table = nil;
	next = NO;
	push(&jc, MODE_DONE);
	for(i = 0; i < len; i++){
		if(!JSON_checker_char(&jc, &ptr[i])){
			jc.state = ___;
			return(nil);
		}
		if((jc.value_type != JSON_TYPE_ERROR) && !next && jc.count){
			next = YES;
			if(jc.value_type == JSON_TYPE_NULL){
				t = [NSNull null];
			}else if(jc.value_type == JSON_TYPE_FALSE){
				t = [NSBoolean booleanWithBool: NO];
			}else if(jc.value_type == JSON_TYPE_TRUE){
				t = [NSBoolean booleanWithBool: YES];
			}else if(jc.value_type == JSON_TYPE_STRING){
				t = JSON_string(jc.value_start, jc.value_len);
			}else if(jc.type == JSON_TYPE_INT){
				t = JSON_number(jc.value_start, jc.value_len, jc.value_type);
			}else if(jc.type == JSON_TYPE_DOUBLE){
				t = JSON_number(jc.value_start, jc.value_len, jc.value_type);
			}else{
				t = JSON_unserialize_object(jc.value_start, jc.value_len);
			}
			if(jc.key_start == NULL){
				if(table == nil)table = [NSMutableArray array];
				[(NSMutableArray *)table addObject: t];
			}else{
				if(NSIsArray(table)){
					[(NSMutableArray *)table addObject: t];
				}else{
					table = NSAdd(table, JSON_string(jc.key_start, jc.key_len), t, nil);
				}
			}
		}else if(jc.value_type == JSON_TYPE_ERROR){
			next = NO;
		}
	}
	if(jc.state >= 0){
		next_state = state_transition_table[jc.state][C_END];
		if((jc.state != OK) && (next_state == OK)){
	    		JSON_set_type(jc.state, jc.start, &ptr[len - 1], &jc.len, &jc.type);
		}
		jc.state = next_state;
	}
	if((jc.state == OK) && pop(&jc, MODE_DONE) && (jc.type != JSON_TYPE_ERROR)){
		if(table != nil)return(table);
		if(jc.type == JSON_TYPE_NULL){
			return([NSNull null]);
		}else if(jc.type == JSON_TYPE_FALSE){
			return([NSBoolean booleanWithBool: NO]);
		}else if(jc.type == JSON_TYPE_TRUE){
			return([NSBoolean booleanWithBool: YES]);
		}else if((jc.type == JSON_TYPE_STRING) && (jc.len > 1)){
			return(JSON_string(jc.start, jc.len));
		}else if(jc.type == JSON_TYPE_INT){
			return(JSON_number(jc.start, jc.len, jc.type));
		}else if(jc.type == JSON_TYPE_DOUBLE){
			return(JSON_number(jc.start, jc.len, jc.type));
		}else if(jc.type == JSON_TYPE_MAP){
			return([NSMutableDictionary dictionary]);
		}else if(jc.type == JSON_TYPE_ARRAY){
			return([NSMutableArray array]);
		}
	}
	return(nil);
}

id json_unserialize(const void *str, int len){
	json_object_t ob;
	if(!json_check((uint8_t *)str, len, &ob))return(nil);
	return(JSON_unserialize_object((uint8_t *)str, len));
}

static NSData *NSJSONNull, *NSJSONTrue, *NSJSONFalse;

NSData *data_serialize(id data){
	NSEnumerator *e;
	NSMutableData *d;
	NSUInteger l, i;
	unichar c;
	id ob;
	uint8_t dc[8];
	if(NSIsBoolean(data)){
		if([(NSBoolean *)data value])return(NSJSONTrue);
		return(NSJSONFalse);
	}else if(NSIsNumber(data)){
		return([[(NSNumber *)data stringValue2] dataUsingEncoding: NSUTF8StringEncoding]);
	}else if(NSIsString(data)){
		l = [(NSString *)data length];
		d = [NSMutableData dataWithCapacity: l + 16];
		[d appendBytes: "\"" length: 1];
		for(i = 0; i < l; i++){
			c = [(NSString *)data characterAtIndex: i];
			if(c == '\"'){
				[d appendBytes: "\\\"" length: 2];
			}else if(c == '\\'){
				[d appendBytes: "\\\\" length: 2];
			}else if(c == '\n'){
				[d appendBytes: "\\n" length: 2];
			}else if(c == '\r'){
				[d appendBytes: "\\r" length: 2];
			}else if(c == '\t'){
				[d appendBytes: "\\t" length: 2];
			}else if(c < 128){
				dc[0] = c;
				[d appendBytes: &dc[0] length: 1];
			}else if(c < 2048){
				dc[0] = (c >> 6) | 0xC0;
				dc[1] = (c & 0x3F) | 0x80;
				[d appendBytes: &dc[0] length: 2];
			}else if(c < 65536LU){
				dc[0] = (c >> 12) | 0xE0;
				dc[1] = ((c >> 6) & 0x3F) | 0x80;
				dc[2] = (c & 0x3F) | 0x80;
				[d appendBytes: &dc[0] length: 3];
			}else if(c < 1114111LU){
				dc[0] = (c >> 18) | 0xF0;
				dc[1] = ((c >> 12) & 0x3F) | 0x80;
				dc[2] = ((c >> 6) & 0x3F) | 0x80;
				dc[3] = (c & 0x3F) | 0x80;
				[d appendBytes: &dc[0] length: 4];
			}
		}
		[d appendBytes: "\"" length: 1];
		return(d);
	}else if(NSIsArray(data)){
		d = [NSMutableData dataWithCapacity: 256];
		[d appendBytes: "[" length: 1];
		i = 0;
		e = NSObjectEnumerator(data);
		while((ob = [e nextObject])){
			if(i)[d appendBytes: "," length: 1];
			i = 1;
			[d appendData: data_serialize(ob)];
		}
		[d appendBytes: "]" length: 1];
		return(d);
	}else if(NSIsDictionary(data)){
		d = [NSMutableData dataWithCapacity: 256];
		[d appendBytes: "{" length: 1];
		i = 0;
		e = NSObjectKeyEnumerator(data);
		while((ob = [e nextObject])){
			if(!NSIsString(NSEnumeratorKey(e)))continue;
			if(i)[d appendBytes: "," length: 1];
			i = 1;
			[d appendData: data_serialize(NSEnumeratorKey(e))];
			[d appendBytes: ":" length: 1];
			[d appendData: data_serialize(ob)];
		}
		[d appendBytes: "}" length: 1];
		return(d);
	}
	return(NSJSONNull);
}

@implementation JSON_INIT
+(void)load{
	NSJSONNull = [[NSData alloc] initWithBytes: "null" length: 4];
	NSJSONTrue = [[NSData alloc] initWithBytes: "true" length: 4];
	NSJSONFalse = [[NSData alloc] initWithBytes: "false" length: 5];
}
@end
@implementation NSData (JSON)
+(NSData *)dataFromDataToJson: (id)data{
	return(data_serialize(data));
}
-(void)jsonPrint{
	NSMutableData *t;
	t = [NSMutableData dataWithData: self];
	[t appendBytes: "\0" length: 1];
	printf("%s\n", (char *)[t bytes]);
}
@end
@implementation NSString (JSON)
-(id)jsonToData{
	char *s;
	s = (char *)[self UTF8String];
	return(json_unserialize(s, strlen(s)));
}
@end
#endif

static void JSON_set_type(int state, uint8_t *start, uint8_t *end, int *len, int *type){
	*len = ((end - start) + 1);
	if(state == N3){
		*type = JSON_TYPE_NULL;
	}else if(state == F4){
		*type = JSON_TYPE_FALSE;
	}else if(state == T3){
		*type = JSON_TYPE_TRUE;
	}else if(state == 7){
		*type = JSON_TYPE_STRING;
	}else if((state == 14) || (state == 15)){
		*type = JSON_TYPE_INT;
	}else if((state == 16) || (state == 19)){
		*type = JSON_TYPE_DOUBLE;
	}else if((state == -8) || (state == -9)){
		*type = JSON_TYPE_MAP;
	}else if(state == -7){
		*type = JSON_TYPE_ARRAY;
	}else{
		*type = JSON_TYPE_ERROR;
	}
}

void print_value(struct JSON_struct *jc){
/*	printf("VALUE %d, TYPE: %d :", jc->count, jc->value_type);
	for(int i = 0; i < jc->value_len; i++){
		printf("%c", jc->value_start[i]);
	}
	printf(":");
	if((jc->value_type == JSON_TYPE_MAP) || (jc->value_type == JSON_TYPE_ARRAY)){
		printf(" (len: %d)", jc->value_count);
	}
	printf("\n");*/
}

static int JSON_checker_char(struct JSON_struct *jc, uint8_t *next_char){
/*
    After calling new_JSON_checker, call this function for each character (or
    partial character) in your JSON text. It can accept UTF-8, UTF-16, or
    UTF-32. It returns true if things are looking ok so far. If it rejects the
    text, it deletes the JSON_checker object and returns false.
*/
    int next_class, next_state;
/*
    Determine the character's class.
*/
    if (*next_char >= 128) {
        next_class = C_ETC;
    } else {
        next_class = ascii_class[*next_char];
        if (next_class <= ___)return(0);
    }
/*
    Get the next state from the state transition table.
*/
    next_state = state_transition_table[jc->state][next_class];
    if((jc->state == GO) && (next_state != GO)){
    	jc->start = next_char;
    }
    if(jc->top == 1){
//    	printf("PREV STATE: %d, STATE: %d\n", jc->state, next_state);
    	if((jc->state == KE) && (next_state != KE)){
		jc->key_start = next_char;
//		printf("START OF KEY\n");
	}else if((jc->state == OB) && (next_state != OB)){
		jc->key_start = next_char;
//		printf("START OF KEY\n");
	}else if((jc->state == VA) && (next_state != VA)){
		jc->value_type = JSON_TYPE_ERROR;
		jc->value_start = next_char;
		jc->value_count = 0;
//		printf("START OF VALUE\n");
	}else if((jc->state == AR) && (next_state != AR)){
		jc->value_type = JSON_TYPE_ERROR;
		jc->value_start = next_char;
		jc->value_count = 0;
//		printf("START OF VALUE\n");
	}
    }
    if((jc->state != OK) && (next_state == OK)){
	    JSON_set_type(jc->state, jc->start, next_char, &jc->len, &jc->type);
	    if(((jc->type == JSON_TYPE_INT) || (jc->type == JSON_TYPE_DOUBLE)) && jc->value_len)jc->len--;
            if((jc->top == 1) && ((jc->stack[jc->top] == MODE_ARRAY) || (jc->stack[jc->top] == MODE_OBJECT))){
		JSON_set_type(jc->state, jc->value_start, next_char, &jc->value_len, &jc->value_type);
		if(((jc->value_type == JSON_TYPE_INT) || (jc->value_type == JSON_TYPE_DOUBLE)) && jc->value_len)jc->value_len--;
//		printf("AAA: %d\n", jc->top);
//		print_value(jc);
//		printf("BBB\n");
	}
//    	printf("OK\n");
    }
    if (next_state >= 0) {
/*
    Change the state.
*/
        jc->state = next_state;
    } else {
/*
    Or perform one of the actions.
*/
        switch (next_state) {
/* empty } */
        case -9:
            if (!pop(jc, MODE_KEY)) {
                return (0);
            }
	    jc->state = -9;
		if(jc->top == 1){
			if(jc->value_type == JSON_TYPE_ERROR){
				JSON_set_type(jc->state, jc->value_start, next_char, &jc->value_len, &jc->value_type);
				print_value(jc);
			}
		}
	    JSON_set_type(jc->state, jc->start, next_char, &jc->len, &jc->type);
            jc->state = OK;
            break;

/* } */ case -8:
            if (!pop(jc, MODE_OBJECT)) {
                return (0);
            }
		if(jc->top == 0){
			jc->count++;
			if(jc->value_type == JSON_TYPE_ERROR){
				JSON_set_type(jc->state, jc->value_start, next_char - 1, &jc->value_len, &jc->value_type);
				print_value(jc);
			}
//	    		printf("OK 1\n");
		}
	    jc->state = -8;
		if(jc->top == 1){
			jc->value_count++;
			if(jc->value_type == JSON_TYPE_ERROR){
				JSON_set_type(jc->state, jc->value_start, next_char, &jc->value_len, &jc->value_type);
				print_value(jc);
			}
//	    		printf("OK 2: %d\n", jc->value_type);
		}
	    JSON_set_type(jc->state, jc->start, next_char, &jc->len, &jc->type);
            jc->state = OK;
//	    printf("OK\n");
            break;

/* ] */ case -7:
            if (!pop(jc, MODE_ARRAY)) {
                return (0);
            }
		if((jc->top == 0) && (jc->state != AR)){
			jc->count++;
			if(jc->value_type == JSON_TYPE_ERROR){
				JSON_set_type(jc->state, jc->value_start, next_char - 1, &jc->value_len, &jc->value_type);
				print_value(jc);
			}
		}else if((jc->top == 1) && (jc->state != AR)){
			jc->value_count++;
		}
	    jc->state = -7;
		if(jc->top == 1){
			if(jc->value_type == JSON_TYPE_ERROR){
				JSON_set_type(jc->state, jc->value_start, next_char, &jc->value_len, &jc->value_type);
				print_value(jc);
			}
		}
	    JSON_set_type(jc->state, jc->start, next_char, &jc->len, &jc->type);
            jc->state = OK;
            break;

/* { */ case -6:
            if (!push(jc, MODE_KEY)) {
                return (0);
            }
            jc->state = OB;
            break;

/* [ */ case -5:
            if (!push(jc, MODE_ARRAY)) {
                return (0);
            }
            jc->state = AR;
            break;

/* " */ case -4:
            switch (jc->stack[jc->top]) {
            case MODE_KEY:
	    	if(jc->top == 1){
			JSON_set_type(jc->state, jc->key_start, next_char, &jc->key_len, &jc->value_type);
		}
                jc->state = CO;
                break;
            case MODE_ARRAY:
            case MODE_OBJECT:
	    	if(jc->top == 1){
			if(jc->value_type == JSON_TYPE_ERROR){
				JSON_set_type(jc->state, jc->value_start, next_char, &jc->value_len, &jc->value_type);
				print_value(jc);
			}
		}
                jc->state = OK;
                break;
            case MODE_DONE:
	    	JSON_set_type(jc->state, jc->start, next_char, &jc->len, &jc->type);
                jc->state = OK;
                break;
            default:
                return (0);
            }
            break;

/* , */ case -3:
            switch (jc->stack[jc->top]) {
            case MODE_OBJECT:
/*
    A comma causes a flip from object mode to key mode.
*/
                if (!pop(jc, MODE_OBJECT) || !push(jc, MODE_KEY)) {
                    return (0);
                }
		if(jc->top == 1){
			jc->count++;
			if(jc->value_type == JSON_TYPE_ERROR){
				JSON_set_type(jc->state, jc->value_start, next_char - 1, &jc->value_len, &jc->value_type);
				print_value(jc);
			}
		}else if(jc->top == 2){
			jc->value_count++;
		}
                jc->state = KE;
                break;
            case MODE_ARRAY:
		if(jc->top == 1){
			jc->count++;
			if(jc->value_type == JSON_TYPE_ERROR){
				JSON_set_type(jc->state, jc->value_start, next_char - 1, &jc->value_len, &jc->value_type);
				print_value(jc);
			}
		}else if(jc->top == 2){
			jc->value_count++;
		}
                jc->state = VA;
                break;
            default:
                return (0);
            }
            break;

/* : */ case -2:
/*
    A colon causes a flip from key mode to object mode.
*/
            if (!pop(jc, MODE_KEY) || !push(jc, MODE_OBJECT)) {
                return (0);
            }
            jc->state = VA;
            break;
/*
    Bad action.
*/
        default:
            return (0);
        }
    }
    return(1);
}

int json_decode_string(const char *ptr, int len, json_utf8_t *utf8_char){
	if(!len)return(0);
	if(ptr[0] == '\\'){
		if(len > 1){
			if(ptr[1] == '\"'){
				*utf8_char = '\"';
				return(2);
			}else if(ptr[1] == '\\'){
				*utf8_char = '\\';
				return(2);
			}else if(ptr[1] == '/'){
				*utf8_char = '/';
				return(2);
			}else if(ptr[1] == 'b'){
				*utf8_char = '\b';
				return(2);
			}else if(ptr[1] == 'f'){
				*utf8_char = '\f';
				return(2);
			}else if(ptr[1] == 'n'){
				*utf8_char = '\n';
				return(2);
			}else if(ptr[1] == 'r'){
				*utf8_char = '\r';
				return(2);
			}else if(ptr[1] == 't'){
				*utf8_char = '\t';
				return(2);
			}else if(ptr[1] == 'u'){
				if(len > 5){
					*utf8_char = (hex_to_utf8(ptr[2]) << 12) | (hex_to_utf8(ptr[3]) << 8) | (hex_to_utf8(ptr[4]) << 4) | hex_to_utf8(ptr[5]);
					return(6);
				}
			}
		}else{
			*utf8_char = '\\';
			return(1);	
		}
	}else if(((ptr[0] & 0xE0) == 0xC0) && (len > 1)){
        	*utf8_char = ((json_utf8_t)(ptr[0] & 0x1F) << 6) | (ptr[1] & 0x3F);
		return(2);
	}else if(((ptr[0] & 0xF0) == 0xE0) && (len > 2)){
		*utf8_char = ((json_utf8_t)(ptr[0] & 0x0F) << 12) | ((json_utf8_t)(ptr[1] & 0x3F) << 6) | (ptr[2] & 0x3F);
		return(3);
	}else if(((ptr[0] & 0xF8) == 0xF0) && (len > 3)){
		*utf8_char = ((json_utf8_t)(ptr[0] & 0x0F) << 18) | ((json_utf8_t)(ptr[1] & 0x3F) << 12) | ((json_utf8_t)(ptr[2] & 0x3F) << 6) | (ptr[3] & 0x3F);
		return(4);
	}
	*utf8_char = ptr[0];
	return(1);
}

static json_utf8_t hex_to_utf8(uint8_t c){
	if(c == '1')return(1);
	if(c == '2')return(2);
	if(c == '3')return(3);
	if(c == '4')return(4);
	if(c == '5')return(5);
	if(c == '6')return(6);
	if(c == '7')return(7);
	if(c == '8')return(8);
	if(c == '9')return(9);
	if(c == 'A' || c == 'a')return(10);
	if(c == 'B' || c == 'b')return(11);
	if(c == 'C' || c == 'c')return(12);
	if(c == 'D' || c == 'd')return(13);
	if(c == 'E' || c == 'e')return(14);
	if(c == 'F' || c == 'f')return(15);
	return(0);
}

size_t strtojson(char *dst, const char *src, size_t siz){
	size_t i, d;
	if(siz < 1)return(0);
	d = 0;
	siz--;
	for(i = 0; src[i]; i++){
		if(src[i] == '\"'){
			if((d + 2) >= siz)goto out;
			dst[d++] = '\\';
			dst[d++] = '\"';
		}else if(src[i] == '\\'){
			if((d + 2) >= siz)goto out;
			dst[d++] = '\\';
			dst[d++] = '\\';
		}else if(src[i] == '\n'){
			if((d + 2) >= siz)goto out;
			dst[d++] = '\\';
			dst[d++] = 'n';
		}else if(src[i] == '\r'){
			if((d + 2) >= siz)goto out;
			dst[d++] = '\\';
			dst[d++] = 'r';
		}else if(src[i] == '\t'){
			if((d + 2) >= siz)goto out;
			dst[d++] = '\\';
			dst[d++] = 't';
		}else if(src[i] < 128){
			if((d + 1) >= siz)goto out;
			dst[d++] = src[i];
		}else if(src[i] < 2048){
			if((d + 2) >= siz)goto out;
			dst[d++] = (src[i] >> 6) | 0xC0;
			dst[d++] = (src[i] & 0x3F) | 0x80;
		}else if(src[i] < 65536LU){
			if((d + 3) >= siz)goto out;
			dst[d++] = (src[i] >> 12) | 0xE0;
			dst[d++] = ((src[i] >> 6) & 0x3F) | 0x80;
			dst[d++] = (src[i] & 0x3F) | 0x80;
		}else if(src[i] < 1114111LU){
			if((d + 4) >= siz)goto out;
			dst[d++] = (src[i] >> 18) | 0xF0;
			dst[d++] = ((src[i] >> 12) & 0x3F) | 0x80;
			dst[d++] = ((src[i] >> 6) & 0x3F) | 0x80;
			dst[d++] = (src[i] & 0x3F) | 0x80;
		}
	}
out:
	dst[d] = 0;
	return(d);
}



