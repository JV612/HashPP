all: lexer

lexer: ./src/comp.lpp
	flex ./src/comp.lpp
	g++ lex.yy.c -o lexer 