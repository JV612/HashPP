# ============================================================================
# Hash++ Compiler - Phase 1
# Lexer: tokenizer.lpp | Parser: ansic.y
# ============================================================================

CXX = g++
CXXFLAGS = -std=c++11 -Wall

# Compiler executable name
TARGET = compiler

# Source files
PARSER_SRC = src/ansic.y
LEXER_SRC = src/tokenizer.lpp
CPP_SOURCES = src/symbol_table.cpp src/tac.cpp src/ast_base.cpp src/expression.cpp src/statement.cpp src/declaration.cpp

# Generated files
PARSER_OUT = src/ansic.tab.c
PARSER_HDR = src/ansic.tab.h
LEXER_OUT = src/lex.yy.c
OBJECTS = src/symbol_table.o src/tac.o src/ast_base.o src/expression.o src/statement.o src/declaration.o

# ============================================================================
# Build Targets
# ============================================================================

all: $(TARGET)

$(TARGET): $(PARSER_OUT) $(LEXER_OUT) $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(LEXER_OUT) $(PARSER_OUT) $(OBJECTS)

# Generate parser from ansic.y
$(PARSER_OUT) $(PARSER_HDR): $(PARSER_SRC)
	bison -d -o $(PARSER_OUT) $(PARSER_SRC)

# Generate lexer from tokenizer.lpp (depends on parser header)
$(LEXER_OUT): $(LEXER_SRC) $(PARSER_HDR)
	flex -o $(LEXER_OUT) $(LEXER_SRC)

# Compile C++ source files
src/symbol_table.o: src/symbol_table.cpp src/symbol_table.h
	$(CXX) $(CXXFLAGS) -c src/symbol_table.cpp -o src/symbol_table.o

src/tac.o: src/tac.cpp src/tac.h
	$(CXX) $(CXXFLAGS) -c src/tac.cpp -o src/tac.o

src/ast_base.o: src/ast_base.cpp src/ast_base.h
	$(CXX) $(CXXFLAGS) -c src/ast_base.cpp -o src/ast_base.o

src/expression.o: src/expression.cpp src/expression.h src/ast_base.h src/symbol_table.h src/tac.h
	$(CXX) $(CXXFLAGS) -c src/expression.cpp -o src/expression.o

src/statement.o: src/statement.cpp src/statement.h src/expression.h src/declaration.h src/ast_base.h src/tac.h
	$(CXX) $(CXXFLAGS) -c src/statement.cpp -o src/statement.o

src/declaration.o: src/declaration.cpp src/declaration.h src/expression.h src/ast_base.h src/symbol_table.h src/tac.h
	$(CXX) $(CXXFLAGS) -c src/declaration.cpp -o src/declaration.o

# ============================================================================
# Utility Targets
# ============================================================================

clean:
	rm -f $(TARGET)
	rm -f $(PARSER_OUT) $(PARSER_HDR) $(LEXER_OUT)
	rm -f src/*.o

rebuild: clean all

# Run all tests
test: $(TARGET)
	@echo "\n========================================="
	@echo "         Running Test Suite"
	@echo "=========================================\n"
	@echo "=== Test 1: Simple declarations ==="
	./$(TARGET) test_files/test1.c
	@echo "\n=== Test 2: Declarations with initialization ==="
	./$(TARGET) test_files/test2.c
	@echo "\n=== Test 3: Arithmetic expressions ==="
	./$(TARGET) test_files/test3.c
	@echo "\n==========================================="
	@echo "         All Tests Completed"
	@echo "==========================================="

# Run grammar test (comprehensive)
test-grammar: $(TARGET)
	@echo "\n=== Testing comprehensive grammar ==="
	./$(TARGET) test_files/test_grammar.c

# Help target
help:
	@echo "Hash++ Compiler - Makefile Targets:"
	@echo "  make          - Build the compiler"
	@echo "  make clean    - Remove generated files"
	@echo "  make rebuild  - Clean and rebuild"
	@echo "  make test     - Run all tests"
	@echo "  make test-grammar - Test comprehensive grammar"
	@echo "  make help     - Show this help message"

.PHONY: all clean rebuild test test-grammar help
