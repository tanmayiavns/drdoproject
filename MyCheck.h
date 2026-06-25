#include "MyCheck.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/AST/ASTContext.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/Lexer.h"
#include "clang/AST/RecursiveASTVisitor.h"

using namespace clang;
using namespace clang::ast_matchers;

// ============================================================
// PP Callbacks
// ============================================================
class MISRAPreprocessorCallback : public PPCallbacks {
public:
    Preprocessor& PP;
    DiagnosticsEngine& DE;

    MISRAPreprocessorCallback(Preprocessor& PP, DiagnosticsEngine& DE)
        : PP(PP), DE(DE) {
    }

    void warn(SourceLocation loc, const char* msg) {
        if (loc.isInvalid()) return;
        if (PP.getSourceManager().isInSystemHeader(loc)) return;
        unsigned ID = DE.getCustomDiagID(DiagnosticsEngine::Warning, "%0");
        DE.Report(loc, ID).AddString(msg);
    }

    // Rule 70: #include only preceded by directives/comments
    void InclusionDirective(SourceLocation HashLoc,
        const Token& IncludeTok,
        StringRef FileName,
        bool IsAngled,
        CharSourceRange FilenameRange,
        OptionalFileEntryRef File,
        StringRef SearchPath,
        StringRef RelativePath,
        const Module* Imported,
        SrcMgr::CharacteristicKind FileType) override {
        SourceManager& SM = PP.getSourceManager();
        if (SM.isInSystemHeader(HashLoc)) return;
        unsigned col = SM.getSpellingColumnNumber(SM.getSpellingLoc(HashLoc));
        if (col > 1)
            warn(HashLoc,
                "Rule 70: #include statements shall only be preceded by "
                "other pre-processor directives or comments");
    }

    void MacroDefined(const Token& Tok, const MacroDirective* MD) override {
        const MacroInfo* MI = MD->getMacroInfo();
        SourceLocation   loc = Tok.getLocation();
        if (!MI || PP.getSourceManager().isInSystemHeader(loc)) return;

        StringRef name = Tok.getIdentifierInfo()->getName();

        // Rule 98: Standard library names shall not be reused
        static const char* stdNames[] = {
            "malloc","free","calloc","realloc",
            "printf","scanf","fprintf","fscanf",
            "exit","abort","system","getenv",
            "memcpy","memset","strlen","strcpy",
            "strcat","strcmp","sprintf","sscanf",
            nullptr
        };
        for (int i = 0; stdNames[i]; i++) {
            if (name == stdNames[i]) {
                warn(loc,
                    "Rule 98: Standard library function names shall not be reused");
                break;
            }
        }

        // Rule 74: Function-like macros
        if (MI->isFunctionLike())
            warn(loc, "Rule 74: Prefer a function over a function-like macro");

        // Rule 78: Expression macro not parenthesized
        if (!MI->isFunctionLike() && MI->getNumTokens() > 1) {
            bool hasOp = false;
            for (auto it = MI->tokens_begin(); it != MI->tokens_end(); ++it)
                if (it->isOneOf(tok::plus, tok::minus, tok::star,
                    tok::slash, tok::percent, tok::pipe,
                    tok::amp, tok::caret))
                {
                    hasOp = true; break;
                }
            if (hasOp) {
                bool lp = MI->tokens_begin()->is(tok::l_paren);
                bool rp = (MI->tokens_end() - 1)->is(tok::r_paren);
                if (!lp || !rp)
                    warn(loc,
                        "Rule 78: Macro expression must be enclosed in parentheses");
            }
        }

        // Rule 80: More than one # or ## in macro
        int cnt = 0;
        for (auto it = MI->tokens_begin(); it != MI->tokens_end(); ++it)
            if (it->isOneOf(tok::hash, tok::hashhash)) cnt++;
        if (cnt > 1)
            warn(loc,
                "Rule 80: At most one # or ## operator allowed in a macro");

        // Rule 76: Macro args must not look like directives
        if (MI->isFunctionLike()) {
            for (auto it = MI->tokens_begin(); it != MI->tokens_end(); ++it) {
                if (it->is(tok::hash)) {
                    auto next = it + 1;
                    if (next != MI->tokens_end() &&
                        next->isOneOf(tok::kw_if, tok::identifier)) {
                        warn(loc,
                            "Rule 76: Macro arguments shall not contain tokens "
                            "that look like pre-processing directives");
                        break;
                    }
                }
            }
        }

        // Rule 72: #define inside a block
        SourceManager& SM = PP.getSourceManager();
        unsigned col = SM.getSpellingColumnNumber(SM.getSpellingLoc(loc));
        if (col > 1 && !SM.isInSystemHeader(loc))
            warn(loc, "Rule 72: Macros shall not be #defined within a block");
    }

    void MacroUndefined(const Token& Tok,
        const MacroDefinition&,
        const MacroDirective*) override {
        SourceLocation loc = Tok.getLocation();
        if (PP.getSourceManager().isInSystemHeader(loc)) return;
        // Rule 73
        warn(loc, "Rule 73: #undef should not be used");
        // Rule 72
        SourceManager& SM = PP.getSourceManager();
        unsigned col = SM.getSpellingColumnNumber(SM.getSpellingLoc(loc));
        if (col > 1)
            warn(loc, "Rule 72: Macros shall not be #undefined within a block");
    }

    // Rule 100: errno macro expansion
    void MacroExpands(const Token& Tok,
        const MacroDefinition& MD,
        SourceRange Range,
        const MacroArgs* Args) override {
        SourceLocation loc = Tok.getLocation();
        if (loc.isInvalid()) return;
        if (PP.getSourceManager().isInSystemHeader(loc)) return;
        IdentifierInfo* II = Tok.getIdentifierInfo();
        if (II && II->getName() == "errno")
            warn(loc, "Rule 100: errno shall not be used");
    }

    // Rule 17: /* inside a C-style comment (nested comment)
    void HandleComment(Preprocessor& PP, SourceRange Comment) {
        SourceManager& SM = PP.getSourceManager();
        SourceLocation loc = Comment.getBegin();
        if (SM.isInSystemHeader(loc)) return;

        bool invalid = false;
        StringRef text = Lexer::getSourceText(
            CharSourceRange::getCharRange(Comment),
            SM, PP.getLangOpts(), &invalid);
        if (!invalid && text.starts_with("/*")) {
            // Check if /* appears inside the comment body
            size_t inner = text.find("/*", 2);
            if (inner != StringRef::npos)
                warn(loc,
                    "Rule 17: The character sequence /* shall not be used "
                    "within a C-style comment");
        }
    }
};

// ============================================================
// Helpers
// ============================================================
static unsigned countReturns(const Stmt* S) {
    if (!S) return 0;
    unsigned cnt = isa<ReturnStmt>(S) ? 1 : 0;
    for (const Stmt* child : S->children())
        cnt += countReturns(child);
    return cnt;
}

static unsigned countDirectBreaks(const Stmt* S, bool top = true) {
    if (!S) return 0;
    if (!top && (isa<ForStmt>(S) || isa<WhileStmt>(S) ||
        isa<DoStmt>(S) || isa<SwitchStmt>(S)))
        return 0;
    unsigned cnt = isa<BreakStmt>(S) ? 1 : 0;
    for (const Stmt* child : S->children())
        cnt += countDirectBreaks(child, false);
    return cnt;
}

