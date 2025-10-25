all: parser

parser: ansic.tab.c lex.yy.c src/ast_base.cpp src/declaration.cpp src/diagnostics.cpp src/expression.cpp src/statement.cpp src/symbol_table.cpp src/tac.cpp
	g++ -std=c++17 -Wall -g -Isrc ansic.tab.c lex.yy.c src/ast_base.cpp src/declaration.cpp src/diagnostics.cpp src/expression.cpp src/statement.cpp src/symbol_table.cpp src/tac.cpp -lfl -o parser

ansic.tab.c ansic.tab.h: src/ansic.y
	bison -d -o ansic.tab.c src/ansic.y

lex.yy.c: src/tokenizer.lpp
	flex -o lex.yy.c src/tokenizer.lpp

clean:
	rm -f ansic.tab.c ansic.tab.h lex.yy.c parser

.PHONY: all clean