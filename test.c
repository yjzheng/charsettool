
#include <string.h>
#include <stdio.h>

#define MAX_CHARSET_NUM 128
#define MAX_CHARSET_BUFFER 2048

typedef struct charset {	
	unsigned char start;
	unsigned char flag; //bit 7: not,bit 0-6: length, length of chars( max  char)
	unsigned char *chars;	
} charset_t;

#define CHARSET_NOT( __cs) do{(__cs).flag |= (1<<7);} while (0)
#define CHARSET_IS_NOT( __cs) (((__cs).flag) & (1<<7))
#define CHARSET_SET_LEN( __cs, __len) do{(__cs).flag |= ((__len) & 0x7f);} while (0)
#define CHARSET_GET_LEN( __cs) (((__cs).flag) & 0x7f)

static unsigned char charset_buffer[MAX_CHARSET_BUFFER];
static unsigned int charset_buffer_used=0;
static charset_t charset_pool[MAX_CHARSET_NUM];
static unsigned int charset_used_count = 0;


enum {
	CHARSET_SPACE=0,
	CHARSET_DIGIT,		
	CHARSET_WORD,

	//add predefined charset here

	CHARSET_DYNAMIC,
	CHARSET_MAX=128
}charset_index_e;




typedef struct charset_pool_s{
	char charset_buf[MAX_CHARSET_BUFFER];
	unsigned int charset_buf_used;

	charset_t charsets[CHARSET_MAX];
	unsigned int charset_used;


	//jeanfixme: check if lock is needed? 
}charset_pool_t;


static charset_pool_t g_charset_pool;

typedef struct tmp_charset{
	char min_byte;
	char max_byte;
	unsigned char charset[32];	
	//jeanfixme: should check only one man is use this
}tmp_charset_t;

static tmp_charset_t g_tmp_charset;

static unsigned char stateful_charset[32];//jeanfixme: check if a faster way to build the charset
static char stateful_charset_min_byte=-1;
static char stateful_charset_max_byte=-1;

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


void charset_dump(charset_t * cur_p){
	int len=CHARSET_GET_LEN(*cur_p);
	int i,j;
	int byte,bit;

	printf("************ charset_dump ************\n");				
	printf("flag(len)==%d\n",cur_p->flag);				
	printf("start==%d\n",cur_p->start);				

		
	for (i=0;i<len;i++){
		byte= cur_p->start+i;
		printf("[%d]char(%2x)=",byte,cur_p->chars[i]);				
		for (j=0;j<8;j++){
			if (cur_p->chars[i]& 1<<(j)) {
				bit=j;
				printf("%c",byte*8+bit);
			}			
		}
		printf("\n");
	}
	printf("**************************************\n\n");				
}


int __charset_include_bit(charset_t * cs_p,unsigned char charactor){
	unsigned int target_byte= charactor/8 ;

	if (target_byte < cs_p->start)
		return 0;

	if (target_byte > cs_p->start+ CHARSET_GET_LEN(*cs_p) -1 )
		return 0;

	//in range
	return (cs_p->chars[target_byte-cs_p->start])& 1<<(charactor%8);	
}


int charset_hit(charset_t * cs_p,unsigned char charactor){
	int include=__charset_include_bit(cs_p,charactor);
	int hit;
	if (CHARSET_IS_NOT(*cs_p)){
		hit = !include;
	}
	else {
		hit = include; 
	}
	return hit;
}

static void __stateful_charset_init(){
	stateful_charset_min_byte = stateful_charset_max_byte = -1 ;//init
}

static void __stateful_charset_set(unsigned char charactor)
{

	unsigned int target_byte= charactor/8 ;
	unsigned int target_bit= charactor%8 ;
	if (stateful_charset_min_byte < 0){ //first bit
		stateful_charset_min_byte = stateful_charset_max_byte = target_byte;
		stateful_charset[target_byte] = 1<< target_bit; //clear and set
	}
	else { //not the first bit
		if (target_byte < stateful_charset_min_byte){ //new area, need to clean the old char bit between target_byte and stateful_charset_min_byte
			memset( &stateful_charset[target_byte] , 0 , stateful_charset_min_byte - target_byte);
			stateful_charset_min_byte = target_byte;
		}
		else if (target_byte > stateful_charset_max_byte){//new area, need to clean the old char bit bw/ stateful_charset_max_byte and target_byte			
			memset( &stateful_charset[stateful_charset_max_byte+1] ,0,target_byte - stateful_charset_max_byte );
			stateful_charset_max_byte = target_byte;
		}
		else { 
			//between stateful_charset_min_byte and stateful_charset_max_byte, simply or the bit, do nothing
		}
		stateful_charset[target_byte] |= 1<< target_bit;
	}
}


static inline void __stateful_charset_set_range(unsigned char start,unsigned char end){
	unsigned char set_char;
	for (set_char=start;set_char<=end;set_char++){
		__stateful_charset_set(set_char);
	}
}

//Return <0 while error, buf used len if success;
static int __stateful_charset_output(){
	int len=stateful_charset_max_byte - stateful_charset_min_byte + 1;
	charset_t * new_cs= &(g_charset_pool.charsets[g_charset_pool.charset_used]);	
	unsigned char * new_buf= &(g_charset_pool.charset_buf[g_charset_pool.charset_buf_used]);	

	//check global buffer length, if not enough error return
	if (len> MAX_CHARSET_BUFFER - g_charset_pool.charset_buf_used){

		return -1;
	}

	CHARSET_SET_LEN(*new_cs , len);
	new_cs->start = stateful_charset_min_byte;
	memcpy(new_buf,&(stateful_charset[stateful_charset_min_byte]),len);
	g_charset_pool.charset_buf_used += len;
	new_cs->chars = new_buf;
	g_charset_pool.charset_used += 1;
	charset_dump(new_cs);
	
	return len;
}