// ============================================================
// registerMatchers
// ============================================================
void MyCheck::registerMatchers(MatchFinder& Finder) {

    // ── TYPE A RULES ─────────────────────────────────────────────────

    // Rule 8: basic types
    Finder.addMatcher(
        varDecl(
            unless(hasAncestor(recordDecl())),
            anyOf(
                hasType(asString("int")),
                hasType(asString("char")),
                hasType(asString("short")),
                hasType(asString("long")),
                hasType(asString("double")),
                hasType(asString("unsigned int")),
                hasType(asString("unsigned char")),
                hasType(asString("unsigned short")),
                hasType(asString("unsigned long"))
            )
        ).bind("r8"), this);

    // Rule 27: assignment in if condition
    Finder.addMatcher(
        ifStmt(hasCondition(
            expr(hasDescendant(binaryOperator(hasOperatorName("="))))
        )).bind("r27"), this);

    // Rule 34: Literal suffixes shall be uppercase (e.g. 1L not 1l)
    Finder.addMatcher(
        integerLiteral().bind("r34int"), this);
    Finder.addMatcher(
        floatLiteral().bind("r34float"), this);

    // Rule 38: float == or !=
    Finder.addMatcher(
        binaryOperator(
            anyOf(hasOperatorName("=="), hasOperatorName("!=")),
            hasLHS(expr(hasType(realFloatingPointType())))
        ).bind("r38"), this);

    // Rule 41: null statement
    Finder.addMatcher(nullStmt().bind("r41"), this);

    // Rule 43: goto
    Finder.addMatcher(gotoStmt().bind("r43"), this);

    // Rule 44: break in loop
    Finder.addMatcher(
        breakStmt(anyOf(
            hasAncestor(forStmt()),
            hasAncestor(whileStmt()),
            hasAncestor(doStmt())
        )).bind("r44b"), this);

    // Rule 44: continue in loop
    Finder.addMatcher(
        continueStmt(anyOf(
            hasAncestor(forStmt()),
            hasAncestor(whileStmt()),
            hasAncestor(doStmt())
        )).bind("r44c"), this);

    // Rule 46: if with no else
    Finder.addMatcher(
        ifStmt(unless(hasElse(anything()))).bind("r46"), this);

    // Rule 48/113: switch no default
    Finder.addMatcher(
        switchStmt(
            unless(hasDescendant(defaultStmt()))
        ).bind("r48"), this);

    // Rule 49: switch on boolean
    Finder.addMatcher(
        switchStmt(
            hasCondition(
                implicitCastExpr(
                    hasSourceExpression(
                        anyOf(
                            binaryOperator(anyOf(
                                hasOperatorName("=="),
                                hasOperatorName("!="),
                                hasOperatorName("<"),
                                hasOperatorName(">"),
                                hasOperatorName("<="),
                                hasOperatorName(">="),
                                hasOperatorName("&&"),
                                hasOperatorName("||")
                            )),
                            unaryOperator(hasOperatorName("!"))
                        )
                    )
                )
            )
        ).bind("r49"), this);

    // Rule 50: switch no case
    Finder.addMatcher(
        switchStmt(
            unless(hasDescendant(caseStmt()))
        ).bind("r50"), this);

    // Rule 51: float loop counter
    Finder.addMatcher(
        varDecl(
            hasType(realFloatingPointType()),
            hasAncestor(forStmt())
        ).bind("r51"), this);

    // Rule 59: explicit return type
    Finder.addMatcher(
        functionDecl(
            isDefinition(),
            unless(isMain()),
            unless(hasParent(recordDecl())),
            returns(asString("int"))
        ).bind("r59"), this);

    // Rule 60: parameter count mismatch
    Finder.addMatcher(
        callExpr(
            argumentCountIs(0),
            callee(functionDecl(unless(parameterCountIs(0))))
        ).bind("r60"), this);

    // Rule 61: void function return value used
    Finder.addMatcher(
        callExpr(
            callee(functionDecl(returns(asString("void")))),
            hasParent(expr())
        ).bind("r61"), this);

    // Rule 83: pointer arithmetic
    Finder.addMatcher(
        binaryOperator(
            anyOf(hasOperatorName("+"), hasOperatorName("-")),
            hasLHS(expr(hasType(pointerType())))
        ).bind("r83"), this);

    // Rule 86: function pointer variable
    Finder.addMatcher(
        varDecl(hasType(pointsTo(functionType()))).bind("r86"), this);

    // Rule 93: bit field not int/unsigned int
    Finder.addMatcher(
        fieldDecl(
            isBitField(),
            unless(anyOf(
                hasType(asString("unsigned int")),
                hasType(asString("int"))
            ))
        ).bind("r93"), this);

    // Rule 94: signed int bit field < 2 bits
    Finder.addMatcher(
        fieldDecl(isBitField(), hasType(asString("int"))).bind("r94"), this);

    // Rule 98: standard library names reused
    Finder.addMatcher(
        functionDecl(
            isDefinition(),
            unless(isExpansionInSystemHeader()),
            anyOf(
                hasName("malloc"), hasName("free"),
                hasName("calloc"), hasName("realloc"),
                hasName("printf"), hasName("scanf"),
                hasName("exit"), hasName("abort"),
                hasName("memcpy"), hasName("memset"),
                hasName("strlen"), hasName("strcpy")
            )
        ).bind("r98"), this);

    // Rule 99: malloc/free
    Finder.addMatcher(
        callExpr(callee(functionDecl(anyOf(
            hasName("malloc"), hasName("calloc"),
            hasName("realloc"), hasName("free")
        )))).bind("r99"), this);

    // Rule 100: errno AST fallback
    Finder.addMatcher(
        declRefExpr(to(varDecl(hasName("errno")))).bind("r100"), this);

    // Rule 105: signal/raise
    Finder.addMatcher(
        callExpr(callee(functionDecl(anyOf(
            hasName("signal"), hasName("raise")
        )))).bind("r105"), this);

    // Rule 106: stdio
    Finder.addMatcher(
        callExpr(callee(functionDecl(anyOf(
            hasName("printf"), hasName("scanf"),
            hasName("fprintf"), hasName("fscanf"),
            hasName("fopen"), hasName("fclose"),
            hasName("fread"), hasName("fwrite"),
            hasName("fgets"), hasName("fputs"),
            hasName("puts"), hasName("gets"),
            hasName("sprintf"), hasName("sscanf")
        )))).bind("r106"), this);

    // Rule 107: atof/atoi/atol
    Finder.addMatcher(
        callExpr(callee(functionDecl(anyOf(
            hasName("atof"), hasName("atoi"), hasName("atol")
        )))).bind("r107"), this);

    // Rule 108: abort/exit/getenv/system
    Finder.addMatcher(
        callExpr(callee(functionDecl(anyOf(
            hasName("abort"), hasName("exit"),
            hasName("getenv"), hasName("system")
        )))).bind("r108"), this);

    // Rule 119: incomplete array
    Finder.addMatcher(
        varDecl(hasType(incompleteArrayType())).bind("r119"), this);

    // Rule 120: array not fully initialized
    Finder.addMatcher(varDecl(hasType(arrayType())).bind("r120"), this);

    // Rule 123: int to pointer cast
    Finder.addMatcher(
        cStyleCastExpr(
            hasSourceExpression(expr(hasType(isInteger()))),
            hasType(pointerType())
        ).bind("r123"), this);

    // Rule 124 & 161: pointer cast
    Finder.addMatcher(
        cStyleCastExpr(
            hasSourceExpression(expr(hasType(pointerType())))
        ).bind("r124"), this);

    // Rule 125: conditional operator type mismatch
    Finder.addMatcher(conditionalOperator().bind("r125"), this);

    // Rule 127: implicit int to float
    Finder.addMatcher(implicitCastExpr().bind("r127"), this);

    // Rule 128: narrower float
    Finder.addMatcher(
        implicitCastExpr(hasCastKind(CK_FloatingCast)).bind("r128"), this);

    // Rule 129: narrower int
    Finder.addMatcher(
        implicitCastExpr(hasCastKind(CK_IntegralCast)).bind("r129"), this);

    // Rule 130: digraph in string
    Finder.addMatcher(stringLiteral().bind("r130"), this);

    // Rule 136: magic numbers
    Finder.addMatcher(
        integerLiteral(
            unless(anyOf(equals(0), equals(1)))
        ).bind("r136"), this);

    // Rule 137: literal array subscript
    Finder.addMatcher(
        arraySubscriptExpr(
            hasIndex(integerLiteral(unless(equals(0))))
        ).bind("r137"), this);

    // Rule 139: float cast to non-float
    Finder.addMatcher(
        cStyleCastExpr(
            hasSourceExpression(expr(hasType(realFloatingPointType())))
        ).bind("r139"), this);

    // Rule 143: empty switch
    Finder.addMatcher(
        switchStmt(
            unless(hasDescendant(caseStmt())),
            unless(hasDescendant(defaultStmt()))
        ).bind("r143"), this);

    // Rule 163: ++/-- mixed in binary expression
    Finder.addMatcher(
        unaryOperator(
            anyOf(hasOperatorName("++"), hasOperatorName("--")),
            hasParent(binaryOperator())
        ).bind("r163"), this);

    // Rule 165: && || operands not bool
    Finder.addMatcher(
        binaryOperator(
            anyOf(hasOperatorName("&&"), hasOperatorName("||")),
            hasLHS(expr(unless(hasType(booleanType()))))
        ).bind("r165"), this);

    // Rule 166: unary minus on unsigned
    Finder.addMatcher(
        unaryOperator(
            hasOperatorName("-"),
            hasUnaryOperand(expr(hasType(isUnsignedInteger())))
        ).bind("r166"), this);

    // Rule 171: comma operator
    Finder.addMatcher(
        binaryOperator(hasOperatorName(",")).bind("r171"), this);

    // Rule 181: if without braces
    Finder.addMatcher(ifStmt().bind("r181"), this);

    // Rule 182: else without braces
    Finder.addMatcher(
        ifStmt(hasElse(
            stmt(unless(anyOf(compoundStmt(), ifStmt())))
        )).bind("r182"), this);

    // Rule 188: float for loop counter
    Finder.addMatcher(
        forStmt(hasLoopInit(declStmt(
            containsDeclaration(0,
                varDecl(hasType(realFloatingPointType()))
            )
        ))).bind("r188"), this);

    // Rule 195: more than one break in a loop
    Finder.addMatcher(forStmt().bind("r195for"), this);
    Finder.addMatcher(whileStmt().bind("r195while"), this);
    Finder.addMatcher(doStmt().bind("r195do"), this);

    // Rule 196: multiple returns in function
    Finder.addMatcher(
        functionDecl(isDefinition(), unless(isMain())).bind("r196"), this);

    // Rule 200: global non-static variable
    Finder.addMatcher(
        varDecl(
            hasGlobalStorage(),
            unless(isStaticStorageClass()),
            unless(hasAncestor(functionDecl()))
        ).bind("r200"), this);

    // ── TYPE A NEW RULES ─────────────────────────────────────────────

    // Rule 4: No unused variables
    Finder.addMatcher(
        varDecl(
            isDefinition(),
            unless(hasAncestor(recordDecl())),
            unless(parmVarDecl())
        ).bind("r4"), this);

    // Rule 12: Every defined function shall be called at least once
    Finder.addMatcher(
        functionDecl(
            isDefinition(),
            unless(isMain()),
            unless(isExpansionInSystemHeader())
        ).bind("r12"), this);

    // Rule 28: typedef name shall be unique identifier
    Finder.addMatcher(
        typedefDecl().bind("r28"), this);

    // Rule 29: enum/union/struct name shall be unique
    Finder.addMatcher(
        enumDecl(isDefinition()).bind("r29enum"), this);

    // ── TYPE B RULES ─────────────────────────────────────────────────

    // Rule 2: Trigraphs in string literals
    Finder.addMatcher(stringLiteral().bind("r2"), this);

    // Rule 13: Octal constants
    Finder.addMatcher(integerLiteral().bind("r13"), this);

    // Rule 23: Braces for initialization — array with initListExpr
    Finder.addMatcher(
        varDecl(
            hasType(arrayType()),
            hasInitializer(initListExpr()),
            unless(isExpansionInSystemHeader())
        ).bind("r23"), this);

    // ── TYPE C RULES ─────────────────────────────────────────────────

    // Rule 1: Non-standard characters/escape sequences in string/char literals
    Finder.addMatcher(
        stringLiteral(
            unless(isExpansionInSystemHeader())
        ).bind("r1str"), this);
    Finder.addMatcher(
        characterLiteral(
            unless(isExpansionInSystemHeader())
        ).bind("r1char"), this);

    // Rule 6: Unreachable code — statement after return inside compound stmt
    Finder.addMatcher(
        compoundStmt(
            forEach(returnStmt().bind("r6ret"))
        ).bind("r6block"), this);

    // Rule 22: Automatic variable used without initializer
    Finder.addMatcher(
        varDecl(
            isDefinition(),
            unless(isExpansionInSystemHeader()),
            unless(hasGlobalStorage()),
            unless(isStaticStorageClass()),
            unless(parmVarDecl()),
            unless(hasInitializer(anything()))
        ).bind("r22"), this);

    // Rule 88: Function pointer assigned a function with mismatched signature
    Finder.addMatcher(
        binaryOperator(
            hasOperatorName("="),
            hasLHS(expr(hasType(pointsTo(functionType())))),
            hasRHS(expr(hasType(pointsTo(functionType()))))
        ).bind("r88assign"), this);
    Finder.addMatcher(
        varDecl(
            hasType(pointsTo(functionType())),
            hasInitializer(expr(hasType(pointsTo(functionType())))),
            unless(isExpansionInSystemHeader())
        ).bind("r88init"), this);

    // Rule 126: Branch condition is a compile-time constant (infeasible code)
    Finder.addMatcher(
        ifStmt(
            unless(isExpansionInSystemHeader())
        ).bind("r126if"), this);
    Finder.addMatcher(
        whileStmt(
            unless(isExpansionInSystemHeader())
        ).bind("r126while"), this);

    // Rule 134: RHS of shift operator shall not be negative
    Finder.addMatcher(
        binaryOperator(
            anyOf(hasOperatorName("<<"), hasOperatorName(">>")),
            hasRHS(expr(hasType(isSignedInteger()))),
            unless(isExpansionInSystemHeader())
        ).bind("r134"), this);

    // Rule 25: Side effects in RHS of && or ||
    Finder.addMatcher(
        binaryOperator(
            anyOf(hasOperatorName("&&"), hasOperatorName("||")),
            hasRHS(expr(hasDescendant(
                unaryOperator(anyOf(
                    hasOperatorName("++"),
                    hasOperatorName("--")
                ))
            )))
        ).bind("r25"), this);

    // Rule 32: Comma operator outside for loop
    Finder.addMatcher(
        binaryOperator(
            hasOperatorName(","),
            unless(hasAncestor(forStmt()))
        ).bind("r32"), this);

    // Rule 39: Dead code after return/goto
    Finder.addMatcher(
        compoundStmt(
            forEach(returnStmt().bind("retStmt"))
        ).bind("r39block"), this);

    // Rule 47: break mandatory in case clauses
    Finder.addMatcher(
        switchStmt(
            forEachDescendant(
                caseStmt(
                    unless(hasDescendant(breakStmt()))
                ).bind("r47case")
            )
        ).bind("r47switch"), this);

    // Rule 53: For counter not modified in body
    Finder.addMatcher(
        forStmt(
            hasLoopInit(declStmt(
                containsDeclaration(0, varDecl().bind("forVar"))
            )),
            hasBody(stmt(hasDescendant(
                binaryOperator(
                    anyOf(
                        hasOperatorName("="),
                        hasOperatorName("+="),
                        hasOperatorName("-=")
                    ),
                    hasLHS(declRefExpr(
                        to(varDecl(equalsBoundNode("forVar")))
                    ))
                )
            )))
        ).bind("r53"), this);

    // Rule 55: Cyclomatic complexity — detect functions with many branches
    Finder.addMatcher(
        functionDecl(
            isDefinition(),
            unless(isMain()),
            unless(isExpansionInSystemHeader())
        ).bind("r55"), this);

    // Rule 57: Recursion
    Finder.addMatcher(
        callExpr(
            callee(functionDecl().bind("calleeFunc")),
            hasAncestor(
                functionDecl(equalsBoundNode("calleeFunc")).bind("r57func")
            )
        ).bind("r57"), this);

    // Rule 64: At most one return per function
    Finder.addMatcher(
        functionDecl(
            isDefinition(),
            unless(isMain())
        ).bind("r64"), this);

    // ── TYPE B NEW RULES ─────────────────────────────────────────────

    // Rule 7: No identifier in one namespace shall have same spelling
    //         as an identifier in another namespace
    //         (tag name == non-tag name in the same scope)
    Finder.addMatcher(
        tagDecl(isDefinition()).bind("r7tag"), this);

    // Rule 11: Once a name is assigned as a typedef it shall not be
    //          reused for any other purpose (variable / function with
    //          the same name as an existing typedef)
    Finder.addMatcher(
        varDecl(unless(isExpansionInSystemHeader())).bind("r11var"), this);
    Finder.addMatcher(
        functionDecl(isDefinition(),
            unless(isExpansionInSystemHeader())).bind("r11fn"), this);

    // Rule 15: Identifier in inner scope shall not hide an outer-scope
    //          identifier (variable shadowing)
    Finder.addMatcher(
        varDecl(
            isDefinition(),
            unless(isExpansionInSystemHeader()),
            hasAncestor(compoundStmt())     // local variable
        ).bind("r15"), this);

    // Rule 18: Identifier with external linkage shall have exactly one
    //          external definition  (extern declaration without a
    //          matching definition in the TU)
    Finder.addMatcher(
        varDecl(
            hasExternalFormalLinkage(),
            unless(isDefinition()),
            unless(isExpansionInSystemHeader())
        ).bind("r18"), this);

    // Rule 19: External objects shall not be declared in more than one
    //          file (multiple extern declarations of same name)
    Finder.addMatcher(
        varDecl(
            hasExternalFormalLinkage(),
            unless(isExpansionInSystemHeader())
        ).bind("r19"), this);

    // Rule 24: In an enumerator list, '=' shall not be used to
    //          explicitly initialise members other than the first,
    //          unless all items are explicitly initialised
    Finder.addMatcher(
        enumDecl(isDefinition(),
            unless(isExpansionInSystemHeader())).bind("r24"), this);

    // Rule 56: Functions with variable number of arguments (ellipsis)
    //          shall not be used
    Finder.addMatcher(
        functionDecl(
            isVariadic(),
            unless(isExpansionInSystemHeader())
        ).bind("r56decl"), this);
    Finder.addMatcher(
        callExpr(
            callee(functionDecl(isVariadic())),
            unless(isExpansionInSystemHeader())
        ).bind("r56call"), this);

    // Rule 58: Functions shall have a prototype declaration and the
    //          prototype shall be visible at both definition and call
    //          (catches definitions with no prior declaration)
    Finder.addMatcher(
        functionDecl(
            isDefinition(),
            unless(isMain()),
            unless(isExpansionInSystemHeader())
        ).bind("r58"), this);

    // ── TYPE D RULES ─────────────────────────────────────────────────

    // Rule D3: Multibyte characters and wide string literals shall not be used
    Finder.addMatcher(
        stringLiteral(
            isWide(),
            unless(isExpansionInSystemHeader())
        ).bind("rd3str"), this);
    Finder.addMatcher(
        characterLiteral(
            unless(isExpansionInSystemHeader())
        ).bind("rd3char"), this);

    // Rule D10: Underlying bit representation of floating-point shall not be used
    // Catch: union containing float + int/char (type punning)
    Finder.addMatcher(
        recordDecl(
            isUnion(),
            unless(isExpansionInSystemHeader())
        ).bind("rd10"), this);

    // Rule D17: All declarations at file scope should be static where possible
    Finder.addMatcher(
        varDecl(
            hasGlobalStorage(),
            unless(isStaticStorageClass()),
            unless(hasExternalFormalLinkage()),
            unless(isExpansionInSystemHeader()),
            unless(parmVarDecl())
        ).bind("rd17"), this);

    // Rule D20: The register storage class specifier should not be used
    Finder.addMatcher(
        varDecl(
            hasLocalStorage(),
            unless(isExpansionInSystemHeader())
        ).bind("rd20"), this);

    // Rule D30: Unary minus shall not be applied to unsigned expression
    // (same as existing Rule 166 — bind separately for D30 label)
    Finder.addMatcher(
        unaryOperator(
            hasOperatorName("-"),
            hasUnaryOperand(
                expr(hasType(isUnsignedInteger()))
            ),
            unless(isExpansionInSystemHeader())
        ).bind("rd30"), this);

    // Rule D31: sizeof shall not be used on expressions with side effects
    Finder.addMatcher(
        sizeOfExpr(
            has(expr(anyOf(
                hasDescendant(unaryOperator(anyOf(
                    hasOperatorName("++"),
                    hasOperatorName("--")))),
                hasDescendant(callExpr())
            ))),
            unless(isExpansionInSystemHeader())
        ).bind("rd31"), this);

    // Rule D33: Implicit conversions which may result in loss of information
    Finder.addMatcher(
        implicitCastExpr(
            anyOf(
                hasCastKind(CK_IntegralToFloating),
                hasCastKind(CK_FloatingToIntegral),
                hasCastKind(CK_IntegralTruncation),
                hasCastKind(CK_FloatingCast)
            ),
            unless(isExpansionInSystemHeader())
        ).bind("rd33"), this);

    // Rule D34: Redundant explicit cast should not be used
    // Catch: explicit cast where types are already identical
    Finder.addMatcher(
        cStyleCastExpr(
            unless(isExpansionInSystemHeader())
        ).bind("rd34"), this);

    // Rule D35: Type casting from any type to or from pointers shall not be used
    Finder.addMatcher(
        cStyleCastExpr(
            anyOf(
                hasType(pointerType()),
                has(expr(hasType(pointerType())))
            ),
            unless(isExpansionInSystemHeader())
        ).bind("rd35"), this);

    // Rule D42: Labels should not be used except in switch statements
    Finder.addMatcher(
        labelStmt(
            unless(isExpansionInSystemHeader())
        ).bind("rd42"), this);

    // Rule D54: Functions shall always be declared at file scope
    // Catch: function declared inside another function (nested function decl)
    Finder.addMatcher(
        functionDecl(
            unless(isExpansionInSystemHeader()),
            hasAncestor(functionDecl())
        ).bind("rd54"), this);

    // Rule D69: The null pointer shall not be dereferenced
    Finder.addMatcher(
        unaryOperator(
            hasOperatorName("*"),
            hasUnaryOperand(
                expr(nullPointerConstant())
            ),
            unless(isExpansionInSystemHeader())
        ).bind("rd69"), this);

    // Rule D95: All struct/union members shall be named and accessed via name
    // Catch: anonymous struct/union members
    Finder.addMatcher(
        fieldDecl(
            unless(isExpansionInSystemHeader())
        ).bind("rd95"), this);

    // Rule D101: The macro offsetof shall not be used
    // Handled in preprocessor callback — matcher for call site
    Finder.addMatcher(
        callExpr(
            callee(functionDecl(hasName("__builtin_offsetof"))),
            unless(isExpansionInSystemHeader())
        ).bind("rd101"), this);
}

