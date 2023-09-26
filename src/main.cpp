#include "ATCLexer.h"
#include "ATCParser.h"
#include "antlr4-runtime/antlr4-runtime.h"
#include <iostream>
using namespace std;
using namespace antlr4;

int main(int argc, const char *argv[]) {
    std::ifstream stream;
    stream.open(argv[1]);

    ANTLRInputStream input(stream);
    ATCLexer lexer(&input);
    CommonTokenStream token(&lexer);
    ATCParser parser(&token);
    parser.expr();

    if (parser.getNumberOfSyntaxErrors() != 0) {
        cout << "error" << endl;
    }
    return 0;
}