typedef struct predefined_charset{
	unsigned char re_syntax[64];
	charset_t charset;		
}predefined_charset_t;


void re_predefined_charset_init(){
	charset_t * cur_p;
	unsigned char tmp_char;
	
	__stateful_charset_init();
	__stateful_charset_set(' ');
	__stateful_charset_set('\r');
	__stateful_charset_set('\n');
	__stateful_charset_set('\t');
	__stateful_charset_set('\f');
	__stateful_charset_output();

	__stateful_charset_init();
	__stateful_charset_set_range('0','9');
	__stateful_charset_output();	
	
	__stateful_charset_init();
	__stateful_charset_set_range('0','9');
	__stateful_charset_set_range('a','z');
	__stateful_charset_set_range('A','Z');
	__stateful_charset_set('_');
	__stateful_charset_output();
	
	charset_used_count = CHARSET_DYNAMIC;	
}

void re_charset_pool_init(){
	g_charset_pool.charset_used=0;
	g_charset_pool.charset_buf_used=0;
	re_predefined_charset_init();
}

static char tempChar[32];
#define UNIT_TEST 1
#if UNIT_TEST
static inline void dump_hex(unsigned char *buf,unsigned int len){
	int i=0;
	for (i=0;i<len;i++)
	{
		printf("%.2x",buf[i]);
	}
	printf("\n");
}

#define MAX_PATTERN_LEN			(64)
typedef struct
{
	unsigned char case_ptn[MAX_PATTERN_LEN];	/* case-sensitive pattern */
	unsigned int case_sensitive : 1;
	unsigned int ignore_order : 1;
	unsigned int need_prev_match : 1;
	unsigned int fixed_position : 1;
	unsigned int group_id : 4; //for alternation, 
	unsigned int attached_ptn_seq : 8; //for alternation, keep the ptn_seq of the first ptn_node of the same group, jeanfixme: check if the 
} text_t;

int main(){
	int ret;
	char set_char;
	text_t test_txt;
	int test_array_init[16]={-1};
	
	re_charset_pool_init();
	charset_hit(&g_charset_pool.charsets[CHARSET_SPACE] ,'\t');
	charset_hit(&g_charset_pool.charsets[CHARSET_SPACE] ,'0');

	charset_hit(&g_charset_pool.charsets[CHARSET_DIGIT] ,'\t');
	charset_hit(&g_charset_pool.charsets[CHARSET_DIGIT] ,'0');

	charset_hit(&g_charset_pool.charsets[CHARSET_WORD] ,'\t');
	charset_hit(&g_charset_pool.charsets[CHARSET_WORD] ,'0');
	charset_hit(&g_charset_pool.charsets[CHARSET_WORD] ,'t');
	charset_hit(&g_charset_pool.charsets[CHARSET_WORD] ,'_');
	charset_hit(&g_charset_pool.charsets[CHARSET_WORD] ,';');

	//\d
printf("jean_debug(%d):%s:\\d\n",__LINE__,__PRETTY_FUNCTION__,1);		
	memset(tempChar,0,sizeof(tempChar));
	for (set_char='0';set_char<='9';set_char++){
		set_bit(set_char,tempChar);
	}
	dump_hex(tempChar,32);

	//\s
printf("jean_debug(%d):%s:\\s\n",__LINE__,__PRETTY_FUNCTION__,1);		
	memset(tempChar,0,sizeof(tempChar));
	set_bit(' ',tempChar);
	set_bit('\r',tempChar);        
	set_bit('\n',tempChar);        
	set_bit('\t',tempChar);        
	set_bit('\f',tempChar);
	dump_hex(tempChar,32);


printf("jean_debug(%d):%s:'tp:/'\n",__LINE__,__PRETTY_FUNCTION__);		
	memset(tempChar,0,sizeof(tempChar));
	set_bit('t',tempChar);
	set_bit('p',tempChar);        
	set_bit(':',tempChar);        
	set_bit('/',tempChar);        
	dump_hex(tempChar,32);


printf("jean_debug(%d):%s:without !\n",__LINE__,__PRETTY_FUNCTION__);		
	memset(tempChar,0xff,sizeof(tempChar));
	clear_bit('!',tempChar);
	dump_hex(tempChar,32);


printf("jean_debug(%d):%s:0123f\n",__LINE__,__PRETTY_FUNCTION__);		
	memset(tempChar,0,sizeof(tempChar));
	set_bit('0',tempChar);
	set_bit('1',tempChar);        
	set_bit('2',tempChar);        
	set_bit('3',tempChar);        
	set_bit('f',tempChar);        
	dump_hex(tempChar,32);	

printf("jean_debug(%d):%s:'/'\n",__LINE__,__PRETTY_FUNCTION__);		
	memset(tempChar,0,sizeof(tempChar));
	set_bit('/',tempChar);
	dump_hex(tempChar,32);	

	
	ret=test_bit('0',tempChar);
printf("jean_debug(%d):%s:ret=%d\n",__LINE__,__PRETTY_FUNCTION__,ret);	
	ret=test_bit('d',tempChar);
printf("jean_debug(%d):%s:ret=%d\n",__LINE__,__PRETTY_FUNCTION__,ret);	




}
#endif 