// ============================================================
// Helper: count branches for cyclomatic complexity
// ============================================================
static unsigned countBranches(const Stmt* S) {
    if (!S) return 0;
    unsigned cnt = 0;
    if (isa<IfStmt>(S) || isa<ForStmt>(S) ||
        isa<WhileStmt>(S) || isa<DoStmt>(S) ||
        isa<CaseStmt>(S) || isa<ConditionalOperator>(S))
        cnt++;
    for (const Stmt* child : S->children())
        cnt += countBranches(child);
    return cnt;
}

// ============================================================
// run
// ============================================================
void MyCheck::run(const MatchFinder::MatchResult& Result) {

    DiagnosticsEngine& DE = Result.Context->getDiagnostics();

    auto warn = [&](SourceLocation loc, const char* msg) {
        if (loc.isInvalid()) return;
        if (Result.Context->getSourceManager().isInSystemHeader(loc)) return;
        unsigned ID = DE.getCustomDiagID(DiagnosticsEngine::Warning, "%0");
        DE.Report(loc, ID).AddString(msg);
        };

    // ── TYPE A ────────────────────────────────────────────────────────

    // Rule 4: Unused variable detection
    if (const VarDecl* V = Result.Nodes.getNodeAs<VarDecl>("r4")) {
        if (!V->isReferenced() &&
            !Result.Context->getSourceManager().isInSystemHeader(V->getLocation()))
            warn(V->getBeginLoc(),
                "Rule 4: There shall be no unused variables");
    }

    // Rule 8
    if (const VarDecl* V = Result.Nodes.getNodeAs<VarDecl>("r8"))
        warn(V->getBeginLoc(),
            "Rule 8: Basic types shall not be used; "
            "use fixed-length typedefs e.g. int32_t, uint8_t");

    // Rule 12: Function defined but never called
    if (const FunctionDecl* FD = Result.Nodes.getNodeAs<FunctionDecl>("r12")) {
        if (!FD->isUsed() && !FD->getBuiltinID())
            warn(FD->getBeginLoc(),
                "Rule 12: Every defined function shall be called at least once");
    }

    // Rule 27
    if (const IfStmt* I = Result.Nodes.getNodeAs<IfStmt>("r27"))
        warn(I->getBeginLoc(),
            "Rule 27: Assignment operators shall not be used in boolean expressions");

    // Rule 28: typedef uniqueness
    if (const TypedefDecl* TD = Result.Nodes.getNodeAs<TypedefDecl>("r28")) {
        if (!Result.Context->getSourceManager().isInSystemHeader(TD->getLocation()))
            warn(TD->getBeginLoc(),
                "Rule 28: A typedef name shall be a unique identifier");
    }

    // Rule 29: enum uniqueness
    if (const EnumDecl* ED = Result.Nodes.getNodeAs<EnumDecl>("r29enum")) {
        if (!Result.Context->getSourceManager().isInSystemHeader(ED->getLocation()))
            warn(ED->getBeginLoc(),
                "Rule 29: An enum name shall be a unique identifier");
    }

    // Rule 34: Literal suffixes uppercase
    if (const IntegerLiteral* IL =
        Result.Nodes.getNodeAs<IntegerLiteral>("r34int")) {
        SourceManager& SM = Result.Context->getSourceManager();
        SourceLocation loc = IL->getBeginLoc();
        if (!SM.isInSystemHeader(loc)) {
            bool invalid = false;
            StringRef text = Lexer::getSourceText(
                CharSourceRange::getTokenRange(loc),
                SM, Result.Context->getLangOpts(), &invalid);
            if (!invalid) {
                // Check for lowercase l, u, ul, lu suffixes
                if (text.ends_with("l") || text.ends_with("u") ||
                    text.ends_with("ul") || text.ends_with("lu") ||
                    text.ends_with("ll") || text.ends_with("ull"))
                    warn(loc,
                        "Rule 34: Literal suffixes shall be upper case "
                        "(use L, U, UL, LL, ULL instead of l, u, ul, ll, ull)");
            }
        }
    }

    if (const FloatingLiteral* FL =
        Result.Nodes.getNodeAs<FloatingLiteral>("r34float")) {
        SourceManager& SM = Result.Context->getSourceManager();
        SourceLocation loc = FL->getBeginLoc();
        if (!SM.isInSystemHeader(loc)) {
            bool invalid = false;
            StringRef text = Lexer::getSourceText(
                CharSourceRange::getTokenRange(loc),
                SM, Result.Context->getLangOpts(), &invalid);
            if (!invalid) {
                if (text.ends_with("f") || text.ends_with("l"))
                    warn(loc,
                        "Rule 34: Literal suffixes shall be upper case "
                        "(use F, L instead of f, l)");
            }
        }
    }

    // Rule 38
    if (const BinaryOperator* B = Result.Nodes.getNodeAs<BinaryOperator>("r38"))
        warn(B->getBeginLoc(),
            "Rule 38: Floating point shall not be tested for exact equality or inequality");

    // Rule 41
    if (const NullStmt* N = Result.Nodes.getNodeAs<NullStmt>("r41"))
        warn(N->getBeginLoc(),
            "Rule 41: Null statement shall only occur on a line by itself");

    // Rule 43
    if (const GotoStmt* G = Result.Nodes.getNodeAs<GotoStmt>("r43"))
        warn(G->getBeginLoc(), "Rule 43: goto statement is prohibited");

    // Rule 44 break
    if (const BreakStmt* B = Result.Nodes.getNodeAs<BreakStmt>("r44b"))
        warn(B->getBeginLoc(), "Rule 44: break is forbidden in loops");

    // Rule 44 continue
    if (const ContinueStmt* C = Result.Nodes.getNodeAs<ContinueStmt>("r44c"))
        warn(C->getBeginLoc(), "Rule 44: continue is forbidden in loops");

    // Rule 46
    if (const IfStmt* I = Result.Nodes.getNodeAs<IfStmt>("r46"))
        warn(I->getBeginLoc(),
            "Rule 46: if/else-if must have a final else clause");

    // Rule 48/113
    if (const SwitchStmt* S = Result.Nodes.getNodeAs<SwitchStmt>("r48"))
        warn(S->getBeginLoc(),
            "Rule 48/113: switch must have a default clause");

    // Rule 49
    if (const SwitchStmt* S = Result.Nodes.getNodeAs<SwitchStmt>("r49"))
        warn(S->getBeginLoc(),
            "Rule 49: switch expression must not be a boolean value");

    // Rule 50
    if (const SwitchStmt* S = Result.Nodes.getNodeAs<SwitchStmt>("r50"))
        warn(S->getBeginLoc(),
            "Rule 50: switch must have at least one case");

    // Rule 51
    if (const VarDecl* V = Result.Nodes.getNodeAs<VarDecl>("r51"))
        warn(V->getBeginLoc(),
            "Rule 51: No floating point variables as loop counters");

    // Rule 59
    if (const FunctionDecl* FD = Result.Nodes.getNodeAs<FunctionDecl>("r59"))
        if (!FD->getBuiltinID())
            warn(FD->getBeginLoc(),
                "Rule 59: Every function shall have an explicit return type");

    // Rule 60
    if (const CallExpr* C = Result.Nodes.getNodeAs<CallExpr>("r60"))
        warn(C->getBeginLoc(),
            "Rule 60: Number of parameters passed does not match prototype");

    // Rule 61
    if (const CallExpr* C = Result.Nodes.getNodeAs<CallExpr>("r61"))
        warn(C->getBeginLoc(),
            "Rule 61: Values returned by void functions shall not be used");

    // Rule 83
    if (const BinaryOperator* B = Result.Nodes.getNodeAs<BinaryOperator>("r83"))
        warn(B->getBeginLoc(), "Rule 83: Pointer arithmetic should not be used");

    // Rule 86
    if (const VarDecl* V = Result.Nodes.getNodeAs<VarDecl>("r86"))
        warn(V->getBeginLoc(),
            "Rule 86: Non-constant pointers to functions shall not be used");

    // Rule 93
    if (const FieldDecl* F = Result.Nodes.getNodeAs<FieldDecl>("r93"))
        warn(F->getBeginLoc(),
            "Rule 93: Bit fields shall only be of type unsigned int or signed int");

    // Rule 94
    if (const FieldDecl* F = Result.Nodes.getNodeAs<FieldDecl>("r94"))
        if (F->getBitWidthValue(*Result.Context) < 2)
            warn(F->getBeginLoc(),
                "Rule 94: Bit fields of signed int shall be at least 2 bits long");

    // Rule 98
    if (const FunctionDecl* FD = Result.Nodes.getNodeAs<FunctionDecl>("r98"))
        warn(FD->getBeginLoc(),
            "Rule 98: Standard library function names shall not be reused");

    // Rule 99
    if (const CallExpr* C = Result.Nodes.getNodeAs<CallExpr>("r99"))
        warn(C->getBeginLoc(),
            "Rule 99: Dynamic memory allocation shall not be used");

    // Rule 100
    if (const DeclRefExpr* D = Result.Nodes.getNodeAs<DeclRefExpr>("r100"))
        warn(D->getBeginLoc(), "Rule 100: errno shall not be used");

    // Rule 105
    if (const CallExpr* C = Result.Nodes.getNodeAs<CallExpr>("r105"))
        warn(C->getBeginLoc(),
            "Rule 105: signal.h facilities shall not be used");

    // Rule 106
    if (const CallExpr* C = Result.Nodes.getNodeAs<CallExpr>("r106"))
        warn(C->getBeginLoc(),
            "Rule 106: stdio.h shall not be used in production code");

    // Rule 107
    if (const CallExpr* C = Result.Nodes.getNodeAs<CallExpr>("r107"))
        warn(C->getBeginLoc(),
            "Rule 107: atof/atoi/atol shall not be used");

    // Rule 108
    if (const CallExpr* C = Result.Nodes.getNodeAs<CallExpr>("r108"))
        warn(C->getBeginLoc(),
            "Rule 108: abort/exit/getenv/system shall not be used");

    // Rule 119
    if (const VarDecl* V = Result.Nodes.getNodeAs<VarDecl>("r119"))
        warn(V->getBeginLoc(),
            "Rule 119: Incomplete array declarations are not permitted");

    // Rule 120
    if (const VarDecl* V = Result.Nodes.getNodeAs<VarDecl>("r120")) {
        if (V->hasInit()) {
            if (const InitListExpr* IL = dyn_cast<InitListExpr>(V->getInit())) {
                if (const ConstantArrayType* CA =
                    dyn_cast<ConstantArrayType>(V->getType().getTypePtr())) {
                    if (IL->getNumInits() < CA->getSize().getZExtValue())
                        warn(V->getBeginLoc(),
                            "Rule 120: Array not fully initialized");
                }
            }
        }
    }

    // Rule 123
    if (const CStyleCastExpr* C = Result.Nodes.getNodeAs<CStyleCastExpr>("r123"))
        warn(C->getBeginLoc(),
            "Rule 123: Cast from integer to pointer shall not be performed");

    // Rule 124 & 161
    if (const CStyleCastExpr* C = Result.Nodes.getNodeAs<CStyleCastExpr>("r124")) {
        QualType dest = C->getType();
        if (dest->isIntegerType())
            warn(C->getBeginLoc(),
                "Rule 124: Pointer cast to integer is not allowed");
        if (dest->isPointerType())
            warn(C->getBeginLoc(),
                "Rule 161: Invalid pointer conversion from void pointer");
    }

    // Rule 125
    if (const ConditionalOperator* C =
        Result.Nodes.getNodeAs<ConditionalOperator>("r125")) {
        QualType t1 = C->getTrueExpr()->IgnoreImpCasts()->getType()
            .getUnqualifiedType();
        QualType t2 = C->getFalseExpr()->IgnoreImpCasts()->getType()
            .getUnqualifiedType();
        if (!Result.Context->hasSameType(t1, t2))
            warn(C->getBeginLoc(),
                "Rule 125: Conditional operator has incompatible types");
    }

    // Rule 127
    if (const ImplicitCastExpr* I =
        Result.Nodes.getNodeAs<ImplicitCastExpr>("r127")) {
        if (I->getSubExpr()->getType()->isIntegerType() &&
            I->getType()->isFloatingType())
            warn(I->getBeginLoc(),
                "Rule 127: Implicit integer to float conversion");
    }

    // Rule 128
    if (const ImplicitCastExpr* I =
        Result.Nodes.getNodeAs<ImplicitCastExpr>("r128")) {
        QualType src = I->getSubExpr()->getType();
        QualType dst = I->getType();
        if (src->isFloatingType() && dst->isFloatingType())
            if (Result.Context->getTypeSize(dst) < Result.Context->getTypeSize(src))
                warn(I->getBeginLoc(),
                    "Rule 128: Narrower float conversion without explicit cast");
    }

    // Rule 129
    if (const ImplicitCastExpr* I =
        Result.Nodes.getNodeAs<ImplicitCastExpr>("r129")) {
        QualType src = I->getSubExpr()->getType();
        QualType dst = I->getType();
        if (src->isIntegerType() && dst->isIntegerType())
            if (Result.Context->getTypeSize(dst) < Result.Context->getTypeSize(src))
                warn(I->getBeginLoc(),
                    "Rule 129: Narrower int conversion without explicit cast");
    }

    // Rule 130
    if (const StringLiteral* SL = Result.Nodes.getNodeAs<StringLiteral>("r130")) {
        StringRef s = SL->getString();
        if (s.contains("<:") || s.contains(":>") ||
            s.contains("<%") || s.contains("%>") ||
            s.contains("%:"))
            warn(SL->getBeginLoc(),
                "Rule 130: Use of digraph characters is not permitted");
    }

    // Rule 136
    if (const IntegerLiteral* I = Result.Nodes.getNodeAs<IntegerLiteral>("r136"))
        warn(I->getBeginLoc(),
            "Rule 136: Magic number used; use symbolic constants instead");

    // Rule 137
    if (const ArraySubscriptExpr* A =
        Result.Nodes.getNodeAs<ArraySubscriptExpr>("r137"))
        warn(A->getBeginLoc(),
            "Rule 137: Numeric literal used as array subscript; "
            "use symbolic name instead");

    // Rule 139
    if (const CStyleCastExpr* C = Result.Nodes.getNodeAs<CStyleCastExpr>("r139"))
        if (!C->getType()->isFloatingType())
            warn(C->getBeginLoc(),
                "Rule 139: Float value cast to non-float type");

    // Rule 143
    if (const SwitchStmt* S = Result.Nodes.getNodeAs<SwitchStmt>("r143"))
        warn(S->getBeginLoc(), "Rule 143: Empty switch statement");

    // Rule 163
    if (const UnaryOperator* U = Result.Nodes.getNodeAs<UnaryOperator>("r163"))
        warn(U->getBeginLoc(),
            "Rule 163: Increment/decrement should not be mixed with other operators");

    // Rule 165
    if (const BinaryOperator* B = Result.Nodes.getNodeAs<BinaryOperator>("r165"))
        warn(B->getBeginLoc(),
            "Rule 165: Each operand of && or || shall have essentially Boolean type");

    // Rule 166
    if (const UnaryOperator* U = Result.Nodes.getNodeAs<UnaryOperator>("r166"))
        warn(U->getBeginLoc(),
            "Rule 166: Unary minus shall not be applied to an unsigned expression");

    // Rule 171
    if (const BinaryOperator* B = Result.Nodes.getNodeAs<BinaryOperator>("r171"))
        warn(B->getBeginLoc(), "Rule 171: The comma operator shall not be used");

    // Rule 181
    if (const IfStmt* I = Result.Nodes.getNodeAs<IfStmt>("r181"))
        if (!isa<CompoundStmt>(I->getThen()))
            warn(I->getBeginLoc(),
                "Rule 181: If must use compound statement {}");

    // Rule 182
    if (const IfStmt* I = Result.Nodes.getNodeAs<IfStmt>("r182"))
        warn(I->getBeginLoc(),
            "Rule 182: else must be followed by compound statement {}");

    // Rule 188
    if (const ForStmt* F = Result.Nodes.getNodeAs<ForStmt>("r188"))
        warn(F->getBeginLoc(),
            "Rule 188: for loop shall not have a float loop counter");

    // Rule 195
    if (const ForStmt* F = Result.Nodes.getNodeAs<ForStmt>("r195for"))
        if (F->getBody() && countDirectBreaks(F->getBody()) > 1)
            warn(F->getBeginLoc(),
                "Rule 195: No more than one break statement per loop");
    if (const WhileStmt* W = Result.Nodes.getNodeAs<WhileStmt>("r195while"))
        if (W->getBody() && countDirectBreaks(W->getBody()) > 1)
            warn(W->getBeginLoc(),
                "Rule 195: No more than one break statement per loop");
    if (const DoStmt* D = Result.Nodes.getNodeAs<DoStmt>("r195do"))
        if (D->getBody() && countDirectBreaks(D->getBody()) > 1)
            warn(D->getBeginLoc(),
                "Rule 195: No more than one break statement per loop");

    // Rule 196
    if (const FunctionDecl* FD = Result.Nodes.getNodeAs<FunctionDecl>("r196"))
        if (!FD->getBuiltinID() && FD->hasBody())
            if (countReturns(FD->getBody()) > 1)
                warn(FD->getBeginLoc(),
                    "Rule 196: Function shall have a single point of exit");

    // Rule 200
    if (const VarDecl* V = Result.Nodes.getNodeAs<VarDecl>("r200"))
        warn(V->getBeginLoc(),
            "Rule 200: Global variables should be avoided");

    // ── TYPE B ────────────────────────────────────────────────────────

    // Rule 2: Trigraphs
    if (const StringLiteral* SL = Result.Nodes.getNodeAs<StringLiteral>("r2")) {
        StringRef s = SL->getString();
        if (s.contains("??=") || s.contains("??(") || s.contains("??)") ||
            s.contains("??/") || s.contains("??'") || s.contains("??<") ||
            s.contains("??>") || s.contains("??!") || s.contains("??-"))
            warn(SL->getBeginLoc(),
                "Rule 2: Trigraphs shall not be used");
    }

    // Rule 13: Octal constants
    if (const IntegerLiteral* IL =
        Result.Nodes.getNodeAs<IntegerLiteral>("r13")) {
        SourceManager& SM = Result.Context->getSourceManager();
        SourceLocation loc = IL->getBeginLoc();
        if (loc.isValid() && !SM.isInSystemHeader(loc)) {
            bool invalid = false;
            StringRef text = Lexer::getSourceText(
                CharSourceRange::getTokenRange(loc),
                SM, Result.Context->getLangOpts(), &invalid);
            if (!invalid && text.size() > 1 &&
                text[0] == '0' &&
                text[1] >= '1' && text[1] <= '7')
                warn(loc,
                    "Rule 13: Octal constants other than zero shall not be used");
        }
    }

    // Rule 25: Side effects in RHS of && or ||
    if (const BinaryOperator* B = Result.Nodes.getNodeAs<BinaryOperator>("r25"))
        warn(B->getBeginLoc(),
            "Rule 25: The right-hand operand of && or || "
            "shall not contain side effects");

    // Rule 32: Comma outside for loop
    if (const BinaryOperator* B = Result.Nodes.getNodeAs<BinaryOperator>("r32"))
        warn(B->getBeginLoc(),
            "Rule 32: The comma operator shall not be used "
            "except in the control expression of a for loop");

    // Rule 39: Dead code after return/goto
    if (const CompoundStmt* CS =
        Result.Nodes.getNodeAs<CompoundStmt>("r39block")) {
        bool seenTerminator = false;
        for (const Stmt* S : CS->body()) {
            if (seenTerminator && !isa<LabelStmt>(S)) {
                warn(S->getBeginLoc(),
                    "Rule 39: No inaccessible code after a goto or return statement");
                break;
            }
            if (isa<ReturnStmt>(S) || isa<GotoStmt>(S))
                seenTerminator = true;
        }
    }

    // Rule 47: break in case
    if (const CaseStmt* CS = Result.Nodes.getNodeAs<CaseStmt>("r47case")) {
        const Stmt* sub = CS->getSubStmt();
        bool hasTerminator = false;
        if (sub) {
            if (isa<BreakStmt>(sub) || isa<ReturnStmt>(sub))
                hasTerminator = true;
            if (const CompoundStmt* comp = dyn_cast<CompoundStmt>(sub)) {
                for (const Stmt* s : comp->body())
                    if (isa<BreakStmt>(s) || isa<ReturnStmt>(s)) {
                        hasTerminator = true; break;
                    }
            }
        }
        if (!hasTerminator)
            warn(CS->getBeginLoc(),
                "Rule 47: The break statement shall be the last statement "
                "in each case clause of a switch statement");
    }

    // Rule 53: For counter modified in body
    if (const ForStmt* F = Result.Nodes.getNodeAs<ForStmt>("r53"))
        warn(F->getBeginLoc(),
            "Rule 53: The counter in a for statement must not be "
            "modified inside the loop body");

    // Rule 55: Cyclomatic complexity > 10
    if (const FunctionDecl* FD = Result.Nodes.getNodeAs<FunctionDecl>("r55")) {
        if (!FD->getBuiltinID() && FD->hasBody()) {
            unsigned complexity = 1 + countBranches(FD->getBody());
            if (complexity > 10)
                warn(FD->getBeginLoc(),
                    "Rule 55: Functions shall have a cyclomatic complexity "
                    "number of 10 or less");
        }
    }

    // Rule 57: Recursion
    if (Result.Nodes.getNodeAs<CallExpr>("r57")) {
        const FunctionDecl* FD =
            Result.Nodes.getNodeAs<FunctionDecl>("r57func");
        if (FD && !FD->getBuiltinID())
            warn(FD->getBeginLoc(),
                "Rule 57: It is not recommended to use recursion");
    }

    // Rule 64: At most one return per function
    if (const FunctionDecl* FD = Result.Nodes.getNodeAs<FunctionDecl>("r64")) {
        if (!FD->getBuiltinID() && FD->hasBody())
            if (countReturns(FD->getBody()) > 1)
                warn(FD->getBeginLoc(),
                    "Rule 64: At most one return statement per function");
    }

    // ── TYPE B NEW RULES ─────────────────────────────────────────────

    // Rule 7: No identifier in one namespace shall have the same
    //         spelling as an identifier in another namespace.
    //         We flag every tag (struct/union/enum) whose name
    //         collides with a non-tag (typedef / variable / function)
    //         in the same translation unit.
    if (const TagDecl* TD = Result.Nodes.getNodeAs<TagDecl>("r7tag")) {
        if (!TD->getName().empty() &&
            !Result.Context->getSourceManager().isInSystemHeader(
                TD->getLocation())) {
            // Look up the same name in the ordinary (non-tag) namespace
            DeclarationName DN =
                Result.Context->DeclarationNames.getIdentifier(
                    &Result.Context->Idents.get(TD->getName()));
            DeclContext::lookup_result res =
                TD->getDeclContext()->lookup(DN);
            for (NamedDecl* ND : res) {
                if (!isa<TagDecl>(ND)) {
                    warn(TD->getLocation(),
                        "Rule 7: Identifier in tag namespace has same "
                        "spelling as an identifier in another namespace");
                    break;
                }
            }
        }
    }

    // Rule 11: Once a name has been assigned as a typedef it shall not
    //          be reused for any other purpose.
    //          Strategy: collect all typedef names first, then flag any
    //          var/function that shares a name with a typedef at file scope.
    if (const VarDecl* VD = Result.Nodes.getNodeAs<VarDecl>("r11var")) {
        if (!VD->getName().empty() &&
            !Result.Context->getSourceManager().isInSystemHeader(
                VD->getLocation())) {
            // Search all typedef declarations in the same translation unit
            ASTContext& Ctx = *Result.Context;
            const TranslationUnitDecl* TU = Ctx.getTranslationUnitDecl();
            for (const Decl* D : TU->decls()) {
                if (const TypedefDecl* TD = dyn_cast<TypedefDecl>(D)) {
                    if (TD->getName() == VD->getName() &&
                        !Ctx.getSourceManager().isInSystemHeader(
                            TD->getLocation())) {
                        warn(VD->getLocation(),
                            "Rule 11: Once a name is assigned as a typedef "
                            "it shall not be reused for any other purpose");
                        break;
                    }
                }
            }
        }
    }
    if (const FunctionDecl* FD = Result.Nodes.getNodeAs<FunctionDecl>("r11fn")) {
        if (!FD->getName().empty() && !FD->getBuiltinID() &&
            !Result.Context->getSourceManager().isInSystemHeader(
                FD->getLocation())) {
            ASTContext& Ctx = *Result.Context;
            const TranslationUnitDecl* TU = Ctx.getTranslationUnitDecl();
            for (const Decl* D : TU->decls()) {
                if (const TypedefDecl* TD = dyn_cast<TypedefDecl>(D)) {
                    if (TD->getName() == FD->getName() &&
                        !Ctx.getSourceManager().isInSystemHeader(
                            TD->getLocation())) {
                        warn(FD->getLocation(),
                            "Rule 11: Once a name is assigned as a typedef "
                            "it shall not be reused for any other purpose");
                        break;
                    }
                }
            }
        }
    }

    // Rule 15: Identifier in inner scope shall not hide an outer-scope
    //          identifier (variable shadowing).
    if (const VarDecl* VD = Result.Nodes.getNodeAs<VarDecl>("r15")) {
        ASTContext& Ctx = *Result.Context;
        SourceManager& SM = Ctx.getSourceManager();
        if (!SM.isInSystemHeader(VD->getLocation()) &&
            !VD->getName().empty()) {
            IdentifierInfo& II = Ctx.Idents.get(VD->getName());
            DeclarationName DN = Ctx.DeclarationNames.getIdentifier(&II);
            // Walk up parent contexts to detect a shadowed declaration
            const DeclContext* parent =
                VD->getDeclContext()->getParent();
            while (parent) {
                DeclContext::lookup_result res = parent->lookup(DN);
                for (NamedDecl* ND : res) {
                    if (isa<VarDecl>(ND) || isa<ParmVarDecl>(ND)) {
                        warn(VD->getLocation(),
                            "Rule 15: Identifier in inner scope hides "
                            "an identifier in an outer scope");
                        goto r15_done;
                    }
                }
                parent = parent->getParent();
            }
        r15_done:;
        }
    }

    // Rule 18: Identifier with external linkage shall have exactly one
    //          external definition — flag every bare 'extern' declaration
    //          that has no definition in this translation unit.
    if (const VarDecl* VD = Result.Nodes.getNodeAs<VarDecl>("r18")) {
        // If there is no definition reachable from this decl, warn.
        if (!VD->getDefinition()) {
            warn(VD->getLocation(),
                "Rule 18: An identifier with external linkage shall "
                "have exactly one external definition");
        }
    }

    // Rule 19: External objects should not be declared in more than
    //          one file — flag every extern variable declaration
    //          (the tool user can review duplicates across files).
    if (const VarDecl* VD = Result.Nodes.getNodeAs<VarDecl>("r19")) {
        if (VD->isThisDeclarationADefinition() != VarDecl::Definition &&
            VD->getLinkageInternal() == clang::Linkage::External) {
            warn(VD->getLocation(),
                "Rule 19: External objects should not be declared "
                "in more than one file");
        }
    }

    // Rule 24: In an enumerator list the '=' construct shall not be
    //          used to explicitly initialise members other than the
    //          first, unless all items are explicitly initialised.
    if (const EnumDecl* ED = Result.Nodes.getNodeAs<EnumDecl>("r24")) {
        if (!Result.Context->getSourceManager().isInSystemHeader(
            ED->getLocation())) {
            bool allExplicit = true;
            bool anyExplicit = false;
            bool firstChecked = false;
            bool firstExplicit = false;
            for (const EnumConstantDecl* ECD : ED->enumerators()) {
                bool hasInit = (ECD->getInitExpr() != nullptr);
                if (!firstChecked) {
                    firstExplicit = hasInit;
                    firstChecked = true;
                }
                if (hasInit) anyExplicit = true;
                else         allExplicit = false;
            }
            // Violation: some (but not all) non-first members are explicit
            if (anyExplicit && !allExplicit) {
                warn(ED->getBeginLoc(),
                    "Rule 24: In an enumerator list, '=' shall not be "
                    "used to explicitly initialise members other than "
                    "the first, unless all items are explicitly "
                    "initialised");
            }
        }
    }

    // Rule 56: Functions with variable number of arguments (ellipsis)
    //          shall not be used.
    if (const FunctionDecl* FD =
        Result.Nodes.getNodeAs<FunctionDecl>("r56decl")) {
        if (!FD->getBuiltinID())
            warn(FD->getBeginLoc(),
                "Rule 56: Functions with variable number of arguments "
                "shall not be used");
    }
    if (const CallExpr* CE = Result.Nodes.getNodeAs<CallExpr>("r56call")) {
        warn(CE->getBeginLoc(),
            "Rule 56: Functions with variable number of arguments "
            "shall not be used");
    }

    // Rule 58: Functions shall have a prototype declaration visible at
    //          both the definition and every call site.
    //          Flag any function definition whose canonical declaration
    //          is not a forward prototype (i.e. first seen at definition).
    if (const FunctionDecl* FD =
        Result.Nodes.getNodeAs<FunctionDecl>("r58")) {
        if (!FD->getBuiltinID()) {
            const FunctionDecl* canon = FD->getCanonicalDecl();
            // If the canonical decl IS the definition, there was no
            // prior prototype declaration.
            if (canon == FD || canon->isThisDeclarationADefinition()) {
                warn(FD->getBeginLoc(),
                    "Rule 58: Functions shall have a prototype "
                    "declaration visible at both the function "
                    "definition and call site");
            }
        }
    }

    // Rule 23: Braces for array initialization (previously missing handler)
    if (const VarDecl* VD = Result.Nodes.getNodeAs<VarDecl>("r23")) {
        warn(VD->getBeginLoc(),
            "Rule 23: Braces shall be used to indicate and match the "
            "structure in the non-zero initialization of arrays");
    }

    // ── TYPE C RULES ─────────────────────────────────────────────────

    // Rule 1: Non-standard characters or escape sequences in literals
    if (const StringLiteral* SL =
        Result.Nodes.getNodeAs<StringLiteral>("r1str")) {
        StringRef s = SL->getString();
        for (char c : s) {
            // Non-printable, non-standard ASCII characters
            unsigned char uc = static_cast<unsigned char>(c);
            if (uc > 126 && uc != '\t' && uc != '\n' && uc != '\r') {
                warn(SL->getBeginLoc(),
                    "Rule 1: Only characters and escape sequences defined "
                    "in the ISO C standard shall be used");
                break;
            }
        }
    }
    if (const CharacterLiteral* CL =
        Result.Nodes.getNodeAs<CharacterLiteral>("r1char")) {
        unsigned val = CL->getValue();
        // Flag non-ASCII or non-standard character values
        if (val > 127) {
            warn(CL->getBeginLoc(),
                "Rule 1: Only characters and escape sequences defined "
                "in the ISO C standard shall be used");
        }
    }

    // Rule 6: Unreachable code after return inside a block
    if (const CompoundStmt* CS =
        Result.Nodes.getNodeAs<CompoundStmt>("r6block")) {
        bool seenReturn = false;
        for (const Stmt* S : CS->body()) {
            if (seenReturn && !isa<LabelStmt>(S) && !isa<NullStmt>(S)) {
                warn(S->getBeginLoc(),
                    "Rule 6: A project shall not contain any unreachable code");
                break;
            }
            if (isa<ReturnStmt>(S)) seenReturn = true;
        }
    }

    // Rule 22: Automatic variable declared without initializer
    if (const VarDecl* VD = Result.Nodes.getNodeAs<VarDecl>("r22")) {
        // Only warn for non-array, non-struct local vars without init
        QualType QT = VD->getType();
        if (!QT->isArrayType() && !QT->isRecordType() &&
            !QT->isPointerType()) {
            warn(VD->getBeginLoc(),
                "Rule 22: All automatic variables shall be assigned "
                "a value before being used");
        }
    }

    // Rule 88: Function pointer assigned a function with mismatched type
    if (const BinaryOperator* BO =
        Result.Nodes.getNodeAs<BinaryOperator>("r88assign")) {
        QualType lhsType = BO->getLHS()->getType();
        QualType rhsType = BO->getRHS()->getType();
        if (!Result.Context->hasSameType(lhsType, rhsType)) {
            warn(BO->getBeginLoc(),
                "Rule 88: All functions pointed to by a function pointer "
                "shall be identical in number/type of parameters and return type");
        }
    }
    if (const VarDecl* VD = Result.Nodes.getNodeAs<VarDecl>("r88init")) {
        QualType varType = VD->getType();
        QualType initType = VD->getInit()->getType();
        if (!Result.Context->hasSameType(varType, initType)) {
            warn(VD->getBeginLoc(),
                "Rule 88: All functions pointed to by a function pointer "
                "shall be identical in number/type of parameters and return type");
        }
    }

    // Rule 126: Branch condition evaluates to a compile-time constant
    if (const IfStmt* IS = Result.Nodes.getNodeAs<IfStmt>("r126if")) {
        const Expr* cond = IS->getCond()->IgnoreParenImpCasts();
        Expr::EvalResult evalResult;
        if (cond->EvaluateAsInt(evalResult, *Result.Context)) {
            warn(IS->getBeginLoc(),
                "Rule 126: Construct leads to infeasible code — "
                "branch condition evaluates to a constant value");
        }
    }
    if (const WhileStmt* WS = Result.Nodes.getNodeAs<WhileStmt>("r126while")) {
        const Expr* cond = WS->getCond()->IgnoreParenImpCasts();
        Expr::EvalResult evalResult;
        // Skip while(1) / while(true) which are intentional infinite loops
        if (cond->EvaluateAsInt(evalResult, *Result.Context)) {
            llvm::APSInt val = evalResult.Val.getInt();
            if (val == 0) {  // while(0) is always dead
                warn(WS->getBeginLoc(),
                    "Rule 126: Construct leads to infeasible code — "
                    "loop condition evaluates to a constant value");
            }
        }
    }

    // Rule 134: RHS of shift operator shall not be negative
    if (const BinaryOperator* BO =
        Result.Nodes.getNodeAs<BinaryOperator>("r134")) {
        const Expr* rhs = BO->getRHS()->IgnoreParenImpCasts();
        Expr::EvalResult evalResult;
        if (rhs->EvaluateAsInt(evalResult, *Result.Context)) {
            llvm::APSInt val = evalResult.Val.getInt();
            if (val.isNegative()) {
                warn(BO->getBeginLoc(),
                    "Rule 134: RHS of shift operator shall not be negative");
            }
        }
        else {
            warn(BO->getBeginLoc(),
                "Rule 134: RHS of shift operator shall not be negative "
                "(signed integer may be negative at runtime)");
        }
    }

    // ── TYPE D RULES ─────────────────────────────────────────────────

    // Rule D3: Wide string literals shall not be used
    if (const StringLiteral* SL =
        Result.Nodes.getNodeAs<StringLiteral>("rd3str")) {
        if (SL->isWide() || SL->getCharByteWidth() > 1) {
            warn(SL->getBeginLoc(),
                "Rule D3: Multibyte characters and wide string literals "
                "shall not be used");
        }
    }
    if (const CharacterLiteral* CL =
        Result.Nodes.getNodeAs<CharacterLiteral>("rd3char")) {
        if (CL->getKind() != CharacterLiteralKind::Ascii) {
            warn(CL->getBeginLoc(),
                "Rule D3: Multibyte characters and wide string literals "
                "shall not be used");
        }
    }

    // Rule D10: Underlying bit representation of floating-point shall not
    //           be used — detect union containing both float and integer members
    if (const RecordDecl* RD =
        Result.Nodes.getNodeAs<RecordDecl>("rd10")) {
        bool hasFloat = false, hasInt = false;
        for (const FieldDecl* FD : RD->fields()) {
            QualType QT = FD->getType();
            if (QT->isFloatingType())   hasFloat = true;
            if (QT->isIntegerType())    hasInt = true;
        }
        if (hasFloat && hasInt) {
            warn(RD->getBeginLoc(),
                "Rule D10: The underlying bit representation of "
                "floating-point numbers shall not be used (union type punning)");
        }
    }

    // Rule D17: File-scope declarations should be static where possible
    if (const VarDecl* VD = Result.Nodes.getNodeAs<VarDecl>("rd17")) {
        if (!Result.Context->getSourceManager().isInSystemHeader(
            VD->getLocation())) {
            warn(VD->getBeginLoc(),
                "Rule D17: All declarations at file scope should be "
                "static where possible");
        }
    }

    // Rule D20: register storage class specifier should not be used
    if (const VarDecl* VD = Result.Nodes.getNodeAs<VarDecl>("rd20")) {
        if (VD->getStorageClass() == SC_Register) {
            warn(VD->getBeginLoc(),
                "Rule D20: The register storage class specifier "
                "should not be used");
        }
    }

    // Rule D30: Unary minus shall not be applied to unsigned expression
    if (const UnaryOperator* UO =
        Result.Nodes.getNodeAs<UnaryOperator>("rd30")) {
        warn(UO->getBeginLoc(),
            "Rule D30: Applying the unary minus operator to an unsigned "
            "expression is not meaningful");
    }

    // Rule D31: sizeof shall not be used on expressions with side effects
    if (const UnaryExprOrTypeTraitExpr* SE =
        Result.Nodes.getNodeAs<UnaryExprOrTypeTraitExpr>("rd31")) {
        warn(SE->getBeginLoc(),
            "Rule D31: The sizeof operator should not be used on "
            "expressions that contain side effects");
    }

    // Rule D33: Implicit conversions that may result in loss of information
    if (const ImplicitCastExpr* ICE =
        Result.Nodes.getNodeAs<ImplicitCastExpr>("rd33")) {
        CastKind CK = ICE->getCastKind();
        const char* detail = "";
        if (CK == CK_IntegralToFloating)  detail = "(int to float)";
        else if (CK == CK_FloatingToIntegral) detail = "(float to int)";
        else if (CK == CK_IntegralTruncation) detail = "(narrowing integral)";
        else if (CK == CK_FloatingCast)   detail = "(narrowing float)";
        warn(ICE->getBeginLoc(),
            std::string("Rule D33: Implicit conversion which may result in "
                "loss of information ") + detail);
    }

    // Rule D34: Redundant explicit cast should not be used
    if (const CStyleCastExpr* CE =
        Result.Nodes.getNodeAs<CStyleCastExpr>("rd34")) {
        QualType srcType = CE->getSubExpr()->getType();
        QualType destType = CE->getType();
        if (Result.Context->hasSameUnqualifiedType(srcType, destType)) {
            warn(CE->getBeginLoc(),
                "Rule D34: Redundant explicit cast — source and destination "
                "types are identical");
        }
    }

    // Rule D35: Type casting from any type to or from pointers shall not be used
    if (const CStyleCastExpr* CE =
        Result.Nodes.getNodeAs<CStyleCastExpr>("rd35")) {
        QualType srcType = CE->getSubExpr()->getType();
        QualType destType = CE->getType();
        bool srcPtr = srcType->isPointerType();
        bool destPtr = destType->isPointerType();
        if (srcPtr || destPtr) {
            warn(CE->getBeginLoc(),
                "Rule D35: Type casting from any type to or from pointers "
                "shall not be used");
        }
    }

    // Rule D42: Labels should not be used except in switch statements
    if (const LabelStmt* LS =
        Result.Nodes.getNodeAs<LabelStmt>("rd42")) {
        // Check if the label is inside a switch statement
        // If parent is not a switch, it's a violation (goto label)
        warn(LS->getBeginLoc(),
            "Rule D42: Labels should not be used except in switch statements");
    }

    // Rule D54: Functions shall always be declared at file scope
    if (const FunctionDecl* FD =
        Result.Nodes.getNodeAs<FunctionDecl>("rd54")) {
        if (!FD->getBuiltinID()) {
            warn(FD->getBeginLoc(),
                "Rule D54: Functions shall always be declared at file scope "
                "(nested function declaration detected)");
        }
    }

    // Rule D69: Null pointer shall not be dereferenced
    if (const UnaryOperator* UO =
        Result.Nodes.getNodeAs<UnaryOperator>("rd69")) {
        warn(UO->getBeginLoc(),
            "Rule D69: The null pointer shall not be dereferenced");
    }

    // Rule D95: All struct/union members shall be named
    if (const FieldDecl* FD =
        Result.Nodes.getNodeAs<FieldDecl>("rd95")) {
        if (FD->isAnonymousStructOrUnion() || FD->getDeclName().isEmpty()) {
            warn(FD->getBeginLoc(),
                "Rule D95: All members of a structure or union shall be "
                "named and accessed only via their name");
        }
    }

    // Rule D101: The macro offsetof shall not be used
    if (const CallExpr* CE =
        Result.Nodes.getNodeAs<CallExpr>("rd101")) {
        warn(CE->getBeginLoc(),
            "Rule D101: The macro offsetof in library <stddef.h> "
            "shall not be used");
    }
}

