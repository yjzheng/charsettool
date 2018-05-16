
#include <string.h>
#include <stdio.h>

#if 8==__SIZEOF_LONG__
#define BITS_PER_LONG 64
#else
#define BITS_PER_LONG 32
#endif
#define BITOP_MASK(nr)          (1UL << ((nr) % BITS_PER_LONG))
#define BITOP_WORD(nr)          ((nr) / BITS_PER_LONG)

static inline int test_bit(int nr, const volatile unsigned long *addr)
{
	return 1UL & (addr[BITOP_WORD(nr)] >> (nr & (BITS_PER_LONG - 1)));
}

static inline void set_bit(int nr, volatile unsigned long *addr)
{
	unsigned long mask = BITOP_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BITOP_WORD(nr);

	*p |= mask;
}

static inline void clear_bit(int nr, volatile unsigned long *addr)
{
	unsigned long mask = BITOP_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BITOP_WORD(nr);


	*p &= ~mask;
}

static char tempChar[32];
static inline void dump_hex(unsigned char *buf,unsigned int len){
	int i=0;
	for (i=0;i<len;i++)
	{
		printf("%.2x",buf[i]);
	}
	printf("\n");
}

static void __print_usage(){
	printf("Usage:./gen_cs_array <s|r[n]> <string|range_cnt> [[r_start] [r_end]]:\n");
	printf("\tEx: ./gen_cs_array s string_example\n");
	printf("\tEx: ./gen_cs_array r 5 a z A Z 0 9 - - . . to generate [a-zA-Z0-9-.]\n");
	printf("\tEx: ./gen_cs_array rn 6 a z A Z 0 9 - - . . \"&\" \"&\"  to generate [^a-zA-Z0-9-.&]\n");
}

int main(int argc, char* argv[]){
	int ret;
	int char_cnt=0, char_idx;
	char temp_char = ' ';
	char temp_char_end = ' ';
	char *re_type= argv[1];
	char *re_string;
	int range_count=0,range_idx;


	if (argc < 3){
		__print_usage();
		return -1;
	}

	memset(tempChar,0,sizeof(tempChar));
	switch (re_type[0]){
		case 's':
			re_string= argv[2];
			char_cnt= strlen(re_string);
			if (char_cnt>64)
				return -2;			

			printf("The cs array of '%s' is : ",re_string);
			for (char_idx=0;char_idx< char_cnt; char_idx++){
				temp_char = re_string[char_idx];
				//printf("The cs array afger set '%c' is :",temp_char);
				//dump_hex(tempChar,32);
				set_bit((int)temp_char,(unsigned long*)tempChar);
			}			
			break;
		case 'r':
			range_count= atoi(argv[2]);
			printf("The cs array after append %d group ",range_count);
			//printf("jean_debug(%d):%s:range_count=%d\n",__LINE__,__PRETTY_FUNCTION__,range_count)	;
			for (range_idx=0;range_idx< range_count; range_idx++){
				temp_char = argv[3+2*range_idx][0];
				temp_char_end = argv[3+2*range_idx+1][0];
				if (temp_char != temp_char_end){
					printf("'%c-%c' ",temp_char,temp_char_end);
					for (;temp_char <= temp_char_end; temp_char++){
						//printf("The cs array afger set '%c (%d)' is :",temp_char,temp_char);
						//dump_hex(tempChar,32);				
						set_bit((int)temp_char,(unsigned long*)tempChar);
					}
				}
				else {
					printf("'%c' ",temp_char);
					set_bit((int)temp_char,(unsigned long*)tempChar);				
				}				
			}			
			printf("is : ");			
			break;
		default:
			printf ("not support re type %c\n",re_type[0]);
			break;
	}

	char_cnt= strlen(re_type);
	if (char_cnt>1 && re_type[1]== 'n'){
		printf ("\nbefore invert: ");
		dump_hex(tempChar,32);	
		for (range_idx=0;range_idx< 32; range_idx++){
			tempChar[range_idx]= ~tempChar[range_idx];
		}
	}	

	dump_hex(tempChar,32);	
}

