all: lexer

lexer: ./src/tokenizer.lpp
	flex ./src/tokenizer.lpp
	g++ lex.yy.c -o lexer 