// ============================================================
// Consumer + Action
// ============================================================
class MyASTConsumer : public ASTConsumer {
public:
    MatchFinder Finder;
    MyCheck     Check;
    MyASTConsumer() { Check.registerMatchers(Finder); }
    void HandleTranslationUnit(ASTContext& Ctx) override {
        Finder.matchAST(Ctx);
    }
};

std::unique_ptr<ASTConsumer>
MyFrontendAction::CreateASTConsumer(CompilerInstance& CI, StringRef file) {
    CI.getPreprocessor().addPPCallbacks(
        std::make_unique<MISRAPreprocessorCallback>(
            CI.getPreprocessor(), CI.getDiagnostics()));
    return std::make_unique<MyASTConsumer>();
}

#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"

static llvm::cl::OptionCategory MISRACheckerCategory("MISRA Checker Options");
static llvm::cl::extrahelp CommonHelp(
    clang::tooling::CommonOptionsParser::HelpMessage);

int main(int argc, const char** argv) {
    auto ExpectedParser =
        clang::tooling::CommonOptionsParser::create(
            argc, argv, MISRACheckerCategory);
    if (!ExpectedParser) {
        llvm::errs() << ExpectedParser.takeError();
        return 1;
    }
    clang::tooling::CommonOptionsParser& OptionsParser = *ExpectedParser;
    clang::tooling::ClangTool Tool(
        OptionsParser.getCompilations(),
        OptionsParser.getSourcePathList());
    return Tool.run(
        clang::tooling::newFrontendActionFactory<MyFrontendAction>().get());
}