#include "tokenise.h"

#include "block.h"

void parse(tokeniser *tok, block *entry)
{
	for(;;){
		enum token ct = token_next(tok);
		if(ct == tok_eof)
			break;
		printf("token %s\n", token_to_str(ct));
	}
}
