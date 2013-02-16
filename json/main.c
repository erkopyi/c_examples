#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "json.h"

json_object_t t,r;
char buf[500];
char buf1[500];

int main (void){
	int i,l;
	printf("Program start \r\n");
	l = snprintf(buf, 500,"{\"tere\":{\"kaks\":56,\"kolm\":\"tere\",\"neli\":56}}");

	if(!json_check(buf, l, &t))return(1);
	if(!json_get_value(buf, l, "tere", &t))return(1);
	if(!json_get_value(t.start, t.len, "kolm", &r))return(1);
	printf("debug type:%d, len:%d, count:%d\r\n", r.type, r.len, r.count);
	snprintf(buf1,r.len + 1,r.start);
	printf("debug string %s\r\n", buf1);
	if(json_isequal(&r, "tere")){
		printf("TODO\r\n");
	}

	return (0);
}
